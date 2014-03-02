
check_PROGRAMS = \
	tests/files.test

TESTS = \
	tests/files.test

tests_files_test_SOURCES = \
	tests/files.c \
	src/libgit-eventc.h

tests_files_test_CFLAGS = \
	$(AM_CFLAGS) \
	$(GLIB_CFLAGS)

tests_files_test_LDADD = \
	$(libgit_eventc_a_LIBS) \
	$(GLIB_LIBS)
