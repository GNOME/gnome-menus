configure_file(
  input: prj_name + '.in',
  output: '@BASENAME@',
  configuration: prj_conf,
  install: true,
  install_dir: prj_bindir,
)

subdir('gnome-menus')

if get_option('tests')
  subdir('tests')
endif

