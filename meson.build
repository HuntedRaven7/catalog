project('catalog', 'cpp',
  version : '0.1.0',
  meson_version : '>= 0.61.0',
  default_options : [
    'cpp_std=c++17',
    'warning_level=2',
    'buildtype=debugoptimized',
  ])

adapta_dep = dependency('libadapta-1', version : '>= 1.5.0',
  fallback : ['libadapta', 'libadapta_dep'])
json_glib_dep = dependency('json-glib-1.0')

src = files(
  'src/main.cpp',
  'src/model.cpp',
  'src/univ.cpp',
  'src/app.cpp',
)

executable('catalog',
  src,
  dependencies : [adapta_dep, json_glib_dep],
  install : true,
)
