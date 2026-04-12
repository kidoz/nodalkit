project('NodalKit', 'cpp',
        version : '0.1.0',
        default_options : ['warning_level=3', 'werror=true', 'cpp_std=c++23'])

nk_inc = include_directories('include')
pkgconfig = import('pkgconfig')
nk_enabled_languages = ['cpp']
cpp_compiler = meson.get_compiler('cpp')

# --- Core sources (platform-agnostic) ---

nk_sources = files(
    'src/animation/easing.cpp',
    'src/animation/timed_animation.cpp',
    'src/foundation/signal.cpp',
    'src/foundation/logging.cpp',
    'src/runtime/event_loop.cpp',
    'src/platform/application.cpp',
    'src/platform/window.cpp',
    'src/platform/platform_backend.cpp',
    'src/platform/system_preferences.cpp',
    'src/debug/diagnostics.cpp',
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
    'src/accessibility/atspi_bridge.cpp',
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
    'src/widgets/segmented_control.cpp',
    'src/widgets/check_box.cpp',
    'src/widgets/radio_button.cpp',
    'src/widgets/switch_widget.cpp',
    'src/widgets/separator.cpp',
    'src/widgets/progress_bar.cpp',
    'src/widgets/tooltip.cpp',
    'src/widgets/slider.cpp',
    'src/widgets/spin_button.cpp',
    'src/widgets/tab_bar.cpp',
    'src/widgets/toolbar.cpp',
    'src/widgets/context_menu.cpp',
    'src/widgets/search_field.cpp',
    'src/widgets/split_view.cpp',
    'src/widgets/info_bar.cpp',
    'src/widgets/expander.cpp',
    'src/widgets/text_area.cpp',
    'src/widgets/popover.cpp',
    'src/widgets/breadcrumb.cpp',
    'src/widgets/calendar.cpp',
    'src/widgets/color_well.cpp',
    'src/widgets/badge.cpp',
    'src/widgets/avatar.cpp',
    'src/widgets/spinner.cpp',
)

nk_deps = []
nk_cpp_args = ['-DBUILDING_NK_LIBRARY']

# --- Platform-specific sources ---

host_system = host_machine.system()

if host_system == 'darwin'
    add_languages('objcpp', native : false)
    nk_enabled_languages += ['objcpp']

    cocoa_dep = dependency('appleframeworks',
        modules : ['Cocoa', 'CoreGraphics', 'CoreText', 'Metal', 'QuartzCore', 'UniformTypeIdentifiers'])

    nk_sources += files(
        'src/platform/macos/macos_backend.mm',
        'src/platform/macos/macos_window.mm',
        'src/render/metal_renderer.mm',
        'src/text/macos/coretext_shaper.mm',
    )

    nk_deps += cocoa_dep

elif host_system == 'linux'
    add_languages('c', native : false)
    nk_enabled_languages += ['c']

    wayland_client_dep = dependency('wayland-client')
    wayland_protocols_dep = dependency('wayland-protocols')
    vulkan_dep = dependency('vulkan', required : false)
    glslang_validator = find_program('glslangValidator', required : false)
    xkbcommon_dep = dependency('xkbcommon')
    freetype_dep = dependency('freetype2')
    fontconfig_dep = dependency('fontconfig')
    harfbuzz_dep = dependency('harfbuzz')
    gio_dep = dependency('gio-2.0')

    # Generate Wayland protocol code via wayland-scanner.
    wayland_scanner = find_program('wayland-scanner')
    wl_protocol_dir = wayland_protocols_dep.get_variable('pkgdatadir')
    xdg_shell_xml = wl_protocol_dir / 'stable' / 'xdg-shell' / 'xdg-shell.xml'
    text_input_v3_xml = wl_protocol_dir / 'unstable' / 'text-input' / 'text-input-unstable-v3.xml'
    primary_selection_v1_xml = wl_protocol_dir / 'unstable' / 'primary-selection' / 'primary-selection-unstable-v1.xml'

    xdg_shell_header = custom_target('xdg-shell-client-header',
        input : xdg_shell_xml,
        output : 'xdg-shell-client-protocol.h',
        command : [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

    xdg_shell_code = custom_target('xdg-shell-private-code',
        input : xdg_shell_xml,
        output : 'xdg-shell-protocol.c',
        command : [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

    text_input_v3_header = custom_target('text-input-v3-client-header',
        input : text_input_v3_xml,
        output : 'text-input-unstable-v3-client-protocol.h',
        command : [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

    text_input_v3_code = custom_target('text-input-v3-private-code',
        input : text_input_v3_xml,
        output : 'text-input-unstable-v3-protocol.c',
        command : [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

    primary_selection_v1_header = custom_target('primary-selection-v1-client-header',
        input : primary_selection_v1_xml,
        output : 'primary-selection-unstable-v1-client-protocol.h',
        command : [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

    primary_selection_v1_code = custom_target('primary-selection-v1-private-code',
        input : primary_selection_v1_xml,
        output : 'primary-selection-unstable-v1-protocol.c',
        command : [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

    nk_sources += files(
        'src/platform/linux/wayland_backend.cpp',
        'src/platform/linux/wayland_portal_helpers.cpp',
        'src/platform/linux/wayland_surface.cpp',
        'src/platform/linux/wayland_input.cpp',
        'src/text/linux/freetype_shaper.cpp',
    )
    nk_sources += [
        xdg_shell_header,
        xdg_shell_code,
        text_input_v3_header,
        text_input_v3_code,
        primary_selection_v1_header,
        primary_selection_v1_code,
    ]

    nk_deps += [wayland_client_dep, xkbcommon_dep, freetype_dep, fontconfig_dep, harfbuzz_dep, gio_dep]

    if vulkan_dep.found() and glslang_validator.found()
        vulkan_primitive_vert_spv = custom_target('vulkan-primitive-vert-spv',
            input : 'src/render/shaders/vulkan_primitive.vert',
            output : 'vulkan_primitive_vert_spv.h',
            command : [
                glslang_validator,
                '-V',
                '-S', 'vert',
                '-x',
                '--vn', 'nk_vulkan_primitive_vert_spv',
                '@INPUT@',
                '-o', '@OUTPUT@',
            ])

        vulkan_primitive_frag_spv = custom_target('vulkan-primitive-frag-spv',
            input : 'src/render/shaders/vulkan_primitive.frag',
            output : 'vulkan_primitive_frag_spv.h',
            command : [
                glslang_validator,
                '-V',
                '-S', 'frag',
                '-x',
                '--vn', 'nk_vulkan_primitive_frag_spv',
                '@INPUT@',
                '-o', '@OUTPUT@',
            ])

        vulkan_image_vert_spv = custom_target('vulkan-image-vert-spv',
            input : 'src/render/shaders/vulkan_image.vert',
            output : 'vulkan_image_vert_spv.h',
            command : [
                glslang_validator,
                '-V',
                '-S', 'vert',
                '-x',
                '--vn', 'nk_vulkan_image_vert_spv',
                '@INPUT@',
                '-o', '@OUTPUT@',
            ])

        vulkan_image_frag_spv = custom_target('vulkan-image-frag-spv',
            input : 'src/render/shaders/vulkan_image.frag',
            output : 'vulkan_image_frag_spv.h',
            command : [
                glslang_validator,
                '-V',
                '-S', 'frag',
                '-x',
                '--vn', 'nk_vulkan_image_frag_spv',
                '@INPUT@',
                '-o', '@OUTPUT@',
            ])

        nk_sources += files('src/render/vulkan_renderer.cpp')
        nk_sources += [
            vulkan_primitive_vert_spv,
            vulkan_primitive_frag_spv,
            vulkan_image_vert_spv,
            vulkan_image_frag_spv,
        ]
        nk_deps += vulkan_dep
        nk_cpp_args += ['-DNK_HAVE_VULKAN']
    endif
elif host_system == 'windows'
    add_project_arguments(
        '-DWIN32_LEAN_AND_MEAN',
        '-DNOMINMAX',
        '-DUNICODE',
        '-D_UNICODE',
        '-D_CRT_SECURE_NO_WARNINGS',
        '-Wno-c2y-extensions',
        language : 'cpp',
    )

    user32_dep = cpp_compiler.find_library('user32', required : true)
    gdi32_dep = cpp_compiler.find_library('gdi32', required : true)
    comdlg32_dep = cpp_compiler.find_library('comdlg32', required : true)
    advapi32_dep = cpp_compiler.find_library('advapi32', required : true)
    dwmapi_dep = cpp_compiler.find_library('dwmapi', required : true)
    shcore_dep = cpp_compiler.find_library('shcore', required : true)
    dwrite_dep = cpp_compiler.find_library('dwrite', required : true)
    d3d11_dep = cpp_compiler.find_library('d3d11', required : true)
    d3dcompiler_dep = cpp_compiler.find_library('d3dcompiler', required : true)
    dxgi_dep = cpp_compiler.find_library('dxgi', required : true)
    vulkan_dep = dependency('vulkan', required : false)
    glslang_validator = find_program('glslangValidator', required : false)

    nk_sources += files(
        'src/platform/windows/win32_backend.cpp',
        'src/render/d3d11_renderer.cpp',
        'src/text/windows/directwrite_text_shaper.cpp',
        'src/text/windows/gdi_text_shaper.cpp',
    )

    nk_deps += [user32_dep, gdi32_dep, comdlg32_dep, advapi32_dep, dwmapi_dep, shcore_dep, dwrite_dep, d3d11_dep, d3dcompiler_dep, dxgi_dep]
    if vulkan_dep.found() and glslang_validator.found()
        vulkan_primitive_vert_spv = custom_target('vulkan-primitive-vert-spv',
            input : 'src/render/shaders/vulkan_primitive.vert',
            output : 'vulkan_primitive_vert_spv.h',
            command : [
                glslang_validator,
                '-V',
                '-S', 'vert',
                '-x',
                '--vn', 'nk_vulkan_primitive_vert_spv',
                '@INPUT@',
                '-o', '@OUTPUT@',
            ])

        vulkan_primitive_frag_spv = custom_target('vulkan-primitive-frag-spv',
            input : 'src/render/shaders/vulkan_primitive.frag',
            output : 'vulkan_primitive_frag_spv.h',
            command : [
                glslang_validator,
                '-V',
                '-S', 'frag',
                '-x',
                '--vn', 'nk_vulkan_primitive_frag_spv',
                '@INPUT@',
                '-o', '@OUTPUT@',
            ])

        vulkan_image_vert_spv = custom_target('vulkan-image-vert-spv',
            input : 'src/render/shaders/vulkan_image.vert',
            output : 'vulkan_image_vert_spv.h',
            command : [
                glslang_validator,
                '-V',
                '-S', 'vert',
                '-x',
                '--vn', 'nk_vulkan_image_vert_spv',
                '@INPUT@',
                '-o', '@OUTPUT@',
            ])

        vulkan_image_frag_spv = custom_target('vulkan-image-frag-spv',
            input : 'src/render/shaders/vulkan_image.frag',
            output : 'vulkan_image_frag_spv.h',
            command : [
                glslang_validator,
                '-V',
                '-S', 'frag',
                '-x',
                '--vn', 'nk_vulkan_image_frag_spv',
                '@INPUT@',
                '-o', '@OUTPUT@',
            ])

        nk_sources += files('src/render/vulkan_renderer.cpp')
        nk_sources += [
            vulkan_primitive_vert_spv,
            vulkan_primitive_frag_spv,
            vulkan_image_vert_spv,
            vulkan_image_frag_spv,
        ]
        nk_deps += vulkan_dep
        nk_cpp_args += ['-DNK_HAVE_VULKAN']
    endif
endif

# --- Sanitizer support ---

if get_option('b_sanitize') != 'none'
    foreach lang : nk_enabled_languages
        add_project_arguments('-fno-omit-frame-pointer', language : lang)
        add_project_link_arguments('-fno-omit-frame-pointer', language : lang)
    endforeach
endif

# --- Build library ---

if host_system == 'windows'
    nk_lib = static_library('NodalKit', nk_sources,
        include_directories : nk_inc,
        dependencies : nk_deps,
        install : true,
        cpp_args : nk_cpp_args,
    )
else
    nk_lib = shared_library('NodalKit', nk_sources,
        include_directories : nk_inc,
        dependencies : nk_deps,
        install : true,
        cpp_args : nk_cpp_args,
    )
endif

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
subdir('tools')
