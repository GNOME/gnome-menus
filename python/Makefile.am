NULL =

INCLUDES =				\
	-DGMENU_I_KNOW_THIS_IS_UNSTABLE	\
	-I$(srcdir)/../libmenu		\
	$(WARN_CFLAGS)			\
	$(NO_STRICT_ALIASING_CFLAGS)	\
	$(GLIB_CFLAGS)			\
	$(PYTHON_INCLUDES)		\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(DEBUG_CFLAGS)			\
	$(NULL)

gmenudir = $(pyexecdir)
gmenu_LTLIBRARIES = gmenu.la

gmenu_la_LDFLAGS = -module -avoid-version -fPIC -export-symbols-regex initgmenu
gmenu_la_LIBADD  = $(GLIB_LIBS) $(PYTHON_LIBS) $(top_builddir)/libmenu/libgnome-menu.la
gmenu_la_SOURCES = gmenu.c

-include $(top_srcdir)/git.mk
