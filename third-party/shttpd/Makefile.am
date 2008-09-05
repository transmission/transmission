
noinst_LIBRARIES = libshttpd.a

AM_CPPFLAGS = -DEMBEDDED -DNDEBUG -DNO_CGI -DNO_SSI

libshttpd_a_SOURCES = \
    string.c shttpd.c log.c auth.c md5.c \
    io_file.c io_socket.c io_ssl.c io_emb.c io_dir.c \
    compat_unix.c compat_win32.c

noinst_HEADERS = \
    defs.h llist.h shttpd.h std_includes.h io.h md5.h ssl.h \
    compat_unix.h compat_rtems.h config.h

extra_DIST = \
    README \
    LICENSE

# Possible flags: (in brackets are rough numbers for 'gcc -O2' on i386)
# -DHAVE_MD5		- use system md5 library (-2kb)
# -DNDEBUG		- strip off all debug code (-5kb)
# -D_DEBUG		- build debug version (very noisy) (+6kb)
# -DNO_CGI		- disable CGI support (-5kb)
# -DNO_SSL		- disable SSL functionality (-2kb)
# -DNO_AUTH		- disable authorization support (-4kb)
# -DCONFIG=\"file\"	- use `file' as the default config file
# -DNO_SSI		- disable SSI support (-4kb)

