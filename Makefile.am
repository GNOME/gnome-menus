NULL =

if HAVE_PYTHON
PYTHON_SUBDIR=python
endif

ALL_SUBDIRS = 			\
	po 			\
	libmenu			\
	layout			\
	desktop-directories	\
	util			\
	$(NULL)

SUBDIRS = $(ALL_SUBDIRS) $(PYTHON_SUBDIR)
DIST_SUBDIRS = $(ALL_SUBDIRS) python

EXTRA_DIST = 			\
	HACKING			\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	$(NULL)

DISTCLEANFILES =		\
        intltool-extract	\
        intltool-merge		\
        intltool-update		\
	$(NULL)
