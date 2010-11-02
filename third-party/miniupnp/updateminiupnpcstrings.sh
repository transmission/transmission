#! /bin/sh
# $Id: updateminiupnpcstrings.sh,v 1.4 2009/07/29 08:34:01 nanard Exp $

TEMPLATE_FILE=$1
OUTPUT_FILE=$2

# detecting the OS name and version
OS_NAME=`uname -s`
OS_VERSION=`uname -r`
if [ -f /etc/debian_version ]; then
	OS_NAME=Debian
	OS_VERSION=`cat /etc/debian_version`
fi
# use lsb_release (Linux Standard Base) when available
if os_name=`lsb_release -i -s 2>/dev/null`; then
    OS_NAME=$os_name
    OS_VERSION=`lsb_release -r -s`
fi

echo "Detected OS [$OS_NAME] version [$OS_VERSION]"

EXPR="s|OS_STRING \".*\"|OS_STRING \"${OS_NAME}/${OS_VERSION}\"|"
#echo $EXPR
#echo "Backing up $OUTPUT_FILE to $OUTPUT_FILE.bak."
#cp $OUTPUT_FILE $OUTPUT_FILE.bak
test -f ${TEMPLATE_FILE}
echo "setting OS_STRING macro value to ${OS_NAME}/${OS_VERSION} in $OUTPUT_FILE."
sed -e "$EXPR" < $TEMPLATE_FILE > $OUTPUT_FILE

