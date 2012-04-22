dist_man_MANS	= doc/subconvert.1
EXTRA_DIST	= autogen.sh src/system.hpp.in
DISTCLEANFILES	= .timestamp

######################################################################

bin_PROGRAMS	= subconvert

subconvert_CXXFLAGS = -Weverything -Wno-padded -Wno-weak-vtables		\
		      -Wno-exit-time-destructors -Wno-global-constructors	\
		      -Wno-missing-prototypes -Wno-missing-noreturn             \
		      -Wno-disabled-macro-expansion -fno-limit-debug-info       \
		      -Wno-switch-enum -Wno-header-hygiene -Wno-c++98-compat    \
		      -std=c++11
#subconvert_CXXFLAGS = -ansi -pedantic -pedantic-errors -Wall -Winvalid-pch	\
#		      -Wextra -Wcast-align -Wcast-qual -Wfloat-equal		\
#		      -Wmissing-field-initializers -Wno-endif-labels		\
#		      -Woverloaded-virtual -Wsign-compare -Wsign-promo		\
#		      -Wwrite-strings -Wno-old-style-cast -Wno-deprecated	\
#	              -Wno-strict-aliasing -Werror

subconvert_SOURCES  = src/svndump.cpp		\
		      src/gitutil.cpp		\
		      src/authors.cpp		\
		      src/branches.cpp		\
		      src/converter.cpp		\
		      src/submodule.cpp		\
		      src/main.cpp

pkginclude_HEADERS = src/svndump.h		\
		     src/gitutil.h		\
		     src/authors.h		\
		     src/branches.h		\
		     src/converter.h		\
		     src/submodule.h		\
		     src/status.h

nodist_subconvert_SOURCES = system.hpp

BUILT_SOURCES = system.hpp
CLEANFILES    = system.hpp

system.hpp: src/system.hpp.in
	cp -p $< $@

if USE_PCH
nodist_subconvert_SOURCES += system.hpp.gch

BUILT_SOURCES += system.hpp.gch
CLEANFILES    += system.hpp.gch

system.hpp.gch: system.hpp
	-rm -f $@
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	    $(lib_cppflags) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
	    -o $@ $<
endif

# Makefile.am ends here
