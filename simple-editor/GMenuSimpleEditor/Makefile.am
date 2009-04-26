QUIET_GEN = $(Q:@=@echo '  GEN   '$@;)

appdir     = $(pyexecdir)/GMenuSimpleEditor
app_PYTHON = 			\
	__init__.py		\
	main.py			\
	maindialog.py		\
	menutreemodel.py	\
	menufilewriter.py

nodist_app_PYTHON = config.py

CLEANFILES = config.py
EXTRA_DIST = config.py.in

all-local: config.py

config.py: config.py.in
	$(QUIET_GEN)sed								\
		-e s!\@DATADIR\@!$(datadir)!				\
		-e s!\@LOCALEDIR\@!$(prefix)/$(DATADIRNAME)/locale!	\
		-e s!\@PACKAGE_NAME\@!$(PACKAGE_NAME)!			\
		-e s!\@PACKAGE_VERSION\@!$(PACKAGE_VERSION)!		\
		< $< > $@

-include $(top_srcdir)/git.mk
