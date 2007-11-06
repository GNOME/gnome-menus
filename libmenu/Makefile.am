NULL = 

# only one will be defined
if HAVE_GIO
MONITOR_CFLAGS = $(GIO_CFLAGS)
MONITOR_LIBS = $(GIO_LIBS)
MONITOR_SOURCES = menu-monitor-gio.c
endif

if HAVE_FAM
MONITOR_CFLAGS =
MONITOR_LIBS = $(FAM_LIBS)
MONITOR_SOURCES = menu-monitor-fam.c
endif

if HAVE_INOTIFY
MONITOR_CFLAGS =
MONITOR_LIBS =
MONITOR_SOURCES = menu-monitor-inotify.c
endif

if HAVE_NO_MONITOR_BACKEND
MONITOR_CFLAGS =
MONITOR_LIBS =
MONITOR_SOURCES = menu-monitor-none.c
endif

INCLUDES =				\
	-DGMENU_I_KNOW_THIS_IS_UNSTABLE	\
	$(GLIB_CFLAGS)			\
	$(MONITOR_CFLAGS)		\
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
	gmenu-tree.c			\
	gmenu-tree.h			\
	menu-layout.c			\
	menu-layout.h			\
	menu-monitor.c			\
	menu-monitor.h			\
	menu-monitor-backend.h		\
	menu-util.c			\
	menu-util.h			\
	$(MONITOR_SOURCES)		\
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
	menu-monitor-gio.c		\
	menu-monitor-fam.c		\
	menu-monitor-inotify.c		\
	menu-monitor-none.c		\
        libgnome-menu.pc.in		\
        libgnome-menu-uninstalled.pc.in	\
	$(NULL)

pkgconfigdir   = $(libdir)/pkgconfig
pkgconfig_DATA = libgnome-menu.pc
