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


## Checklist for modernization of a module

- Pass 1: Satisfy clang-tidy
- Pass 2: Group member functions and their parent structs
- Pass 3: Memory - promote init and free functions to C++ ctors and dtors, and ensure it is only managed with new/delete
- Pass 4: Owned memory - promote simple pointer fields owning their data to smart pointers (unique_ptr, shared_ptr)

### Detailed Steps

- Satisfy clang-tidy warnings
    - Change C includes to C++ wraps, example: <string.h> becomes <cstring> and update calls to standard library to
      use `std::` namespace prefix. This is optional but clearly delineates the border between std library and
      transmission.
    - Revisit type warnings, `int` vs `unsigned int`. Sizes and counts should use `size_t` type where this does not
      break external API declarations. Ideally change that too.
- Move member functions into structs. To minimize code churn, create function forward declarations inside structs, and
  give `struct_name::` prefixes to the functions.
   ```c++
   typedef struct {} foo;
   void foo_blep(struct foo *f) {
       f->blergh();
   }
   ```
  becomes:
   ```c++
   struct foo {
       void blep(); 
   };
   void foo::blep() {
       this->blergh(); 
   }
   ```
- Memory management:
    - Prefer constructors and destructors vs manual construction and destruction. But when doing so must ensure that the
      struct is never constructed using C `malloc`/`free`, but must use C++ `new`/`delete`.
    - Avoid using std::memset on a new struct. It is allowed in C, and C++ struct but will destroy virtual table on a
      C++ class. Use field initializers in C++.
- Owned memory:
    - If destructor deletes something, it means it was owned. Promote that field to owning type (vector, unique_ptr,
      shared_ptr or string).
  
