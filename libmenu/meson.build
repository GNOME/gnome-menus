as_version = meson.project_version()
ver_arr = as_version.split('.')

as_major_version = ver_arr[0]
as_minor_version = ver_arr[1]
as_micro_version = ver_arr[2]

prj_name = meson.project_name()

project(prj_name, 'c', version : '3.31.3')


conf = configuration_data()
conf.set_quoted('VERSION', as_version)
configure_file(output : 'config.h',
               configuration : conf)

gio_unix = dependency('gio-unix-2.0', version : '>= 2.29.15')

if get_option('enable-dep11')
  yaml = dependency('yaml-0.1')
  conf.set('AS_BUILD_DEP11', 1)
else
  yaml = dependency('yaml-0.1', required : false)
endif

