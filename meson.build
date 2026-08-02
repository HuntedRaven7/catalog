project('catalog', 'cpp', 'c',
  version : '0.1.0',
  meson_version : '>= 0.61.0',
  default_options : [
    'cpp_std=c++17',
    'warning_level=2',
    'buildtype=debugoptimized',
  ])

adapta_dep = dependency('libadapta-1', version : '>= 1.5.0',
  fallback : ['libadapta', 'libadapta_dep'],
  default_options : ['introspection=disabled'])
json_glib_dep = dependency('json-glib-1.0')

gnome = import('gnome')
resources = gnome.compile_resources('catalog_resources',
  'src/catalog.gresource.xml',
  source_dir : 'images',
  c_name : 'catalog')

src = files(
  'src/main.cpp',
  'src/model.cpp',
  'src/univ.cpp',
  'src/app.cpp',
)

executable('catalog',
  src, resources,
  dependencies : [adapta_dep, json_glib_dep],
  install : true,
  install_rpath : '$ORIGIN/../' + get_option('libdir'),
)

install_data('io.univ.Catalog.desktop',
  install_dir : get_option('datadir') / 'applications')

foreach size : ['16', '24', '32', '48', '64', '128', '256', '512']
  install_data('images/icons/hicolor/' + size + 'x' + size + '/apps/catalog.png',
    install_dir : get_option('datadir') / 'icons' / 'hicolor' / (size + 'x' + size) / 'apps')
endforeach
