ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory

bin_PROGRAMS = \
	git-eventc-webhook

git_eventc_webhook_SOURCES = \
	src/main.c

git_eventc_webhook_CFLAGS = \
	$(AM_CFLAGS) \
	$(JSON_GLIB_CFLAGS) \
	$(LIBSOUP_CFLAGS) \
	$(LIBEVENTC_CFLAGS) \
	$(GLIB_CFLAGS)

git_eventc_webhook_LDADD = \
	$(JSON_GLIB_LIBS) \
	$(LIBSOUP_LIBS) \
	$(LIBEVENTC_LIBS) \
	$(GLIB_LIBS)
