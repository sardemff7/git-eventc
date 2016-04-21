AC_PREREQ([2.65])
AC_INIT([git-eventc],[0.1],[],[])

AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_SRCDIR([src/libgit-eventc.c])
AC_CONFIG_AUX_DIR([.aux])

AM_INIT_AUTOMAKE([1.11 foreign subdir-objects parallel-tests dist-xz no-dist-gzip tar-ustar])

AM_SILENT_RULES([yes])

AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AM_PROG_AR
AC_PROG_RANLIB
AC_PROG_SED
PKG_PROG_PKG_CONFIG

AC_PROG_INSTALL

glib_min_major="2"
glib_min_minor="42"
glib_min_version="${glib_min_major}.${glib_min_minor}"
gio_min_version="$glib_min_version"
libeventc_min_version="0.6"
libsoup_min_version="2.48"
libgit2_min_version="0.23.0"


AM_CFLAGS="-fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"
AM_CPPFLAGS=""
AM_LDFLAGS="-Wl,--no-undefined"


if test -d .git; then
    PACKAGE_VERSION="${PACKAGE_VERSION} - `git describe --tags --always` (`git describe --tags --always --all | sed -e 's:heads/::'`)"
    AC_DEFINE_UNQUOTED([PACKAGE_VERSION], ["$PACKAGE_VERSION"], [Full version with git information])
fi

AC_CHECK_HEADERS([string.h stdio.h])

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= ${glib_min_version} gobject-2.0 gio-2.0])
AC_DEFINE_UNQUOTED([GLIB_VERSION_MIN_REQUIRED], [(G_ENCODE_VERSION(${glib_min_major},${glib_min_minor}))], [The lower GLib version supported])

PKG_CHECK_MODULES(LIBEVENTC, [libeventc >= ${libeventc_min_version} libeventd])

PKG_CHECK_MODULES(LIBSOUP, [libsoup-2.4 >= ${libsoup_min_version}])

AC_ARG_ENABLE(hook, AS_HELP_STRING([--disable-hook], [Disable post-receive hook]), [], enable_hook=yes)
if test x$enable_hook = xyes; then
    PKG_CHECK_MODULES(LIBGIT2, [libgit2 >= ${libgit2_min_version}])
fi
AM_CONDITIONAL(ENABLE_HOOK, test x$enable_hook = xyes)

AC_ARG_ENABLE(webhook, AS_HELP_STRING([--disable-webhook], [Disable WebHook handler]), [], enable_webhook=yes)
if test x$enable_webhook = xyes; then
    PKG_CHECK_MODULES(JSON_GLIB, [json-glib-1.0])
fi
AM_CONDITIONAL(ENABLE_WEBHOOK, test x$enable_webhook = xyes)

if test x$enable_hook = xno -a x$enable_webhook = xno; then
    AC_MSG_ERROR([All features disabled])
fi

AC_ARG_ENABLE([systemd], AS_HELP_STRING([--enable-systemd], [Enable systemd activation support]), [], [enable_systemd=no])
AC_ARG_WITH([systemdsystemunitdir], AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd system unit files]), [], [with_systemdsystemunitdir=yes])
if test x${enable_systemd} = xyes; then
    if test x$enable_webhook = xno; then
        AC_MSG_ERROR([systemd support is requested but Webhook is disabled])
    fi
    case "${with_systemdsystemunitdir}" in
        no|"") AC_MSG_ERROR([*** You must define systemdsystemunitdir when compiling systemd support]) ;;
        yes) systemdsystemunitdir="`${PKG_CONFIG} --variable=systemdsystemunitdir systemd`" ;;
        *) systemdsystemunitdir="${with_systemdsystemunitdir}" ;;
    esac
    AC_CHECK_HEADERS([sys/socket.h])
    PKG_CHECK_MODULES([SYSTEMD], [libsystemd >= 209],, [PKG_CHECK_MODULES([SYSTEMD], [libsystemd-daemon])])
    AC_DEFINE([ENABLE_SYSTEMD], [1], [Enable systemd activation support])
    AC_SUBST([systemdsystemunitdir])
fi
AM_CONDITIONAL([ENABLE_SYSTEMD], [test x${enable_systemd} = xyes])

# Debug flags
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [Enable debug]), [], enable_debug=no)
if test x$enable_debug = xyes; then
    AC_DEFINE([GIT_EVENTC_DEBUG], [1], [Activate debug mode])
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

    post-receive hook: $enable_hook
    WebHook handler: $enable_webhook
        systemd support: ${enable_systemd}
])
