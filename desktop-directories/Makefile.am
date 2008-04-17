NULL = 

@INTLTOOL_DIRECTORY_RULE@
directorydir = $(datadir)/desktop-directories
# Should be moved to gnome-control-center:
directory_in_controlcenterfiles =		\
	Hardware.directory.in			\
	InternetAndNetwork.directory.in		\
	LookAndFeel.directory.in		\
	Personal.directory.in			\
	System.directory.in			\
	$(NULL)
directory_in_files =				\
	AudioVideo.directory.in			\
	Development.directory.in		\
	Education.directory.in			\
	Game.directory.in			\
	Graphics.directory.in			\
	Network.directory.in			\
	Office.directory.in			\
	Settings.directory.in			\
	Settings-Accessibility.directory.in	\
	Settings-System.directory.in		\
	System-Tools.directory.in		\
	Utility.directory.in			\
	Utility-Accessibility.directory.in	\
	X-GNOME-Other.directory.in		\
	X-GNOME-Menu-Applications.directory.in	\
	X-GNOME-Menu-System.directory.in	\
	$(directory_in_controlcenterfiles)	\
	$(NULL)

directory_DATA = $(directory_in_files:.directory.in=.directory)

EXTRA_DIST= $(directory_in_files)

DISTCLEANFILES = $(directory_DATA)
