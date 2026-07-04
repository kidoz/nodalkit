catch2_test_dep = dependency('catch2-with-main', required : false)
if not catch2_test_dep.found()
    catch2_proj = subproject('catch2', default_options : ['tests=false', 'install=false'])
    catch2_test_dep = catch2_proj.get_variable('catch2_with_main_dep')
endif

# Point LeakSanitizer at our suppressions file for every test run. This is a no-op
# for non-sanitizer builds (LSAN_OPTIONS is ignored) and, under -Db_sanitize, stops
# the third-party libdbus/AT-SPI atk-bridge leaks from failing the suite while still
# catching genuine NodalKit leaks. See lsan_suppressions.txt.
add_test_setup('nodalkit',
    env : {'LSAN_OPTIONS': 'suppressions=' + (meson.current_source_dir() / 'lsan_suppressions.txt')},
    is_default : true,
)

test_signal = executable('test_signal',
    'signal_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('signal', test_signal)

test_property = executable('test_property',
    'property_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('property', test_property)

test_app = executable('test_basic_app',
    'basic_app_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('basic_app', test_app)

test_text_shaper = executable('test_text_shaper',
    'text_shaper_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('text_shaper', test_text_shaper)

test_renderer = executable('test_renderer',
    'renderer_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('renderer', test_renderer)

test_theme = executable('test_theme',
    'theme_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('theme', test_theme)

test_service_locator = executable('test_service_locator',
    'service_locator_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('service_locator', test_service_locator)

test_state_store = executable('test_state_store',
    'state_store_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('state_store', test_state_store)

test_model = executable('test_model',
    'model_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('model', test_model)

test_settings = executable('test_settings',
    'settings_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('settings', test_settings)

test_drag_drop = executable('test_drag_drop',
    'drag_drop_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('drag_drop', test_drag_drop)

test_widget_controls = executable('test_widget_controls',
    'widget_controls_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('widget_controls', test_widget_controls)

test_widget_views = executable('test_widget_views',
    'widget_views_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('widget_views', test_widget_views)

test_editor_scale_validation = executable('test_editor_scale_validation',
    'editor_scale_validation_test.cpp',
    dependencies : [nk_dep, catch2_test_dep],
)
test('editor_scale_validation', test_editor_scale_validation)

# Public API link audit: references every public symbol declared in
# include/nk/**/*.h and fails the build if any symbol has no implementation
# in libNodalKit (Shortcut-class bugs). Intentionally headless — catches
# no runtime issues, only link-time gaps.
test_public_api_audit = executable('test_public_api_audit',
    'public_api_audit/main.cpp',
    dependencies : [nk_dep],
)
test('public_api_audit', test_public_api_audit)

if host_machine.system() == 'linux'
    test_wayland_portal_contract = executable('test_wayland_portal_contract',
        'wayland_portal_contract_test.cpp',
        '../src/platform/linux/wayland_portal_helpers.cpp',
        include_directories : include_directories('..'),
        dependencies : [nk_dep, catch2_test_dep],
    )
    test('wayland_portal_contract', test_wayland_portal_contract)
endif
