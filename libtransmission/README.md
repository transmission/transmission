# Notes on the C-to-C++ Conversion

- libtransmission was written in C for fifteen years, so eliminating all Cisms is nearly impossible. **Modernization
  patches are welcomed** but it won't all happen overnight. `tr_strdup()` and `constexpr` wil exist side-by-side in the
  codebase for the forseeable future.

- It's so tempting to refactor all the things! Please keep modernization patches reasonably focused so that they will be
  easy to review.

- Prefer `std::` tools over bespoke ones. For example, use `std::vector`
  instead of tr_ptrArray. Redundant bespoke code should be removed.

- Consider ripple effects before adding C++ into public headers. Will it break C code that #includes that header? If you think it might, [consult with downstream projects](https://github.com/transmission/transmission/issues/1826) to see if this is a problem for them.

## Checklist for modernization of a module

> **_NOTE:_**   
> The version in `libtransmission/CMakeLists.txt` is C++17.   
> See https://github.com/AnthonyCalandra/modern-cpp-features

This can be done in multiple smaller passes:

1. Satisfy clang-tidy. `libtransmission/.clang-tidy`'s list of rules should eventually be expanded to a list similar to the one in `qt/.clang-tidy`.
2. Group member functions and their parent structs
    - Use `namespace libtransmission`
    - Split large modules into smaller groups of files in a `libtransmission/<name>` subdirectories, with own
      sub-namespace.
3. Enums replaced with new `enum class` syntax. Numeric `#define` constants replaced with C++ `const`/`constexpr`.
4. Memory - promote init and free functions to C++ ctors and dtors, and ensure it is only managed with new/delete
5. Owned memory - promote simple pointer fields owning their data to smart pointers (unique_ptr, shared_ptr, vector,
   string)

### Detailed Steps

1. Satisfy clang-tidy warnings
    - Change C includes to C++ wraps, example: <string.h> becomes <cstring> and update calls to standard library to
      use `std::` namespace prefix. This clearly delineates the border between std library and transmission. Headers
      must be sorted alphabetically.
    - Headers which are used conditionally based on some `#ifdef` in the code, should also have same `#ifdef` in the
      include section.
    - Revisit type warnings, `int` vs `unsigned int`. Sizes and counts should use `size_t` type where this does not
      break external API declarations. Ideally change that too.
2. Move and group code together.
    - Move member functions into structs. To minimize code churn, create function forward declarations inside structs,
      and give `struct_name::` prefixes to the functions.
       ```c++
       typedef struct {
           int field;
       } foo;
       int foo_blep(struct foo *f) {
           return f->field;
       }
       ```
      becomes:
       ```c++
       struct foo {
           int field;
           void blep(); 
       };
       int foo::blep() {
           return this->field; 
       }
       ```
    - For functions taking `const` pointer, add `const` after the function prototype: `int blep() const` like so.
    - For structs used by other modules, struct definitions should relocate to internal `*-common.h` header files.
    - Split large files into sub-modules sharing own separate sub-namespace and sitting in a subdirectory
      under `libtransmission/`.
    - Some externally invoked functions must either not move OR have `extern "C"` adapter functions.

3. Enums promoted to `enum class` and given a type:
   ```c++
   enum { A, B, C };
   ```
   becomes
   ```c++
   enum: int { A, B, C };        // unscoped, use A, B, C
   enum MyEnum: int { A, B, C }; // unscoped, use A, B, C
   // OR wrap into a scope -
   enum struct MyEnum: int { A, B, C }; // scoped, use MyEnum::A
   enum class MyEnum: int { A, B, C };  // scoped, use MyEnum::A
   ```
   this will make all values of enum to have that numeric type.

4. Numeric/bool `#define` constants should be replaced with C++ `const`/`constexpr`.

6. Memory management:
    - Prefer constructors and destructors vs manual construction and destruction. But when doing so must ensure that the
      struct is never constructed using C `malloc`/`free`, but must use C++ `new`/`delete`.
    - Avoid using std::memset on a new struct. It is allowed in C, and C++ struct but will destroy virtual table on a
      C++ class. Use field initializers in C++.

7. Owned memory:
    - If destructor deletes something, it means it was owned. Promote that field to owning type (vector, unique_ptr,
      shared_ptr or string).
    