project('NodalKit', 'cpp',
        version : '0.1.0',
        default_options : ['warning_level=3', 'werror=true', 'cpp_std=c++23'])

nk_inc = include_directories('include')
pkgconfig = import('pkgconfig')

# --- Core sources (platform-agnostic) ---

nk_sources = files(
    'src/foundation/signal.cpp',
    'src/foundation/logging.cpp',
    'src/runtime/event_loop.cpp',
    'src/platform/application.cpp',
    'src/platform/window.cpp',
    'src/platform/platform_backend.cpp',
    'src/platform/system_preferences.cpp',
    'src/ui_core/widget.cpp',
    'src/layout/layout_manager.cpp',
    'src/layout/box_layout.cpp',
    'src/layout/grid_layout.cpp',
    'src/layout/stack_layout.cpp',
    'src/controllers/event_controller.cpp',
    'src/actions/action.cpp',
    'src/render/render_node.cpp',
    'src/render/renderer.cpp',
    'src/render/image_node.cpp',
    'src/render/snapshot_context.cpp',
    'src/style/theme.cpp',
    'src/style/theme_selection.cpp',
    'src/accessibility/accessible.cpp',
    'src/model/abstract_list_model.cpp',
    'src/model/selection_model.cpp',
    'src/text/shaped_text.cpp',
    'src/text/text_shaper.cpp',
    'src/widgets/label.cpp',
    'src/widgets/button.cpp',
    'src/widgets/text_field.cpp',
    'src/widgets/scroll_area.cpp',
    'src/widgets/list_view.cpp',
    'src/widgets/dialog.cpp',
    'src/widgets/image_view.cpp',
    'src/widgets/menu_bar.cpp',
    'src/widgets/status_bar.cpp',
    'src/widgets/combo_box.cpp',
)

nk_deps = []
nk_cpp_args = ['-DBUILDING_NK_LIBRARY']

# --- Platform-specific sources ---

host_system = host_machine.system()

if host_system == 'darwin'
    add_languages('objcpp', native : false)

    cocoa_dep = dependency('appleframeworks',
        modules : ['Cocoa', 'CoreGraphics', 'CoreText', 'UniformTypeIdentifiers'])

    nk_sources += files(
        'src/platform/macos/macos_backend.mm',
        'src/platform/macos/macos_window.mm',
        'src/text/macos/coretext_shaper.mm',
    )

    nk_deps += cocoa_dep

elif host_system == 'linux'
    add_languages('c', native : false)

    wayland_client_dep = dependency('wayland-client')
    wayland_protocols_dep = dependency('wayland-protocols')
    xkbcommon_dep = dependency('xkbcommon')
    freetype_dep = dependency('freetype2')
    fontconfig_dep = dependency('fontconfig')
    harfbuzz_dep = dependency('harfbuzz')
    gio_dep = dependency('gio-2.0')

    # Generate XDG shell protocol code via wayland-scanner.
    wayland_scanner = find_program('wayland-scanner')
    wl_protocol_dir = wayland_protocols_dep.get_variable('pkgdatadir')
    xdg_shell_xml = wl_protocol_dir / 'stable' / 'xdg-shell' / 'xdg-shell.xml'

    xdg_shell_header = custom_target('xdg-shell-client-header',
        input : xdg_shell_xml,
        output : 'xdg-shell-client-protocol.h',
        command : [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

    xdg_shell_code = custom_target('xdg-shell-private-code',
        input : xdg_shell_xml,
        output : 'xdg-shell-protocol.c',
        command : [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

    nk_sources += files(
        'src/platform/linux/wayland_backend.cpp',
        'src/platform/linux/wayland_portal_helpers.cpp',
        'src/platform/linux/wayland_surface.cpp',
        'src/platform/linux/wayland_input.cpp',
        'src/text/linux/freetype_shaper.cpp',
    )
    nk_sources += [xdg_shell_header, xdg_shell_code]

    nk_deps += [wayland_client_dep, xkbcommon_dep, freetype_dep, fontconfig_dep, harfbuzz_dep, gio_dep]
endif

# --- Build shared library ---

nk_lib = shared_library('NodalKit', nk_sources,
    include_directories : nk_inc,
    dependencies : nk_deps,
    install : true,
    cpp_args : nk_cpp_args,
)

nk_dep = declare_dependency(
    include_directories : nk_inc,
    link_with : nk_lib,
)

install_subdir('include/nk',
    install_dir : get_option('includedir') / 'nk',
    strip_directory : true,
)

pkgconfig.generate(
    libraries : nk_lib,
    filebase : 'nodalkit',
    name : 'NodalKit',
    description : 'C++-first GUI toolkit for modern desktop applications',
    version : meson.project_version(),
)

subdir('examples')
subdir('tests')
