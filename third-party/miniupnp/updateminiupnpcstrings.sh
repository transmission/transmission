#! /bin/sh
# $Id: updateminiupnpcstrings.sh,v 1.4 2009/07/29 08:34:01 nanard Exp $

FILE=miniupnpcstrings.h
TEMPLATE_FILE=${FILE}.in

# detecting the OS name and version
OS_NAME=`uname -s`
OS_VERSION=`uname -r`
if [ -f /etc/debian_version ]; then
	OS_NAME=Debian
	OS_VERSION=`cat /etc/debian_version`
fi
# use lsb_release (Linux Standard Base) when available
LSB_RELEASE=`which lsb_release`
if [ 0 -eq $? ]; then
	OS_NAME=`${LSB_RELEASE} -i -s`
	OS_VERSION=`${LSB_RELEASE} -r -s`
	case $OS_NAME in
		Debian)
			#OS_VERSION=`${LSB_RELEASE} -c -s`
			;;
		Ubuntu)
			#OS_VERSION=`${LSB_RELEASE} -c -s`
			;;
	esac
fi

echo "Detected OS [$OS_NAME] version [$OS_VERSION]"

EXPR="s|OS_STRING \".*\"|OS_STRING \"${OS_NAME}/${OS_VERSION}\"|"
#echo $EXPR
#echo "Backuping $FILE to $FILE.bak."
#cp $FILE $FILE.bak
test -f ${FILE}.in
echo "setting OS_STRING macro value to ${OS_NAME}/${OS_VERSION} in $FILE."
sed -e "$EXPR" < $TEMPLATE_FILE > $FILE

