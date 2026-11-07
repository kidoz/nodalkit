#include "coretext_shaper.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nk {

namespace {

constexpr std::size_t MaxCachedFonts = 32;
constexpr std::size_t MaxCachedMeasurements = 2048;
constexpr std::size_t MaxMeasurementKeyBytes = std::size_t{1024} * 1024;
constexpr std::size_t MaxCachedTextBytes = 4096;
constexpr std::size_t MaxCachedFamilyBytes = 1024;

bool same_font(const FontDescriptor& lhs, const FontDescriptor& rhs) {
    return lhs.family == rhs.family && lhs.size == rhs.size && lhs.weight == rhs.weight &&
           lhs.style == rhs.style;
}

struct MeasureKey {
    std::string text;
    FontDescriptor font;
    float width;
    bool wrapped;

    bool operator==(const MeasureKey& other) const {
        return text == other.text && same_font(font, other.font) && width == other.width &&
               wrapped == other.wrapped;
    }
};

struct MeasureLookup {
    std::string_view text;
    const FontDescriptor& font;
    float width;
    bool wrapped;
};

struct MeasureHash {
    using is_transparent = void;

    std::size_t operator()(const MeasureLookup& key) const {
        auto hash = std::hash<std::string_view>{}(key.text);
        const auto combine = [&](std::size_t value) {
            hash ^= value + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        };
        combine(std::hash<std::string_view>{}(key.font.family));
        combine(std::hash<float>{}(key.font.size));
        combine(std::hash<uint16_t>{}(static_cast<uint16_t>(key.font.weight)));
        combine(std::hash<uint8_t>{}(static_cast<uint8_t>(key.font.style)));
        combine(std::hash<float>{}(key.width));
        combine(std::hash<bool>{}(key.wrapped));
        return hash;
    }

    std::size_t operator()(const MeasureKey& key) const {
        return (*this)(MeasureLookup{key.text, key.font, key.width, key.wrapped});
    }
};

struct MeasureEqual {
    using is_transparent = void;

    bool operator()(const MeasureKey& lhs, const MeasureKey& rhs) const { return lhs == rhs; }

    bool operator()(const MeasureLookup& lhs, const MeasureKey& rhs) const {
        return lhs.text == rhs.text && same_font(lhs.font, rhs.font) && lhs.width == rhs.width &&
               lhs.wrapped == rhs.wrapped;
    }

    bool operator()(const MeasureKey& lhs, const MeasureLookup& rhs) const {
        return (*this)(rhs, lhs);
    }
};

struct FontReleaser {
    void operator()(CTFontRef font) const { CFRelease(font); }
};

using FontHandle = std::unique_ptr<std::remove_pointer_t<CTFontRef>, FontReleaser>;

CTFontRef create_ct_font(const FontDescriptor& font) {
    NSString* family = font.family.empty() ? @".AppleSystemUIFont"
                                           : [NSString stringWithUTF8String:font.family.c_str()];

    CTFontSymbolicTraits traits = 0;
    if (font.weight >= FontWeight::Bold) {
        traits |= kCTFontBoldTrait;
    }
    if (font.style == FontStyle::Italic || font.style == FontStyle::Oblique) {
        traits |= kCTFontItalicTrait;
    }

    NSDictionary* attributes = @{
        (id)kCTFontFamilyNameAttribute : family,
        (id)kCTFontTraitsAttribute : @{
            (id)kCTFontSymbolicTrait : @(traits),
        },
    };
    CTFontDescriptorRef descriptor =
        CTFontDescriptorCreateWithAttributes((CFDictionaryRef)attributes);
    CTFontRef ct_font = CTFontCreateWithFontDescriptor(descriptor, font.size, nullptr);
    CFRelease(descriptor);
    return ct_font;
}

CFAttributedStringRef create_attributed_string(std::string_view text, CTFontRef font, Color color) {
    @autoreleasepool {
        NSString* str = [[[NSString alloc] initWithBytes:text.data()
                                                  length:text.size()
                                                encoding:NSUTF8StringEncoding] autorelease];
        if (!str) {
            str = @"";
        }

        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGFloat components[] = {
            static_cast<CGFloat>(color.r),
            static_cast<CGFloat>(color.g),
            static_cast<CGFloat>(color.b),
            static_cast<CGFloat>(color.a),
        };
        CGColorRef cg_color = CGColorCreate(cs, components);
        CGColorSpaceRelease(cs);

        NSDictionary* attrs = @{
            (id)kCTFontAttributeName : (__bridge id)font,
            (id)kCTForegroundColorAttributeName : (__bridge id)cg_color,
        };

        CFAttributedStringRef result =
            CFAttributedStringCreate(nullptr, (CFStringRef)str, (CFDictionaryRef)attrs);
        CGColorRelease(cg_color);
        return result;
    }
}

void convert_premultiplied_to_straight_rgba(std::vector<uint8_t>& bitmap) {
    for (std::size_t index = 0; index + 3 < bitmap.size(); index += 4) {
        const auto alpha = bitmap[index + 3];
        if (alpha == 0) {
            bitmap[index + 0] = 0;
            bitmap[index + 1] = 0;
            bitmap[index + 2] = 0;
            continue;
        }
        if (alpha == 255) {
            continue;
        }

        const float scale = 255.0F / static_cast<float>(alpha);
        bitmap[index + 0] = static_cast<uint8_t>(
            std::clamp(std::lround(static_cast<float>(bitmap[index + 0]) * scale), 0L, 255L));
        bitmap[index + 1] = static_cast<uint8_t>(
            std::clamp(std::lround(static_cast<float>(bitmap[index + 1]) * scale), 0L, 255L));
        bitmap[index + 2] = static_cast<uint8_t>(
            std::clamp(std::lround(static_cast<float>(bitmap[index + 2]) * scale), 0L, 255L));
    }
}

} // namespace

struct CoreTextShaper::Impl {
    struct CachedFont {
        FontDescriptor descriptor;
        FontHandle font;
    };

    using Measurements = std::list<std::pair<MeasureKey, Size>>;

    std::list<CachedFont> fonts;
    Measurements measurements;
    std::unordered_map<MeasureKey, Measurements::iterator, MeasureHash, MeasureEqual> measure_map;
    std::size_t measurement_key_bytes = 0;
    float scale_factor = 1.0F;

    // The caller owns the returned reference, whether it came from the cache or CoreText.
    CTFontRef retain_font(const FontDescriptor& descriptor) {
        if (!std::isfinite(descriptor.size) || descriptor.size <= 0.0F ||
            descriptor.family.size() > MaxCachedFamilyBytes) {
            return create_ct_font(descriptor);
        }
        for (auto it = fonts.begin(); it != fonts.end(); ++it) {
            if (same_font(it->descriptor, descriptor)) {
                fonts.splice(fonts.begin(), fonts, it);
                return static_cast<CTFontRef>(CFRetain(fonts.front().font.get()));
            }
        }
        fonts.push_front({descriptor, FontHandle(create_ct_font(descriptor))});
        if (fonts.size() > MaxCachedFonts) {
            fonts.pop_back();
        }
        return static_cast<CTFontRef>(CFRetain(fonts.front().font.get()));
    }

    static bool cacheable(const MeasureLookup& key) {
        return key.text.size() <= MaxCachedTextBytes &&
               key.font.family.size() <= MaxCachedFamilyBytes && std::isfinite(key.font.size) &&
               key.font.size > 0.0F &&
               (!key.wrapped || (std::isfinite(key.width) && key.width > 0.0F));
    }

    const Size* find_measure(const MeasureLookup& key) {
        if (!cacheable(key)) {
            return nullptr;
        }
        const auto it = measure_map.find(key);
        if (it == measure_map.end()) {
            return nullptr;
        }
        measurements.splice(measurements.begin(), measurements, it->second);
        return &it->second->second;
    }

    void insert_measure(const MeasureLookup& key, Size measured) {
        if (!cacheable(key)) {
            return;
        }
        // Both the LRU list and the lookup map own their keys.
        const auto bytes = 2 * (key.text.size() + key.font.family.size());
        while (measurements.size() >= MaxCachedMeasurements ||
               measurement_key_bytes + bytes > MaxMeasurementKeyBytes) {
            const auto& oldest = measurements.back().first;
            measurement_key_bytes -= 2 * (oldest.text.size() + oldest.font.family.size());
            measure_map.erase(oldest);
            measurements.pop_back();
        }
        measurements.emplace_front(
            MeasureKey{std::string(key.text), key.font, key.width, key.wrapped}, measured);
        try {
            measure_map.emplace(measurements.front().first, measurements.begin());
        } catch (...) {
            measurements.pop_front();
            throw;
        }
        measurement_key_bytes += bytes;
    }

    void clear() {
        measure_map.clear();
        measurements.clear();
        measurement_key_bytes = 0;
        fonts.clear();
    }
};

CoreTextShaper::CoreTextShaper() : impl_(std::make_unique<Impl>()) {}

CoreTextShaper::~CoreTextShaper() = default;

void CoreTextShaper::set_scale_factor(float scale_factor) {
    if (std::isfinite(scale_factor) && scale_factor > 0.0F && impl_->scale_factor != scale_factor) {
        impl_->scale_factor = scale_factor;
        impl_->clear();
    }
}

void CoreTextShaper::set_system_default_family(std::string_view /*family*/, bool /*monospace*/) {
    impl_->clear();
}

void CoreTextShaper::set_system_document_family(std::string_view /*family*/) {
    impl_->clear();
}

Size CoreTextShaper::measure(std::string_view text, const FontDescriptor& font) const {
    const MeasureLookup key{text, font, 0.0F, false};
    if (const auto* cached = impl_->find_measure(key)) {
        return *cached;
    }
    @autoreleasepool {
        CTFontRef ct_font = impl_->retain_font(font);
        CFAttributedStringRef attr_str = create_attributed_string(text, ct_font, {0, 0, 0, 1});

        CTLineRef line = CTLineCreateWithAttributedString(attr_str);
        CGRect bounds = CTLineGetBoundsWithOptions(line, 0);

        CFRelease(line);
        CFRelease(attr_str);
        CFRelease(ct_font);

        const Size measured{static_cast<float>(std::ceil(bounds.size.width)),
                            static_cast<float>(std::ceil(bounds.size.height))};
        impl_->insert_measure(key, measured);
        return measured;
    }
}

ShapedText
CoreTextShaper::shape(std::string_view text, const FontDescriptor& font, Color color) const {
    @autoreleasepool {
        CTFontRef ct_font = impl_->retain_font(font);
        CFAttributedStringRef attr_str = create_attributed_string(text, ct_font, color);

        CTLineRef line = CTLineCreateWithAttributedString(attr_str);

        // Measure.
        CGFloat ascent = 0;
        CGFloat descent = 0;
        CGFloat leading = 0;
        double line_width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
        const int w = static_cast<int>(std::ceil(line_width));
        const int h = static_cast<int>(std::ceil(ascent + descent + leading));

        ShapedText result;
        result.set_text_size({static_cast<float>(w), static_cast<float>(h)});
        result.set_baseline(static_cast<float>(ascent));

        if (w > 0 && h > 0) {
            // Rasterize into an RGBA8 bitmap.
            const auto stride = static_cast<std::size_t>(w * 4);
            std::vector<uint8_t> bitmap(stride * static_cast<std::size_t>(h), 0);

            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx =
                CGBitmapContextCreate(bitmap.data(),
                                      static_cast<size_t>(w),
                                      static_cast<size_t>(h),
                                      8,
                                      stride,
                                      cs,
                                      kCGImageAlphaPremultipliedLast |
                                          static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big));
            CGColorSpaceRelease(cs);

            if (ctx) {
                // CoreText already produces bitmap rows in the orientation
                // the software renderer expects here. Flipping the rows
                // afterward inverts glyphs on screen.
                CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
                CGContextSetTextPosition(ctx, 0, descent);
                CTLineDraw(line, ctx);
                CGContextRelease(ctx);
            }

            convert_premultiplied_to_straight_rgba(bitmap);
            result.set_bitmap(std::move(bitmap), w, h);
        }

        CFRelease(line);
        CFRelease(attr_str);
        CFRelease(ct_font);

        return result;
    }
}

Size CoreTextShaper::measure_wrapped(std::string_view text,
                                     const FontDescriptor& font,
                                     float max_width) const {
    const MeasureLookup key{text, font, max_width, true};
    if (const auto* cached = impl_->find_measure(key)) {
        return *cached;
    }
    @autoreleasepool {
        CTFontRef ct_font = impl_->retain_font(font);
        CFAttributedStringRef attr_str = create_attributed_string(text, ct_font, {0, 0, 0, 1});

        CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(attr_str);

        CFRange fit_range{};
        CGSize constraints = CGSizeMake(static_cast<CGFloat>(max_width), CGFLOAT_MAX);
        CGSize frame_size = CTFramesetterSuggestFrameSizeWithConstraints(
            framesetter, CFRangeMake(0, 0), nullptr, constraints, &fit_range);

        CFRelease(framesetter);
        CFRelease(attr_str);
        CFRelease(ct_font);

        const Size measured{static_cast<float>(std::ceil(frame_size.width)),
                            static_cast<float>(std::ceil(frame_size.height))};
        impl_->insert_measure(key, measured);
        return measured;
    }
}

ShapedText CoreTextShaper::shape_wrapped(std::string_view text,
                                         const FontDescriptor& font,
                                         Color color,
                                         float max_width) const {
    @autoreleasepool {
        CTFontRef ct_font = impl_->retain_font(font);
        CFAttributedStringRef attr_str = create_attributed_string(text, ct_font, color);

        CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(attr_str);

        // Measure to determine bitmap size.
        CFRange fit_range{};
        CGSize constraints = CGSizeMake(static_cast<CGFloat>(max_width), CGFLOAT_MAX);
        CGSize frame_size = CTFramesetterSuggestFrameSizeWithConstraints(
            framesetter, CFRangeMake(0, 0), nullptr, constraints, &fit_range);

        const int w = static_cast<int>(std::ceil(frame_size.width));
        const int h = static_cast<int>(std::ceil(frame_size.height));

        ShapedText result;
        result.set_text_size({static_cast<float>(w), static_cast<float>(h)});

        // Baseline of the first line.
        CGMutablePathRef path = CGPathCreateMutable();
        CGPathAddRect(path, nullptr, CGRectMake(0, 0, frame_size.width, frame_size.height));
        CTFrameRef frame = CTFramesetterCreateFrame(framesetter, CFRangeMake(0, 0), path, nullptr);

        CFArrayRef lines = CTFrameGetLines(frame);
        CFIndex line_count = CFArrayGetCount(lines);
        if (line_count > 0) {
            auto* first_line = static_cast<CTLineRef>(CFArrayGetValueAtIndex(lines, 0));
            CGFloat ascent = 0;
            CTLineGetTypographicBounds(first_line, &ascent, nullptr, nullptr);
            result.set_baseline(static_cast<float>(ascent));
        }

        if (w > 0 && h > 0) {
            const auto stride = static_cast<std::size_t>(w * 4);
            std::vector<uint8_t> bitmap(stride * static_cast<std::size_t>(h), 0);

            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx =
                CGBitmapContextCreate(bitmap.data(),
                                      static_cast<size_t>(w),
                                      static_cast<size_t>(h),
                                      8,
                                      stride,
                                      cs,
                                      kCGImageAlphaPremultipliedLast |
                                          static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big));
            CGColorSpaceRelease(cs);

            if (ctx) {
                CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
                // Draw each line manually with correct top-down
                // positioning. CoreText origins are bottom-up, but
                // the bitmap context has (0,0) at bottom-left.
                std::vector<CGPoint> origins(static_cast<std::size_t>(line_count));
                CTFrameGetLineOrigins(frame, CFRangeMake(0, 0), origins.data());

                for (CFIndex i = 0; i < line_count; ++i) {
                    auto* line_ref = static_cast<CTLineRef>(CFArrayGetValueAtIndex(lines, i));
                    CGContextSetTextPosition(ctx,
                                             origins[static_cast<std::size_t>(i)].x,
                                             origins[static_cast<std::size_t>(i)].y);
                    CTLineDraw(line_ref, ctx);
                }
                CGContextRelease(ctx);
            }

            convert_premultiplied_to_straight_rgba(bitmap);
            result.set_bitmap(std::move(bitmap), w, h);
        }

        CFRelease(frame);
        CGPathRelease(path);
        CFRelease(framesetter);
        CFRelease(attr_str);
        CFRelease(ct_font);

        return result;
    }
}

} // namespace nk
