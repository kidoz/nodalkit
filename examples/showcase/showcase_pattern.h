#pragma once

/// @file showcase_pattern.h
/// @brief Test-pattern generation for the showcase preview canvas.

#include <cstdint>
#include <vector>

std::vector<std::uint32_t> generate_test_pattern(int width, int height, int frame);
