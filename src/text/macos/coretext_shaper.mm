#include "coretext_shaper.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

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

            result.set_bitmap(std::move(bitmap), w, h);
        }

        CFRelease(line);
        CFRelease(attr_str);
        CFRelease(ct_font);

        return result;
    }
}

} // namespace nk
