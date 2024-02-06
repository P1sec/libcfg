SUBDIRS = .

SOURCE_FILES = libcfgcli.c

AM_CPPFLAGS = -I$(top_srcdir) -Wall -Werror -Wshadow -Wduplicated-cond -Wunused-parameter -fstack-protector -Wimplicit-fallthrough=0

if ENABLE_COVERAGE
AM_CPPFLAGS += --coverage
AM_LDFLAGS = --coverage
endif

lib_LTLIBRARIES = libcfgcli.la

libcfgcli_la_SOURCES = $(SOURCE_FILES)
libcfgcli_la_CFLAGS = $(AM_CPPFLAGS)
libcfgcli_la_LDFLAGS = $(AM_LDFLAGS) -export-dynamic -version-info @LIBCFGCLI_SO_VERSION@

libcfgcliincludedir = $(includedir)
libcfgcliinclude_HEADERS = libcfgcli.h

clean-local: clean-gcno clean-gcda
	-rm -f *.orig

clean-gcno:
	-rm -f *.gcno
	-rm -f .libs/*.gcno

clean-gcda:
	-rm -f *.gcda
	-rm -f .libs/*.gcda

check-local: clean-gcda
