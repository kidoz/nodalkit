catch2_test_dep = dependency('catch2-with-main', required : false)
if not catch2_test_dep.found()
    catch2_proj = subproject('catch2', default_options : ['tests=false'])
    catch2_test_dep = catch2_proj.get_variable('catch2_with_main_dep')
endif

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
