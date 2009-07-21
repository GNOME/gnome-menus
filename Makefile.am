NULL =

if HAVE_PYTHON
PYTHON_SUBDIRS = python simple-editor
endif

ALL_SUBDIRS = 			\
	po 			\
	libmenu			\
	layout			\
	desktop-directories	\
	util			\
	$(NULL)

SUBDIRS = $(ALL_SUBDIRS) $(PYTHON_SUBDIRS)
DIST_SUBDIRS = $(ALL_SUBDIRS) python simple-editor

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = 			\
	HACKING			\
	$(NULL)

MAINTAINERCLEANFILES = \
	$(srcdir)/INSTALL \
	$(srcdir)/aclocal.m4 \
	$(srcdir)/config.guess \
	$(srcdir)/config.h.in \
	$(srcdir)/config.sub \
	$(srcdir)/depcomp \
	$(srcdir)/install-sh \
	$(srcdir)/ltmain.sh \
	$(srcdir)/missing \
	$(srcdir)/mkinstalldirs \
	$(srcdir)/py-compile \
	`find "$(srcdir)" -type f -name Makefile.in -print` \
	$(srcdir)/configure \
	$(srcdir)/m4/intltool.m4

-include $(top_srcdir)/git.mk
