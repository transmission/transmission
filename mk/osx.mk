# $Id$

#
# Use Xcode do make the actual build
#
all:
	@xcodebuild -alltargets -activeconfiguration | grep -v "^$$"
clean:
	@xcodebuild -alltargets -activeconfiguration clean | grep -v "^$$"


#
# Package generation
# Check if this is a release or an SVN build
#
include mk/version.mk
ifeq ($(VERSION_STRING),$(VERSION_MAJOR).$(VERSION_MINOR))
VERSION_PACKAGE = $(VERSION_STRING)
else
VERSION_PACKAGE = $(VERSION_STRING)-r$(VERSION_REVISION)
endif
	
define PACKAGE_RULE1
	$(RM) tmp "Transmission $(VERSION_PACKAGE)" \
	  Transmission-$(VERSION_PACKAGE).dmg
	mkdir -p tmp
	cp -R macosx/Transmission.app tmp/
endef
define PACKAGE_RULE2
	mv tmp "Transmission $(VERSION_PACKAGE)"
	hdiutil create -format UDZO -imagekey zlib-level=9 -srcfolder \
	  "Transmission $(VERSION_PACKAGE)" Transmission-$(VERSION_PACKAGE).dmg
	rm -rf "Transmission $(VERSION_PACKAGE)"
endef

package:
	$(PACKAGE_RULE1)
	$(PACKAGE_RULE2)

package-release:
	$(PACKAGE_RULE1)
	strip -S tmp/Transmission.app/Contents/MacOS/Transmission
	$(PACKAGE_RULE2)
