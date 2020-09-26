/**
 * Copyright © Dave Perrett, Malcolm Jarvis and Artem Vorotnikov
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

export const isMobileDevice = /(iPhone|iPod|Android)/.test(navigator.userAgent);

/*
 *   Given a numerator and denominator, return a ratio string
 */
Math.ratio = function (numerator, denominator) {
  let result = Math.floor((100 * numerator) / denominator) / 100;

  // check for special cases
  if (result === Number.POSITIVE_INFINITY || result === Number.NEGATIVE_INFINITY) {
    result = -2;
  } else if (isNaN(result)) {
    result = -1;
  }

  return result;
};
