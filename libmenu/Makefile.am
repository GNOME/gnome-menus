lib_LTLIBRARIES = libgnome-menu.la

AM_CPPFLAGS =				\
	$(GLIB_CFLAGS)			\
	-DGMENU_I_KNOW_THIS_IS_UNSTABLE	\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(DEBUG_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

libgnome_menu_includedir = $(includedir)/gnome-menus
libgnome_menu_include_HEADERS =		\
	gmenu-tree.h

libgnome_menu_sources =			\
	canonicalize.c			\
	desktop-entries.c		\
	entry-directories.c		\
	gmenu-tree.c			\
	menu-layout.c			\
	menu-monitor.c			\
	menu-util.c

libgnome_menu_la_SOURCES =		\
	$(libgnome_menu_sources)	\
	canonicalize.h			\
	desktop-entries.h		\
	entry-directories.h		\
	gmenu-tree.h			\
	menu-layout.h			\
	menu-monitor.h			\
	menu-util.h

libgnome_menu_la_LIBADD = 		\
	$(GLIB_LIBS)

libgnome_menu_la_LDFLAGS =			\
	-version-info $(LIB_MENU_LT_VERSION)	\
	-no-undefined				\
	-export-symbols-regex gmenu_tree

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libgnome-menu.pc

EXTRA_DIST =				\
	libgnome-menu.pc.in		\
	libgnome-menu-uninstalled.pc.in

CLEANFILES =

# Introspection
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --warn-all --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

if HAVE_INTROSPECTION
# Note: we only include the headers here so far because there's no gtk-doc at all anyway
introspection_sources = $(libgnome_menu_include_HEADERS)

GMenu-2.0.gir: libgnome-menu.la
GMenu_2_0_gir_INCLUDES = GObject-2.0
GMenu_2_0_gir_CFLAGS = $(AM_CPPFLAGS)
GMenu_2_0_gir_LIBS = libgnome-menu.la
GMenu_2_0_gir_SCANNERFLAGS = --pkg-export=libgnome-menu
GMenu_2_0_gir_FILES = $(addprefix $(srcdir)/,$(introspection_sources))
INTROSPECTION_GIRS += GMenu-2.0.gir

girdir = $(INTROSPECTION_GIRDIR)
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(INTROSPECTION_TYPELIBDIR)
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif

-include $(top_srcdir)/git.mk
