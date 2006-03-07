include ../Makefile.config
include ../Makefile.common

SRCS = $(shell ls *.m *.h *.plist)

Transmission.app: $(SRCS) Info.plist
	xcodebuild -configuration Debug | grep -v "^$$"

Info.plist: Info.plist.in ../Makefile.common
	$(RM) $@
	sed "s/%%VERSION%%/$(VERSION_STRING)/g" < $< > $@

clean:
	$(RM) Info.plist build
