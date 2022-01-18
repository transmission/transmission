# Contributing to Transmission

:+1::tada: Thanks for reading this and considering contributing to the project! :tada::+1:

The following is a set of guidelines for contributing to Transmission. These are just guidelines, not rules, use your best judgment and feel free to propose changes to this document in a pull request.

# What changes would be welcome?

- Bugfixes
- Changes that improve Transmission's compliance with [accepted BEPs](https://www.bittorrent.org/beps/bep_0000.html)
- Changes that improve transfer speeds
- Changes that improve peer communication, indictly improving transfer speeds
- Changes that measurably reduce CPU load
- Changes that measurably reduce memory use
- Changes that improve testing
- Changes that simplify or shrink the existing codebase
- Changes that implement an issue that has a [`pr welcome` label](https://github.com/transmission/transmission/issues?q=is%3Aissue+is%3Aopen+label%3A%22pr+welcome%22)
- Changes that remove deprecated macOS API use in the macOS client
- Changes that remove deprecated GTK API use in the GTK client
- Changes that reduce feature disparity between the different Transmission apps

# Guidelines

## Style

- Try to follow the [C++ core guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines).
- Prefer memory-managed objects over raw pointers
- Prefer `constexpr` over `#define`
- Prefer `enum class` over `enum`
- Prefer new-style headers, e.g. `<cstring>` over `<string.h>`
- Fix any warnings in new code before merging
- Run `./code-style.sh` on your code to ensure the whole codebase has consistent indentation.

Be aware that the project was written in C for over a decade before moving to C++, so the codebase mixes styles. Unfortunately, the legacy code doesn't always meet these guidelines. :eyeroll:

## Considerations

- Prefer commonly-used tools over bespoke ones, e.g. use `std::list` instead of rolling your own list. This simplifies the code and makes it easier for other contributors to work with.
- Please keep new code reasonably decoupled from the rest of the codebase for testability, either with DI or other methods. Be aware that much of the codebase was not written  with testability in mind. See peer-mgr-wishlist for one example of adding new, tested code into an existing untested module.
- When adding advanced features, consider exposing them only in the config file instead of the UI.
  - Transmission has a native macOS app, a native GTK app, and a Qt app. This is a strength in that each client can tightly integrate with its target environment, but it comes at the cost of making GUI changes very time-consuming. So consider, does this feature _need_ to be in the GUI?
- New features must be reachable via the C API and the RPC/JSON API.
  - The macOS and GTK clients still use the C API. Everything else, including a large number of 3rd party applications, use the RPC/JSON API. New features need to be usable via both of these.
- KISS. Transmission is a _huge_ codebase so if you're trying to decide between to approaches to implement something, try the simpler one first.
 
# Questions?

If you have questions or are in doubt, feel free to [file an issue](https://github.com/transmission/transmission/issues/new/choose) or [start a new discussion](https://github.com/transmission/transmission/discussions/new)!

