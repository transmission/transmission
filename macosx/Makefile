# $Id$

include ../Makefile.config
include ../Makefile.version
include ../Makefile.common

Info.plist: Info.plist.in ../Makefile.version ../Makefile.common
	$(RM) $@
	sed "s/%%VERSION%%/$(VERSION_STRING)/g" < $< > $@

clean:
	$(RM) Info.plist
