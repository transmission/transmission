include ../Makefile.config
include ../Makefile.common

Info.plist: Info.plist.in ../Makefile.common
	$(RM) $@
	sed "s/%%VERSION%%/$(VERSION_STRING)/g" < $< > $@

clean:
	$(RM) Info.plist
