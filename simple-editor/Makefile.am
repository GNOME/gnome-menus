SUBDIRS = GMenuSimpleEditor

bin_SCRIPTS = gmenu-simple-editor

gmenu-simple-editor: gmenu-simple-editor.in
	sed -e s!\@pyexecdir\@!@pyexecdir@! -e s!\@PYTHON\@!@PYTHON@! < $< > $@
	chmod a+x $@

gladedir   = $(pkgdatadir)/glade
glade_DATA = gmenu-simple-editor.glade

desktopdir = $(datadir)/applications
desktop_in_files = gmenu-simple-editor.desktop.in 
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST =				\
	gmenu-simple-editor.in		\
	gmenu-simple-editor.desktop.in	\
	gmenu-simple-editor.glade

CLEANFILES =				\
	gmenu-simple-editor		\
	gmenu-simple-editor.desktop
