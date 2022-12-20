// This file Copyright © 2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <CoreFoundation/CoreFoundation.h>

/**
 * @brief YES if a value has stayed the same over time.
 */
template <class FloatType> [[nodiscard]] static inline BOOL FloatEqual(FloatType a, FloatType b);

template <> [[nodiscard]] inline BOOL FloatEqual<CGFloat>(CGFloat a, CGFloat b)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
    return a == b;
#pragma clang diagnostic pop
}
