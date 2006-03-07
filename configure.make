#! /bin/sh

#
# Default settings
#
SYSTEM=
BEOS_NETSERVER=no
MATH=no
PTHREAD=no
OPENSSL=
CC="${CC-cc}"
CFLAGS="${CFLAGS}"
LDFLAGS="${LDFLAGS}"


#
# Functions
#
usage()
{
  cat << EOF

Options:
  --disable-openssl      Disable OpenSSL, use built-in SHA1 implementation

Some influential environment variables:
  CC          C compiler command
  CFLAGS      C compiler flags
  LDFLAGS     linker flags

EOF
}

openssl_test()
{
  cat > testconf.c << EOF
  #include <stdio.h>
  #include <openssl/sha.h>
  int main()
  {
      SHA1( 0, 0, 0 );
  }
EOF
  if $CC $CFLAGS $LDFLAGS -o testconf testconf.c -lcrypto > /dev/null 2>&1
  then
    echo "yes"
    OPENSSL=yes
  else
    echo "missing, using built-in SHA1 implementation"
    OPENSSL=no
  fi
  rm -f testconf.c testconf
}


#
# Parse options
#
while [ $# -ne 0 ]; do
  param=`expr "opt$1" : 'opt[^=]*=\(.*\)'`

  case "x$1" in
    x--disable-openssl)
      OPENSSL=no
      ;;
    x--help)
      usage
      exit 0
      ;;
  esac
  shift
done

#
# System-specific flags
#
SYSTEM=`uname -s`
case $SYSTEM in
  BeOS)
    RELEASE=`uname -r`
    case $RELEASE in
      6.0*|5.0.4) # Zeta or R5 / BONE beta 7
        ;;
      5.0*)       # R5 / net_server
        BEOS_NETSERVER=yes
        ;;
      *)
        echo "Unsupported BeOS version"
        exit 1 
        ;;
    esac
    ;;

  Darwin)
    # Make sure the Universal SDK is installed
    if [ ! -d /Developer/SDKs/MacOSX10.4u.sdk ]; then
      cat << EOF
You need to install the Universal SDK in order to build Transmission:
  Get your Xcode CD or package
  Restart the install
  When it gets to "Installation Type", select "Customize"
  Select "Mac OS X 10.4 (Universal) SDL" under "Cross Development"
  Finish the install.
EOF
      exit 1
    fi
    PTHREAD=yes
    ;;

  FreeBSD|NetBSD|OpenBSD|Linux)
    MATH=yes
    PTHREAD=yes
    ;;

  *)
    echo "Unsupported operating system"
    exit 1 ;;
esac
echo "System:  $SYSTEM"

#
# OpenSSL settings
#
echo -n "OpenSSL: "
if [ "$OPENSSL" = no ]; then
  echo "disabled, using built-in SHA1 implementation"
else
  openssl_test
fi 

#
# Generate Makefile.config
#
rm -f Makefile.config
cat > Makefile.config << EOF
SYSTEM         = $SYSTEM
BEOS_NETSERVER = $BEOS_NETSERVER
MATH           = $MATH
PTHREAD        = $PTHREAD
OPENSSL        = $OPENSSL
CC             = $CC
CFLAGS         = $CFLAGS
LDFLAGS        = $LDFLAGS
EOF

echo
echo "To build Transmission, run 'make'."
