ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory

bin_PROGRAMS =
libexec_PROGRAMS =
noinst_LIBRARIES = \
	libgit-eventc.a

libgit_eventc_a_SOURCES = \
	src/libgit-eventc.h \
	src/libgit-eventc.c

libgit_eventc_a_CFLAGS = \
	$(AM_CFLAGS) \
	$(LIBSOUP_CFLAGS) \
	$(LIBEVENTC_CFLAGS) \
	$(GLIB_CFLAGS)

libgit_eventc_a_LIBS = \
	libgit-eventc.a \
	$(LIBSOUP_LIBS) \
	$(LIBEVENTC_LIBS) \
	$(GLIB_LIBS)

if ENABLE_HOOK
libexec_PROGRAMS += git-eventc-post-receive
endif

git_eventc_post_receive_SOURCES = \
	src/libgit-eventc.h \
	src/git-eventc-post-receive.c

git_eventc_post_receive_CFLAGS = \
	$(AM_CFLAGS) \
	$(LIBGIT2_CFLAGS) \
	$(LIBEVENTC_CFLAGS) \
	$(GLIB_CFLAGS)

git_eventc_post_receive_LDADD = \
	$(libgit_eventc_a_LIBS) \
	$(LIBGIT2_LIBS) \
	$(LIBEVENTC_LIBS) \
	$(GLIB_LIBS)

if ENABLE_WEBHOOK
bin_PROGRAMS += git-eventc-webhook
endif

git_eventc_webhook_SOURCES = \
	src/libgit-eventc.h \
	src/git-eventc-webhook.c

git_eventc_webhook_CFLAGS = \
	$(AM_CFLAGS) \
	$(JSON_GLIB_CFLAGS) \
	$(LIBSOUP_CFLAGS) \
	$(LIBEVENTC_CFLAGS) \
	$(GLIB_CFLAGS)

git_eventc_webhook_LDADD = \
	$(libgit_eventc_a_LIBS) \
	$(JSON_GLIB_LIBS) \
	$(LIBSOUP_LIBS) \
	$(LIBEVENTC_LIBS) \
	$(GLIB_LIBS)
