NULL = 

@INTLTOOL_DIRECTORY_RULE@
directorydir = $(datadir)/desktop-directories
directory_in_files =				\
	Accessibility.directory.in		\
	Accessories.directory.in		\
	Applications.directory.in		\
	Development.directory.in		\
	Desktop.directory.in			\
	Education.directory.in			\
	Games.directory.in			\
	Graphics.directory.in			\
	Hardware.directory.in			\
	Internet.directory.in			\
	InternetAndNetwork.directory.in		\
	LookAndFeel.directory.in		\	
	Multimedia.directory.in			\
	Office.directory.in			\
	Other.directory.in			\
	Personal.directory.in			\
	Preferences.directory.in		\
	Settings-Accessibility.directory.in	\
	Settings.directory.in			\
	System.directory.in			\
	System-Settings.directory.in		\
	System-Tools.directory.in		\
	$(NULL)

directory_DATA = $(directory_in_files:.directory.in=.directory)

EXTRA_DIST= $(directory_in_files)

DISTCLEANFILES = $(directory_DATA)
