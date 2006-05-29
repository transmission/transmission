#! /bin/sh
#
# $Id$

REVMAX=0

for pattern in '*.[chm]' '*.cpp' '*.po' 'Makefile*' 'configure' 'version.sh'; do
  for f in `find . -name "$pattern"`; do 
    REV=`grep '\$Id:' $f | sed 's/.*\$Id: [^ ]* \([0-9]*\) .*/\1/'`
    if [ -n "$REV" ]; then
      if [ "$REV" -gt "$REVMAX" ]; then
        REVMAX="$REV"
      fi
    fi
  done
done

rm -f Makefile.version
echo "SVN_REVISION = $REVMAX" > Makefile.version

exit 0
