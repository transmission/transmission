SRCS=		string.c shttpd.c log.c auth.c md5.c cgi.c config.c io_ssi.c \
		io_file.c io_socket.c io_ssl.c io_emb.c io_dir.c io_cgi.c
HDRS=		defs.h llist.h shttpd.h std_includes.h io.h md5.h ssl.h \
		compat_unix.h compat_win32.h compat_rtems.h config.h
OBJS=		$(SRCS:%.c=%.o)
PROG=		shttpd

# Possible flags: (in brackets are rough numbers for 'gcc -O2' on i386)
# -DHAVE_MD5		- use system md5 library (-2kb)
# -DNDEBUG		- strip off all debug code (-5kb)
# -D_DEBUG		- build debug version (very noisy) (+6kb)
# -DNO_CGI		- disable CGI support (-5kb)
# -DNO_SSL		- disable SSL functionality (-2kb)
# -DNO_AUTH		- disable authorization support (-4kb)
# -DCONFIG=\"file\"	- use `file' as the default config file
# -DNO_SSI		- disable SSI support (-4kb)

# XXX Note for the windows users. In order to build shttpd, MSVS6 is needed.
# Follow these steps:
# 1. Add c:\path_to_msvs6\bin to the system Path environment variable.
# 2. Add two new system environment variables:
#    LIB=c:\path_to_msvs6\lib
#    INCLUDE=c:\path_to_msvs6\include
# 3. start console, go to shttpd-VERSION\src\ directory
# 4. type "nmake msvc"
# 5. go to shttpd-VERSION\examples , type "nmake msvc"


VC6=		..\..\..			# MSVC installation path
CL_FLAGS=	/MD /TC /nologo /DNDEBUG /Os 	# MSVC compiler flags

all:
	@echo "make (unix|msvc|mingw|rtems)"
	@echo on Linux, do \'LIBS=-ldl make unix\'

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

unix: $(OBJS)
	$(AR) -r lib$(PROG).a $(OBJS) && ranlib lib$(PROG).a 
	$(CC) $(CFLAGS) compat_unix.c standalone.c \
		-o $(PROG) $(LIBS) -L. -l$(PROG)

rtems:
	$(CC) -c $(CFLAGS) -DEMBEDDED $(SRCS) compat_rtems.c
	$(AR) -r lib$(PROG).a *.o && ranlib lib$(PROG).a 

#cl $(SRCS) compat_win32.c /c $(CL_FLAGS) /DEMBEDDED
#lib *.obj /out:shttpd.lib

msvc:
	$(VC6)\bin\cl /I $(VC6)\include \
		$(SRCS) compat_win32.c standalone.c  $(CL_FLAGS) \
		/link /out:$(PROG).exe /LIBPATH:$(VC6)\lib ws2_32.lib user32.lib

mingw:
	$(CC) -c $(CFLAGS) -DEMBEDDED $(SRCS) compat_win32.c
	$(AR) -r lib$(PROG).a *.o && ranlib lib$(PROG).a 
	$(CC) $(CFLAGS) $(SRCS) compat_win32.c standalone.c \
		-o $(PROG) $(LIBS) -lws2_32 -lcomdlg32 -lcomctl32

man:
	cat shttpd.1 | tbl | groff -man -Tascii | col -b > shttpd.1.txt
	cat shttpd.1 | tbl | groff -man -Tascii | less

clean:
	rm -rf *.o *.core $(PROG) lib$(PROG).a
