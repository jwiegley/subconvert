dist_man_MANS	= doc/subconvert.1
EXTRA_DIST	= autogen.sh
DISTCLEANFILES	= .timestamp

pkginclude_HEADERS = src/svndump.h src/gitutil.h

######################################################################

bin_PROGRAMS	= subconvert

subconvert_CXXFLAGS = -ansi -pedantic -pedantic-errors -Wall -Winvalid-pch	\
		      -Wextra -Wcast-align -Wcast-qual -Wfloat-equal		\
		      -Wmissing-field-initializers -Wno-endif-labels		\
		      -Woverloaded-virtual -Wsign-compare -Wsign-promo		\
		      -Wwrite-strings -Wno-old-style-cast -Wno-deprecated	\
	              -Wno-strict-aliasing -Werror
subconvert_SOURCES  = src/subconvert.cpp src/svndump.cpp src/gitutil.cpp

# Makefile.am ends here
