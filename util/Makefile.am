NULL =

INCLUDES =				\
	-DMENU_I_KNOW_THIS_IS_UNSTABLE	\
	-I$(srcdir)/../libmenu		\
	$(LIBMENU_CFLAGS)		\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(DEBUG_CFLAGS)			\
	$(WARN_CFLAGS)			\
	$(NULL)

bin_PROGRAMS =				\
	gnome-menu-spec-test		\
	$(NULL)

gnome_menu_spec_test_SOURCES =		\
	test-menu-spec.c		\
	$(NULL)

gnome_menu_spec_test_LDADD =		\
	../libmenu/libgnome-menu.la	\
	$(LIBMENU_LIBS)			\
	$(NULL)
