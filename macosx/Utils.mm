// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <cmath>

#import "Utils.h"

bool isSpeedEqual(CGFloat old_speed, CGFloat new_speed)
{
    static CGFloat constexpr kSpeedCompareEps = 0.1 / 2;
    return std::abs(new_speed - old_speed) < kSpeedCompareEps;
}

bool isRatioEqual(CGFloat old_ratio, CGFloat new_ratio)
{
    static CGFloat constexpr kRatioCompareEps = 0.01 / 2;
    return std::abs(new_ratio - old_ratio) < kRatioCompareEps;
}
