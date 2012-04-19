# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)

m4_include([version.m4])

AC_INIT([subconvert],[VERSION_NUMBER],[johnw@boostpro.com])

AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE([dist-bzip2 foreign])
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADER([config.h])

# Use AM_SILENT_RULES if present
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CXX
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL
#AM_GNU_GETTEXT
#AM_GNU_GETTEXT_VERSION([0.17])

# Check for options
AC_ARG_ENABLE(debug,
  [  --enable-debug          Turn on debugging],
  [case "${enableval}" in
    yes) debug=true ;;
    no)  debug=false ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
  esac],[debug=false])

if [ test x$debug = xtrue ]; then
  AC_DEFINE([DEBUG_MODE], [1], [Whether debugging is enabled])
fi
AM_CONDITIONAL(DEBUG, test x$debug = xtrue)

AC_ARG_ENABLE(pch,
  [  --enable-pch            Use GCC 4.x pre-compiled headers],
  [case "${enableval}" in
    yes) pch=true ;;
    no)  pch=false ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-pch) ;;
  esac],[pch=false])

if [ test x$pch = xtrue ]; then
  AC_DEFINE([USE_PCH], [1], [Whether pre-compiled headers are being used])
fi
AM_CONDITIONAL(USE_PCH, test x$pch = xtrue)

AC_ARG_ENABLE(doxygen,
  [  --enable-doxygen        Turns on generation of code documentation],
  [case "${enableval}" in
    yes) doxygen=true ;;
    no)  doxygen=false ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-doxygen) ;;
  esac],[doxygen=false])

AM_CONDITIONAL(USE_DOXYGEN, test x$doxygen = xtrue)

AC_ARG_WITH(boost-suffix,
  [  --with-boost-suffix=X   Append X to the Boost library names],
  [BOOST_SUFFIX="${withval}"],
  [BOOST_SUFFIX=""])

AC_SUBST([BOOST_SUFFIX], $BOOST_SUFFIX)

# check for boost_filesystem
AC_CACHE_CHECK(
  [if boost_filesystem is available],
  [boost_filesystem_cpplib_avail_cv_],
  [boost_filesystem_save_libs=$LIBS
   LIBS="-lboost_filesystem$BOOST_SUFFIX -lboost_system$BOOST_SUFFIX $LIBS"
   AC_LANG_PUSH(C++)
   AC_LINK_IFELSE(
     [AC_LANG_PROGRAM(
        [[#include <boost/filesystem/path.hpp>]],
        [[boost::filesystem::path this_path("Hello");]])],
     [boost_filesystem_cpplib_avail_cv_=true],
     [boost_filesystem_cpplib_avail_cv_=false])
   AC_LANG_POP
   LIBS=$boost_filesystem_save_libs])

if [test x$boost_filesystem_cpplib_avail_cv_ = xtrue ]; then
  LIBS="-lboost_filesystem$BOOST_SUFFIX -lboost_system$BOOST_SUFFIX $LIBS"
else
  AC_MSG_FAILURE("Could not find boost_filesystem library (set CPPFLAGS and LDFLAGS?)")
fi

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_STAT
AC_CHECK_HEADERS([openssl/md5.h openssl/sha.h])

# Checks for libraries.
AC_CHECK_LIB(crypto, MD5)
AC_CHECK_LIB(crypto, SHA1)
AC_CHECK_LIB(git2, git_oid_fmt)

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
#AC_STRUCT_TM

# Checks for library functions.
#AC_FUNC_MKTIME
#AC_FUNC_STAT
#AC_FUNC_STRFTIME
#AC_CHECK_FUNCS([access isatty])

# Pepare the Makefiles
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
