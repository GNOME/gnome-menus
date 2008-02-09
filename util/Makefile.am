NULL =

INCLUDES =							\
	-DGMENU_I_KNOW_THIS_IS_UNSTABLE				\
	-DGNOMELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\"	\
	-I$(srcdir)/../libmenu					\
	$(GLIB_CFLAGS)						\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	$(DEBUG_CFLAGS)						\
	$(WARN_CFLAGS)						\
	$(NULL)

noinst_PROGRAMS =			\
	gnome-menu-spec-test		\
	$(NULL)

gnome_menu_spec_test_SOURCES =		\
	test-menu-spec.c		\
	$(NULL)

gnome_menu_spec_test_LDADD =		\
	../libmenu/libgnome-menu.la	\
	$(GLIB_LIBS)			\
	$(NULL)
