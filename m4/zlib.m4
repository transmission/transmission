 AC_DEFUN([AC_PATH_ZLIB], [
	AC_ARG_WITH(zlib,
                    AC_HELP_STRING([--with-zlib=DIR],
                                 [search for ZLIB in DIR/include and DIR/lib]),
                    [for dir in `echo "$withval" | tr : ' '`; do
    if test -d $dir/include; then
      ZLIB_CFLAGS="$ZLIB_CFLAGS -I$dir/include"
    fi
    if test -d $dir/lib; then
      ZLIB_LDFLAGS="$ZLIB_LDFLAGS -L$dir/lib"
    fi
  done[]])

        AC_ARG_WITH(zlib-includes,
                    AC_HELP_STRING([--with-zlib-includes=DIR],
	                           [search for ZLIB includes in DIR]),
	            [for dir in `echo "$withval" | tr : ' '`; do
    if test -d $dir; then
      ZLIB_CFLAGS="$ZLIB_CFLAGS -I$dir"
    fi
  done[]])

ac_zlib_saved_CFLAGS="$CFLAGS"
ac_zlib_saved_LDFLAGS="$LDFLAGS"
ac_zlib_saved_LIBS="$LIBS"
CFLAGS="$CFLAGS $ZLIB_CFLAGS"
LDFLAGS="$LDFLAGS $ZLIB_LDFLAGS"
ac_have_zlibh=no
ac_have_zlib=no
  	touch /tmp/dummy1_zlib.h
        AC_CHECK_HEADERS([/tmp/dummy1_zlib.h], [ac_have_zlibh=yes], [ac_have_zlibh=no],
		[#include "zlib.h"])
	rm /tmp/dummy1_zlib.h
 	if test $ac_have_zlibh = yes; then
        	AC_SEARCH_LIBS(gzopen, [z], [ac_have_zlib=yes], [ac_have_zlib=no])
	fi
# List of places to try
testdirs="$HOME/opt/zlib $OBITINSTALL/other"
for dir in $testdirs; do
	if test $ac_have_zlib = no; then
		if  test -f $dir/include/zlib.h; then
			ZLIB_CFLAGS="-I$dir/include"
			CPPFLAGS="$ac_zlib_saved_CPPFLAGS $ZLIB_CFLAGS"
			ZLIB_LDFLAGS="-L$dir/lib"
			LDFLAGS="$ac_zlib_saved_LDFLAGS $ZLIB_LDFLAGS"
  			touch /tmp/dummy3_zlib.h
 		        AC_CHECK_HEADERS([/tmp/dummy3_zlib.h], [ac_have_zlibh=yes], [ac_have_zlibh=no],
				[#include "zlib.h"])
			rm /tmp/dummy3_zlib.h
			if test $ac_have_zlibh = yes; then
				# Force check
				ac_cv_search_gzopen="  "
		        	AC_SEARCH_LIBS(gzopen, [z], [ac_have_zlib=yes], [ac_have_zlib=no])
			fi
			if test $ac_have_zlib = yes ; then
				if test $ac_have_zlibh = yes ; then
					break;
				fi
			fi
		fi
	fi
done[]
if test $ac_have_zlib = no; then
	AC_MSG_WARN([cannot find ZLIB library])
fi
if test $ac_have_zlibh = no; then
	AC_MSG_WARN([cannot find ZLIB headers])
	ac_have_zlib=no
fi
if test $ac_have_zlib = yes; then
	AC_DEFINE(HAVE_ZLIB, 1, [Define to 1 if ZLIB is available.])
fi
ZLIB_LIBS="$LIBS"
CFLAGS="$ac_zlib_saved_CFLAGS"
LDFLAGS="$ac_zlib_saved_LDFLAGS"
LIBS="$ac_zlib_saved_LIBS"
	AC_SUBST(ZLIB_CFLAGS)
	AC_SUBST(ZLIB_LDFLAGS)
	AC_SUBST(ZLIB_LIBS)
])
