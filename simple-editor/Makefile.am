SUBDIRS = GMenuSimpleEditor

bin_SCRIPTS = gmenu-simple-editor

gmenu-simple-editor: gmenu-simple-editor.in
	sed -e s!\@pyexecdir\@!@pyexecdir@! < $< > $@
	chmod a+x $@

gladedir   = $(pkgdatadir)/glade
glade_DATA = gmenu-simple-editor.glade

EXTRA_DIST =				\
	gmenu-simple-editor.in		\
	gmenu-simple-editor.glade

CLEANFILES = gmenu-simple-editor
