NULL = 

INCLUDES =				\
	-DMENU_I_KNOW_THIS_IS_UNSTABLE	\
	$(LIBMENU_CFLAGS)		\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(DEBUG_CFLAGS)			\
	$(WARN_CFLAGS)			\
	$(NULL)

lib_LTLIBRARIES = libgnome-menu.la

libgnome_menu_la_SOURCES =		\
	canonicalize.c			\
	canonicalize.h			\
	desktop-entries.c		\
	desktop-entries.h		\
	entry-directories.c		\
	entry-directories.h		\
	menu-layout.c			\
	menu-layout.h			\
	menu-tree.c			\
	menu-tree.h			\
	menu-util.c			\
	menu-util.h			\
	$(NULL)

libgnome_menu_la_LIBADD = 		\
	$(LIBMENU_LIBS)			\
	$(NULL)
libgnome_menu_la_LDFLAGS =		\
	-info $(LIB_MENU_LT_VERSION)	\
	-no-undefined			\
	$(NULL)

libgnome_menu_includedir = $(includedir)/gnome-menus

libgnome_menu_include_HEADERS =		\
	menu-tree.h			\
	$(NULL)

EXTRA_DIST =				\
        libgnome-menu.pc.in		\
        libgnome-menu-uninstalled.pc.in	\
	$(NULL)

pkgconfigdir   = $(libdir)/pkgconfig
pkgconfig_DATA = libgnome-menu.pc



