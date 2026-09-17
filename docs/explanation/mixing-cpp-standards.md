# Mixing C++ standards in one process

This page explains what is and is not safe when a C++23 NodalKit frontend
and a C++17 core live in the same process. The build configuration that
sets this up is in
[Integrate NodalKit into a Meson build](../how-to/integrate-with-meson.md).

## The real constraint is one runtime, not one standard

Build every target with the same compiler and standard library. On Windows
that means one toolchain (clang + lld-link) and one CRT (`/MD` versus `/MT`)
across all targets. Mixing CRTs is the classic cause of duplicate-runtime
crashes: two copies of the heap, two sets of locale state, and objects freed
by a different allocator than the one that created them.

Two different `-std` flags on top of one runtime are a much smaller risk.
The standard library's types keep the same layout whether a translation
unit is compiled as C++17 or C++23, because the library is one build. In
practice, since both halves use the same standard library, passing
`std::string` or `std::vector` across the boundary is fine.

## Where the C++23 requirement actually applies

Only translation units that include NodalKit headers must be compiled as
C++23. The C++17 core links against those translation units normally. That
is why the recommended Meson setup uses `override_options` on the GUI
target rather than raising the project default.

## Prefer a narrow interface anyway

Even though standard-library types cross the boundary safely, it is better
to keep the interface between core and frontend narrow: your own
application-controller types and plain structs, rather than NodalKit types
exposed into the C++17 core. This keeps the core testable without the
toolkit and keeps the frontend free to change how it renders state. The
broader ownership model is in
[Application architecture](application-architecture.md).
