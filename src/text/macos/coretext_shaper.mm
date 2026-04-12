#include "coretext_shaper.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nk {

CoreTextShaper::CoreTextShaper() = default;
CoreTextShaper::~CoreTextShaper() = default;

namespace {

CTFontRef create_ct_font(FontDescriptor const& font) {
    NSString* family = font.family.empty()
        ? @".AppleSystemUIFont"
        : [NSString stringWithUTF8String:font.family.c_str()];

    CTFontSymbolicTraits traits = 0;
    if (font.weight >= FontWeight::Bold) {
        traits |= kCTFontBoldTrait;
    }
    if (font.style == FontStyle::Italic || font.style == FontStyle::Oblique) {
        traits |= kCTFontItalicTrait;
    }

    NSDictionary* attributes = @{
        (id)kCTFontFamilyNameAttribute: family,
        (id)kCTFontTraitsAttribute: @{
            (id)kCTFontSymbolicTrait: @(traits),
        },
    };
    CTFontDescriptorRef descriptor =
        CTFontDescriptorCreateWithAttributes((CFDictionaryRef)attributes);
    CTFontRef ct_font =
        CTFontCreateWithFontDescriptor(descriptor, font.size, nullptr);
    CFRelease(descriptor);
    return ct_font;
}

CFAttributedStringRef create_attributed_string(
    std::string_view text, CTFontRef font, Color color) {
    @autoreleasepool {
        NSString* str = [[NSString alloc]
            initWithBytes:text.data()
                   length:text.size()
                 encoding:NSUTF8StringEncoding];
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
            (id)kCTFontAttributeName: (__bridge id)font,
            (id)kCTForegroundColorAttributeName: (__bridge id)cg_color,
        };

        CFAttributedStringRef result = CFAttributedStringCreate(
            nullptr, (CFStringRef)str, (CFDictionaryRef)attrs);
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

Size CoreTextShaper::measure(
    std::string_view text, FontDescriptor const& font) const {
    @autoreleasepool {
        CTFontRef ct_font = create_ct_font(font);
        CFAttributedStringRef attr_str =
            create_attributed_string(text, ct_font, {0, 0, 0, 1});

        CTLineRef line = CTLineCreateWithAttributedString(attr_str);
        CGRect bounds = CTLineGetBoundsWithOptions(line, 0);

        CFRelease(line);
        CFRelease(attr_str);
        CFRelease(ct_font);

        return {static_cast<float>(std::ceil(bounds.size.width)),
                static_cast<float>(std::ceil(bounds.size.height))};
    }
}

ShapedText CoreTextShaper::shape(
    std::string_view text, FontDescriptor const& font,
    Color color) const {
    @autoreleasepool {
        CTFontRef ct_font = create_ct_font(font);
        CFAttributedStringRef attr_str =
            create_attributed_string(text, ct_font, color);

        CTLineRef line = CTLineCreateWithAttributedString(attr_str);

        // Measure.
        CGFloat ascent = 0;
        CGFloat descent = 0;
        CGFloat leading = 0;
        double line_width =
            CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
        int const w = static_cast<int>(std::ceil(line_width));
        int const h = static_cast<int>(std::ceil(ascent + descent + leading));

        ShapedText result;
        result.set_text_size({static_cast<float>(w), static_cast<float>(h)});
        result.set_baseline(static_cast<float>(ascent));

        if (w > 0 && h > 0) {
            // Rasterize into an RGBA8 bitmap.
            auto const stride = static_cast<std::size_t>(w * 4);
            std::vector<uint8_t> bitmap(stride * static_cast<std::size_t>(h), 0);

            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(
                bitmap.data(), static_cast<size_t>(w),
                static_cast<size_t>(h), 8, stride, cs,
                kCGImageAlphaPremultipliedLast | static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big));
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

Size CoreTextShaper::measure_wrapped(
    std::string_view text, FontDescriptor const& font,
    float max_width) const {
    @autoreleasepool {
        CTFontRef ct_font = create_ct_font(font);
        CFAttributedStringRef attr_str =
            create_attributed_string(text, ct_font, {0, 0, 0, 1});

        CTFramesetterRef framesetter =
            CTFramesetterCreateWithAttributedString(attr_str);

        CFRange fit_range{};
        CGSize constraints = CGSizeMake(
            static_cast<CGFloat>(max_width), CGFLOAT_MAX);
        CGSize frame_size =
            CTFramesetterSuggestFrameSizeWithConstraints(
                framesetter, CFRangeMake(0, 0), nullptr,
                constraints, &fit_range);

        CFRelease(framesetter);
        CFRelease(attr_str);
        CFRelease(ct_font);

        return {static_cast<float>(std::ceil(frame_size.width)),
                static_cast<float>(std::ceil(frame_size.height))};
    }
}

ShapedText CoreTextShaper::shape_wrapped(
    std::string_view text, FontDescriptor const& font,
    Color color, float max_width) const {
    @autoreleasepool {
        CTFontRef ct_font = create_ct_font(font);
        CFAttributedStringRef attr_str =
            create_attributed_string(text, ct_font, color);

        CTFramesetterRef framesetter =
            CTFramesetterCreateWithAttributedString(attr_str);

        // Measure to determine bitmap size.
        CFRange fit_range{};
        CGSize constraints = CGSizeMake(
            static_cast<CGFloat>(max_width), CGFLOAT_MAX);
        CGSize frame_size =
            CTFramesetterSuggestFrameSizeWithConstraints(
                framesetter, CFRangeMake(0, 0), nullptr,
                constraints, &fit_range);

        int const w = static_cast<int>(std::ceil(frame_size.width));
        int const h = static_cast<int>(std::ceil(frame_size.height));

        ShapedText result;
        result.set_text_size(
            {static_cast<float>(w), static_cast<float>(h)});

        // Baseline of the first line.
        CGMutablePathRef path = CGPathCreateMutable();
        CGPathAddRect(path, nullptr,
                      CGRectMake(0, 0, frame_size.width,
                                 frame_size.height));
        CTFrameRef frame = CTFramesetterCreateFrame(
            framesetter, CFRangeMake(0, 0), path, nullptr);

        CFArrayRef lines = CTFrameGetLines(frame);
        CFIndex line_count = CFArrayGetCount(lines);
        if (line_count > 0) {
            auto* first_line = static_cast<CTLineRef>(
                CFArrayGetValueAtIndex(lines, 0));
            CGFloat ascent = 0;
            CTLineGetTypographicBounds(first_line, &ascent, nullptr,
                                       nullptr);
            result.set_baseline(static_cast<float>(ascent));
        }

        if (w > 0 && h > 0) {
            auto const stride = static_cast<std::size_t>(w * 4);
            std::vector<uint8_t> bitmap(
                stride * static_cast<std::size_t>(h), 0);

            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(
                bitmap.data(), static_cast<size_t>(w),
                static_cast<size_t>(h), 8, stride, cs,
                kCGImageAlphaPremultipliedLast |
                    static_cast<CGBitmapInfo>(
                        kCGBitmapByteOrder32Big));
            CGColorSpaceRelease(cs);

            if (ctx) {
                CGContextSetTextMatrix(ctx,
                                       CGAffineTransformIdentity);
                // Draw each line manually with correct top-down
                // positioning. CoreText origins are bottom-up, but
                // the bitmap context has (0,0) at bottom-left.
                std::vector<CGPoint> origins(
                    static_cast<std::size_t>(line_count));
                CTFrameGetLineOrigins(
                    frame, CFRangeMake(0, 0), origins.data());

                for (CFIndex i = 0; i < line_count; ++i) {
                    auto* line_ref = static_cast<CTLineRef>(
                        CFArrayGetValueAtIndex(lines, i));
                    CGContextSetTextPosition(
                        ctx, origins[static_cast<std::size_t>(i)].x,
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
