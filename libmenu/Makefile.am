NULL = 

INCLUDES =				\
	-DGMENU_I_KNOW_THIS_IS_UNSTABLE	\
	$(GLIB_CFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(DEBUG_CFLAGS)			\
	$(WARN_CFLAGS)			\
	$(NULL)

lib_LTLIBRARIES = libgnome-menu.la

libgnome_menu_sources =			\
	canonicalize.c			\
	desktop-entries.c		\
	entry-directories.c		\
	gmenu-tree.c			\
	menu-layout.c			\
	menu-monitor.c			\
	menu-util.c			\
	$(NULL)

libgnome_menu_la_SOURCES =		\
	$(libgnome_menu_sources)	\
	canonicalize.h			\
	desktop-entries.h		\
	entry-directories.h		\
	gmenu-tree.h			\
	menu-layout.h			\
	menu-monitor.h			\
	menu-util.h			\
	$(NULL)

libgnome_menu_la_LIBADD = 		\
	$(GLIB_LIBS)			\
	$(MONITOR_LIBS)			\
	$(NULL)
libgnome_menu_la_LDFLAGS =			\
	-version-info $(LIB_MENU_LT_VERSION)	\
	-no-undefined				\
	-export-symbols-regex gmenu_tree	\
	$(NULL)

libgnome_menu_includedir = $(includedir)/gnome-menus

libgnome_menu_include_HEADERS =		\
	gmenu-tree.h			\
	$(NULL)

EXTRA_DIST =				\
        libgnome-menu.pc.in		\
        libgnome-menu-uninstalled.pc.in	\
	$(NULL)

CLEANFILES =				\
	$(NULL)

pkgconfigdir   = $(libdir)/pkgconfig
pkgconfig_DATA = libgnome-menu.pc

# Introspection
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

if HAVE_INTROSPECTION
# Note: we only include the headers here so far because there's no gtk-doc at all anyway
introspection_sources = $(libgnome_menu_include_HEADERS)

GMenu-2.0.gir: libgnome-menu.la Makefile
GMenu_2_0_gir_INCLUDES = GObject-2.0
GMenu_2_0_gir_CFLAGS =			\
	-DGMENU_I_KNOW_THIS_IS_UNSTABLE	\
	$(GLIB_CFLAGS)
GMenu_2_0_gir_LIBS = libgnome-menu.la
GMenu_2_0_gir_FILES = $(addprefix $(srcdir)/,$(introspection_sources))
INTROSPECTION_GIRS += GMenu-2.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif

-include $(top_srcdir)/git.mk
