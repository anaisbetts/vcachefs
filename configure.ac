AC_PREREQ(2.51)

AC_INIT(README)
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE(vcachefs, 0.1)

AM_PROG_LIBTOOL

AC_ISC_POSIX
AC_PROG_CC
AC_C_INLINE
AC_STDC_HEADERS
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

AC_SUBST(ACLOCAL_AMFLAGS, "$ACLOCAL_FLAGS")
AC_CONFIG_SRCDIR(src)
AC_CONFIG_HEADERS(config.h)

AM_MAINTAINER_MODE

dnl -------------- Add the debug flag ----------------------
AC_ARG_ENABLE([debug],
        AC_HELP_STRING([--enable-debug], [Enable Debug Mode (default disabled)]),
	enable_debug=$enableval,
	enable_debug=no)
ENABLE_DEBUG=$enable_debug
if test "x$enable_debug" = "xno"; then
	DEBUG_CFLAGS=""
else
	DEBUG_CFLAGS="-Wall -g"
fi
AC_SUBST(DEBUG_CFLAGS)
AC_SUBST(ENABLE_DEBUG)

dnl pkg-config
AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
if test "x$PKG_CONFIG" = "xno"; then
	AC_MSG_ERROR([You need to install pkg-config])
fi

GLIB_REQUIRED=2.9.0

PKG_CHECK_MODULES(VCACHEFS, glib-2.0 >= $GLIB_REQUIRED 
		  fuse )

AC_SUBST(VCACHEFS_CFLAGS)
AC_SUBST(VCACHEFS_LIBS)

AC_OUTPUT([
Makefile
src/Makefile
])

