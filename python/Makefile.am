NULL =

INCLUDES =				\
	-DMENU_I_KNOW_THIS_IS_UNSTABLE	\
	-I$(srcdir)/../libmenu		\
	$(LIBMENU_CFLAGS)		\
	$(PYTHON_INCLUDES)		\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(DEBUG_CFLAGS)			\
	$(NULL)

gmenudir = $(pythondir)
gmenu_LTLIBRARIES = gmenu.la

gmenu_la_LDFLAGS = -module -avoid-version -fPIC -export-symbols-regex initgmenu
gmenu_la_LIBADD  = $(top_builddir)/libmenu/libgnome-menu.la
nodist_gmenu_la_SOURCES = gmenu.c

gmenu.c: gmenu.pyx
	pyrexc gmenu.pyx

EXTRA_DIST = gmenu.pyx
CLEANFILES = gmenu.c
