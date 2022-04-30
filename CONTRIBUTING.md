# Contributing to Transmission

Thanks for reading this and thinking about contributing! :tada:

This page is a list of suggestions and guidelines for contributing. They're not rules, just guidelines. Use your best judgment and feel free to propose changes to this document in a pull request.

# If you've got a change in mind

New people usually start volunteering because they have an itch they want to scratch. If you already know what you want to work on first, please comment in an existing issue, or [file a new issue](https://github.com/transmission/transmission/issues/new/choose) or [start a new discussion](https://github.com/transmission/transmission/discussions/new)! The maintainers will try to get you the information you need.

# If you're looking for ideas

If not, there are three labels in the issues tracker that can help:

- [`help wanted`](https://github.com/transmission/transmission/issues?q=is%3Aissue+is%3Aopen+label%3A%22help+wanted%22) indicates that the issue is stuck and needs an outside developer. This is usually because some domain expertise is needed, e.g. for a specific platform or external API.
- [`good first issue`](https://github.com/transmission/transmission/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22) is a `pr-welcome` issue that is probably on the easier side to code.
- [`pr welcome`](https://github.com/transmission/transmission/issues?q=is%3Aissue+is%3Aopen+label%3A%22pr+welcome%22) is for features that have been requested and which that the project doesn't have resources to implement, but would consider adding if a good PR was submitted.

The project also welcomes changes that:

- improve Transmission's compliance with [accepted BEPs](https://www.bittorrent.org/beps/bep_0000.html)
- improve transfer speeds or peer communication
- reduce the app's footprint in CPU or memory use
- improve testing
- simplify / shrink the existing codebase
- remove deprecated macOS API use in the macOS client
- remove deprecated GTK API use in the GTK client
- reduce feature disparity between the different Transmission apps

# Mechanics

## Getting Started

On macOS, Transmission is usually built with Xcode. Everywhere else, it's CMake + the development environment of your choice. If you need to add source files but don't have Xcode, a maintainer can help you to update the Xcode project file. See [README.md](README.md) for information on building Transmission from source.

## Style

- Try to follow the [C++ core guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines).
- Prefer memory-managed objects over raw pointers
- Prefer `constexpr` over `#define`
- Prefer `enum class` over `enum`
- Prefer new-style headers, e.g. `<cstring>` over `<string.h>`
- Fix any warnings in new code before merging
- Run `./code-style.sh` on your code to ensure the whole codebase has consistent indentation.

Note that Transmission existed in C for over a decade and those idioms don't change overnight. "Follow the C++ core guidelines" can be difficult when working with older code, and the maintainers will understand that when reviewing your PRs. :smiley:

## Considerations

- Prefer commonly-used tools over bespoke ones, e.g. use `std::list` instead of rolling your own list. This simplifies the code and makes it easier for other contributors to work with.
- Please keep new code reasonably decoupled from the rest of the codebase for testability, either with DI or other methods. Be aware that much of the codebase was not written  with testability in mind. See peer-mgr-wishlist for one example of adding new, tested code into an existing untested module.
- When adding advanced features, consider exposing them only in the config file instead of the UI.
  - Transmission has a native macOS app, a native GTK app, and a Qt app. This is a strength in that each client can tightly integrate with its target environment, but it comes at the cost of making GUI changes very time-consuming. So consider, does this feature _need_ to be in the GUI?
- New features must be reachable via the C API and the RPC/JSON API.
  - The macOS and GTK clients still use the C API. Everything else, including a large number of 3rd party applications, use the RPC/JSON API. New features need to be usable via both of these.
- KISS. Transmission is a _huge_ codebase so if you're trying to decide between to approaches to implement something, try the simpler one first.
