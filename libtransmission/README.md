# Notes on the C-to-C++ Conversion

- libtransmission was written in C for fifteen years, so eliminating all
  Cisms is nearly impossible. **Modernization patches are welcomed** but
  it won't all happen overnight. `tr_strdup()` and `constexpr` wil exist
  side-by-side in the codebase for the forseeable future.

- It's so tempting to refactor all the things! Please keep modernization
  patches reasonably focused so that they will be easy to review.

- Prefer `std::` tools over bespoke ones. For example, use `std::vector`
  instead of tr_ptrArray. Redundant bespoke code should be removed.

- Consider ripple effects before adding C++ into public headers. Will it
  break C code that #includes that header?
