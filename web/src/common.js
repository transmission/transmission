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

/**
 * Round a string of a number to a specified number of decimal places
 */
Number.prototype.toTruncFixed = function (place) {
  const ret = Math.floor(this * Math.pow(10, place)) / Math.pow(10, place);
  return ret.toFixed(place);
};

Number.prototype.toStringWithCommas = function () {
  return this.toString().replace(/\B(?=(?:\d{3})+(?!\d))/g, ',');
};
