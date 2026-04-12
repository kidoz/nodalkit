#pragma once

/// @file easing.h
/// @brief Standard easing functions for animations.

#include <functional>

namespace nk {

/// An easing function maps a linear progress value t ∈ [0, 1] to an
/// eased output value, also nominally in [0, 1] (though overshoot is
/// allowed for elastic or back easings).
using EasingFunction = std::function<float(float t)>;

/// Built-in easing curves.
namespace easing {

/// Linear — no acceleration.
float linear(float t);

/// Cubic ease-in — slow start, accelerating.
float ease_in(float t);

/// Cubic ease-out — fast start, decelerating.
float ease_out(float t);

/// Cubic ease-in-out — slow start and end, fast middle.
float ease_in_out(float t);

} // namespace easing
} // namespace nk
