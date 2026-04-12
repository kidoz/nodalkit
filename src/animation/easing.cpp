#include <nk/animation/easing.h>

namespace nk::easing {

float linear(float t) {
    return t;
}

float ease_in(float t) {
    return t * t * t;
}

float ease_out(float t) {
    const float inv = 1.0F - t;
    return 1.0F - (inv * inv * inv);
}

float ease_in_out(float t) {
    if (t < 0.5F) {
        return 4.0F * t * t * t;
    }
    const float inv = (-2.0F * t) + 2.0F;
    return 1.0F - (inv * inv * inv) * 0.5F;
}

} // namespace nk::easing
