builddir := "build"

# List available recipes
default:
    @just --list

# Configure the project (debug by default)
setup buildtype="debug":
    meson setup {{builddir}} --buildtype={{buildtype}}

# Reconfigure with changed options
reconfigure buildtype="debug":
    meson setup {{builddir}} --reconfigure --buildtype={{buildtype}}

# Build the project
build *args="":
    meson compile -C {{builddir}} {{args}}

# Run all tests
test *args="":
    meson test -C {{builddir}} {{args}}

# Run a single test by name
test-one name:
    meson test -C {{builddir}} {{name}} -v

# Clean build artifacts
clean:
    meson compile -C {{builddir}} --clean

# Remove build directory entirely
distclean:
    rm -rf {{builddir}}

# Rebuild from scratch
rebuild: distclean setup build

# Format all C/C++ sources in-place
format:
    find src include tests examples -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.mm' \) \
        | xargs clang-format -i

# Check formatting without modifying files
format-check:
    find src include tests examples -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.mm' \) \
        | xargs clang-format --dry-run --Werror

# Run clang-tidy on all project sources
lint:
    find src include -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) \
        | xargs clang-tidy -p {{builddir}}

# Run clang-tidy with automatic fixes
lint-fix:
    find src include -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) \
        | xargs clang-tidy -p {{builddir}} --fix

# Install the library
install:
    meson install -C {{builddir}}

# Generate compile_commands.json (already created by meson setup, this copies it to root)
compdb:
    cp {{builddir}}/compile_commands.json .
