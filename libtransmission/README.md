# Notes on the C-to-C++ Conversion

- libtransmission was written in C for fifteen years, so eliminating all
  Cisms is nearly impossible. Modernization patches are welcomed but one
  should accept seeing `tr_strdup()` and `constexpr` side-by-side in the
  codebase.

- C++ MUST NOT be leaked into public headers, e.g. transmission.h. If in
  doubt check `#error only libtransmission should #include this header.`

- C++ tools are preferred everywhere else, including in private headers.
  For example, use std::vector instead of tr_ptrArray. Any private tools
  such as tr_ptrArray should eventually be removed.

- Please keep modernization patches reasonably focused so that they will
  be easy to review.

