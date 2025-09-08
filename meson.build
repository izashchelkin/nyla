project('nyla', 'cpp', default_options: ['cpp_std=gnu++26'])

lib = library(
  'nyla',
  [
      'src/nyla_main.cpp',
      'src/event_loop.cpp',
      'src/keyboard.cpp',
      'src/spawn.cpp',

      'src/handlers/configure_request.cpp',
      'src/handlers/error.cpp',
      'src/handlers/key_press.cpp',
      'src/handlers/map_request.cpp',
  ],
  dependencies: [
    dependency('xcb', required: true),
    dependency('xcb-util', required: true),
    dependency('xkbcommon', required: true),
    dependency('xkbcommon-x11', required: true),
    dependency('xcb-xkb', required: true)
  ],
)
exe = executable('nyla', 'src/main.cpp', link_with: lib)

gtest = dependency('gtest', fallback: ['gtest', 'gtest_dep'])

test_exe = executable('tests',
  ['tests/tests.cpp'],
  dependencies: [gtest],
  link_with: lib
)
test('tests', test_exe)


