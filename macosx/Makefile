# $Id$

include ../Makefile.config
include ../Makefile.version
include ../Makefile.common

Info.plist: Info.plist.in ../Makefile.version ../Makefile.common
	$(RM) $@
	sed -e "s/%%BUNDLE_VERSION%%/$(VERSION_REVISION)/g" \
	  -e "s/%%SHORT_VERSION_STRING%%/$(VERSION_STRING)/g" < $< > $@

clean:
	$(RM) Info.plist
