directorydir = $(datadir)/desktop-directories

directory_in_files =				\
	AudioVideo.directory.desktop.in			\
	Development.directory.desktop.in		\
	Education.directory.desktop.in			\
	Game.directory.desktop.in			\
	Graphics.directory.desktop.in			\
	Network.directory.desktop.in			\
	Office.directory.desktop.in			\
	System-Tools.directory.desktop.in		\
	Utility.directory.desktop.in			\
	Utility-Accessibility.directory.desktop.in	\
	X-GNOME-Other.directory.desktop.in		\
	X-GNOME-Menu-Applications.directory.desktop.in	\
	X-GNOME-Sundry.directory.desktop.in 		\
	X-GNOME-Utilities.directory.desktop.in 		\
	X-GNOME-WebApplications.directory.desktop.in 	\
	X-GNOME-SystemSettings.directory.desktop.in

directory_DATA = $(directory_in_files:.directory.desktop.in=.directory)

%.directory: %.directory.desktop.in
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -o $@-t \
		-d $(top_srcdir)/po && mv $@-t $@

EXTRA_DIST= $(directory_in_files)

DISTCLEANFILES = $(directory_DATA)

-include $(top_srcdir)/git.mk
