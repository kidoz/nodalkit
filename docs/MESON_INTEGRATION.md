# Meson integration and mixed C++ standards

NodalKit is C++23-first. Applications — especially ones with an older core such
as a C++17 emulator — need to consume it without forcing C++23 onto unrelated
targets. This page covers the supported integration shapes.

## Choosing an integration shape

| Shape | When to use |
| ----- | ----------- |
| Installed SDK (pkg-config) | You install NodalKit once and link many projects against it. See [BUILDING_APPLICATIONS.md](BUILDING_APPLICATIONS.md). |
| Meson subproject / wrap | You vendor NodalKit into your build and want it built from source alongside your app. |

### As a Meson subproject

Place NodalKit under `subprojects/` (directly or via a `.wrap`), then depend on
the dependency object it exports:

```meson
nodalkit_proj = subproject('nodalkit')
nodalkit_dep = nodalkit_proj.get_variable('nk_dep')

executable('frontend', 'main.cpp',
    dependencies : nodalkit_dep,
    override_options : ['cpp_std=c++23'],
)
```

`nk_dep` carries the include directories and links the NodalKit library; on
Windows it also pulls in the transitive Win32 import libraries.

## Isolating C++23 to the GUI target

Set the standard **per target**, not project-wide, so only the NodalKit-facing
code compiles as C++23:

```meson
project('emulator', 'cpp', default_options : ['cpp_std=c++17'])  # core default

# Emulator core and its libraries stay C++17 — unchanged.
core_lib = static_library('core', core_sources)

# Only the frontend links NodalKit and compiles as C++23.
executable('frontend',
    'frontend/main.cpp',
    dependencies : [nodalkit_dep, core_dep],
    override_options : ['cpp_std=c++23'],
)
```

`override_options` on the GUI target keeps C++23 out of the C/C++ core and any
third-party targets. The only constraint is that the **translation units that
include NodalKit headers** must be C++23; the C++17 core links against those TUs
normally.

## Keeping the ABI boundary clean

When a C++23 GUI and a C++17 core live in one process:

- **Use one C++ runtime.** Build every target with the same compiler and
  standard library. On Windows that means one toolchain (clang + lld-link) and
  one CRT (`/MD` vs `/MT`) across all targets — mixing CRTs is the classic cause
  of duplicate-runtime crashes.
- **Do not pass C++ standard-library types with layout that differs by standard
  across the boundary at the ABI level.** In practice, since both halves use the
  same standard library build, `std::string`/`std::vector` are fine; the risk is
  mixing two different runtimes, not two different `-std` flags with one runtime.
- **Prefer a narrow interface** between core and frontend — your own
  application-controller types and POD structs — rather than exposing NodalKit
  types into the C++17 core.

## Static linking on Windows

NodalKit is a `static_library` on Windows. A pkg-config consumer gets the Win32
import libraries automatically from `nodalkit.pc`'s link line (this is what the
`NK-1` build change fixed). A subproject consumer gets them through `nk_dep`.
Either way you should not need to list `user32`, `d3d11`, `dwrite`, etc. by hand.
