# Integrate NodalKit into a Meson build

NodalKit is C++23-first. Applications, especially ones with an older core
such as a C++17 emulator, need to consume it without forcing C++23 onto
unrelated targets. This guide covers the supported integration shapes and
how to confine C++23 to the GUI target.

## Choose an integration shape

| Shape | When to use |
| ----- | ----------- |
| Installed SDK (pkg-config) | You install NodalKit once and link many projects against it. See [Consume the installed SDK](consume-the-installed-sdk.md). |
| Meson subproject / wrap | You vendor NodalKit into your build and want it built from source alongside your app. |

## Use NodalKit as a Meson subproject

Place NodalKit under `subprojects/` (directly or via a `.wrap`), then depend
on the dependency object it exports:

```meson
nodalkit_proj = subproject('nodalkit')
nodalkit_dep = nodalkit_proj.get_variable('nk_dep')

executable('frontend', 'main.cpp',
    dependencies : nodalkit_dep,
    override_options : ['cpp_std=c++23,c++latest'],
)
```

`nk_dep` carries the include directories and links the NodalKit library; on
Windows it also pulls in the transitive Win32 import libraries. The ordered
`c++23,c++latest` value keeps the exact C++23 mode on compilers that name it
directly and uses MSVC's `/std:c++latest` spelling otherwise.

## Isolate C++23 to the GUI target

Set the standard per target, not project-wide, so only the NodalKit-facing
code compiles as C++23:

```meson
project('emulator', 'cpp', default_options : ['cpp_std=c++17'])  # core default

# Emulator core and its libraries stay C++17, unchanged.
core_lib = static_library('core', core_sources)

# Only the frontend links NodalKit and compiles as C++23.
executable('frontend',
    'frontend/main.cpp',
    dependencies : [nodalkit_dep, core_dep],
    override_options : ['cpp_std=c++23,c++latest'],
)
```

`override_options` on the GUI target keeps C++23 out of the C/C++ core and
any third-party targets. The only constraint is that the translation units
that include NodalKit headers must be C++23; the C++17 core links against
those translation units normally.

Build every target with the same compiler and standard library. Why this is
the real constraint, and what is safe to pass across the boundary, is
covered in [Mixing C++ standards in one process](../explanation/mixing-cpp-standards.md).

## Link statically on Windows

NodalKit is a `static_library` on Windows. A `pkg-config` consumer gets the
Win32 import libraries automatically from the link line in `nodalkit.pc`. A
subproject consumer gets them through `nk_dep`. Either way you should not
need to list `user32`, `d3d11`, `dwrite`, and the rest by hand.
