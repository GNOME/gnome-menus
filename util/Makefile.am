noinst_PROGRAMS = gnome-menu-spec-test

AM_CPPFLAGS =							\
	$(GLIB_CFLAGS)						\
	-I$(srcdir)/../libmenu					\
	-DGMENU_I_KNOW_THIS_IS_UNSTABLE				\
	-DGNOMELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\"	\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	$(DEBUG_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

gnome_menu_spec_test_SOURCES =		\
	test-menu-spec.c

gnome_menu_spec_test_LDADD =		\
	$(GLIB_LIBS)			\
	../libmenu/libgnome-menu.la

if HAVE_PYTHON
pyexampledir = $(pkgdatadir)/examples
pyexample_DATA = gnome-menus-ls.py
else
pyexampledir =
pyexample_DATA =
endif

EXTRA_DIST = $(pyexample_DATA)

-include $(top_srcdir)/git.mk
