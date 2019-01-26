i18n = import('i18n')

add_project_arguments('-DGETTEXT_PACKAGE=' + gettext_package, language: 'c')
subdir('po')

i18n.merge_file(
  input: 'foo.desktop.in',
  output: 'foo.desktop',
  type: 'desktop',
  po_dir: 'po',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

