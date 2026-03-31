project('nodalkit-install-smoke', 'cpp',
    default_options : ['warning_level=3', 'werror=true', 'cpp_std=c++23'])

nodalkit_dep = dependency('nodalkit', method : 'pkg-config')

install_smoke = executable('install_smoke',
    'main.cpp',
    dependencies : nodalkit_dep,
)

test('install_smoke', install_smoke)
