SUBDIRS = GMenuSimpleEditor

QUIET_GEN = $(Q:@=@echo '  GEN   '$@;)

bin_SCRIPTS = gmenu-simple-editor

gmenu-simple-editor: gmenu-simple-editor.in
	$(QUIET_GEN)sed -e s!\@pyexecdir\@!@pyexecdir@! -e s!\@PYTHON\@!@PYTHON@! < $< > $@ && \
	chmod a+x $@

uidir   = $(pkgdatadir)/ui
ui_DATA = gmenu-simple-editor.ui

desktopdir = $(datadir)/applications
desktop_in_files = gmenu-simple-editor.desktop.in 
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST =				\
	gmenu-simple-editor.in		\
	gmenu-simple-editor.desktop.in	\
	gmenu-simple-editor.ui

CLEANFILES =				\
	gmenu-simple-editor		\
	gmenu-simple-editor.desktop

-include $(top_srcdir)/git.mk
