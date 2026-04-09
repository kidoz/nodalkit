#pragma once

/// @file directwrite_text_shaper.h
/// @brief DirectWrite-based text shaper for Windows.

#include "gdi_text_shaper.h"

#include <nk/text/text_shaper.h>

#include <memory>

namespace nk {

class DirectWriteTextShaper : public TextShaper {
public:
    DirectWriteTextShaper();
    ~DirectWriteTextShaper() override;

    [[nodiscard]] Size measure(std::string_view text, const FontDescriptor& font) const override;

    [[nodiscard]] ShapedText
    shape(std::string_view text, const FontDescriptor& font, Color color) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    GdiTextShaper fallback_;
};

} // namespace nk
