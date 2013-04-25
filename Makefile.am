ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory

bin_PROGRAMS = \
	github-eventc

github_eventc_SOURCES = \
	src/main.c

github_eventc_CFLAGS = \
	$(AM_CFLAGS) \
	$(JSON_GLIB_CFLAGS) \
	$(LIBSOUP_CFLAGS) \
	$(LIBEVENTC_CFLAGS) \
	$(GLIB_CFLAGS)

github_eventc_LDADD = \
	$(JSON_GLIB_LIBS) \
	$(LIBSOUP_LIBS) \
	$(LIBEVENTC_LIBS) \
	$(GLIB_LIBS)
