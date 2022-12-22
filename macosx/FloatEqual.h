// This file Copyright © 2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <CoreFoundation/CoreFoundation.h>

/**
 * @brief YES if a value hasn't changed at all.
 */
template<class FloatType>
[[nodiscard]] static inline BOOL FloatEqualNoWarnings(FloatType a, FloatType b)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
    return a == b;
#pragma clang diagnostic pop
}
