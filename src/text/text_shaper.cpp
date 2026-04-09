#include <nk/text/text_shaper.h>

#ifdef __APPLE__
#include "macos/coretext_shaper.h"
#elif defined(_WIN32)
#include "windows/directwrite_text_shaper.h"
#include "windows/gdi_text_shaper.h"
#elif defined(__linux__)
#include "linux/freetype_shaper.h"
#endif

namespace nk {

std::unique_ptr<TextShaper> TextShaper::create() {
#ifdef __APPLE__
    return std::make_unique<CoreTextShaper>();
#elif defined(_WIN32)
    return std::make_unique<DirectWriteTextShaper>();
#elif defined(__linux__)
    return std::make_unique<FreeTypeShaper>();
#else
    return nullptr;
#endif
}

} // namespace nk
