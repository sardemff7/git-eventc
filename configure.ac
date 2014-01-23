AC_PREREQ([2.65])
AC_INIT([git-eventc],[0.1],[],[])

AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([.aux])

AM_INIT_AUTOMAKE([1.11 foreign subdir-objects parallel-tests dist-xz no-dist-gzip tar-ustar])

AM_SILENT_RULES([yes])

AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
PKG_PROG_PKG_CONFIG

AC_PROG_INSTALL

glib_min_major="2"
glib_min_minor="28"
glib_min_version="${glib_min_major}.${glib_min_minor}"
gio_min_version="$glib_min_version"


AM_CFLAGS="-fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"
AM_CPPFLAGS=""
AM_LDFLAGS="-Wl,--no-undefined"


if test -d .git; then
    PACKAGE_VERSION="${PACKAGE_VERSION} - `git describe --tags --always` (`git describe --tags --always --all | sed -e 's:heads/::'`)"
    AC_DEFINE_UNQUOTED([PACKAGE_VERSION], ["$PACKAGE_VERSION"], [Full version with git information])
fi

AC_CHECK_HEADERS([string.h])

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= $glib_min_version gobject-2.0])
AC_DEFINE_UNQUOTED([GLIB_VERSION_MIN_REQUIRED], [(G_ENCODE_VERSION(${glib_min_major},${glib_min_minor}))], [The lower GLib version supported])

PKG_CHECK_MODULES(LIBEVENTC, [libeventc])

PKG_CHECK_MODULES(LIBSOUP, [libsoup-2.4])

PKG_CHECK_MODULES(JSON_GLIB, [json-glib-1.0])


# Debug flags
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [Enable debug]), [], enable_debug=no)
if test x$enable_debug = xyes; then
    AC_DEFINE([DEBUG], [1], [Activate debug mode])
fi

AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_CPPFLAGS)
AC_SUBST(AM_LDFLAGS)

AC_CONFIG_FILES([
    Makefile
])
AC_OUTPUT

AC_MSG_RESULT([
    ${PACKAGE_NAME} ${PACKAGE_VERSION}
])
