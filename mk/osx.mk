# $Id$

#
# Update Info.plist with the version from version.mk
#
include mk/version.mk
macosx/Info.plist: macosx/Info.plist.in mk/version.mk
	$(RM) $@
	sed -e "s/%%BUNDLE_VERSION%%/$(VERSION_REVISION)/g" \
	  -e "s/%%SHORT_VERSION_STRING%%/$(VERSION_STRING)/g" < $< > $@

#
# Then use Xcode do make the actual build
#
all: macosx/Info.plist
	@xcodebuild -alltargets -activeconfiguration | grep -v "^$$"
clean:
	@xcodebuild -alltargets -activeconfiguration clean | grep -v "^$$"
	$(RM) macosx/Info.plist


#
# Package generation
# Check if this is a release or an SVN build
#
ifeq ($(VERSION_STRING),$(VERSION_MAJOR).$(VERSION_MINOR))
VERSION_PACKAGE = $(VERSION_STRING)
else
VERSION_PACKAGE = $(VERSION_STRING)-r$(VERSION_REVISION)
endif
	
URL = printf "[InternetShortcut]\nURL=http://transmission.m0k.org%s\n"
define PACKAGE_RULE1
	$(RM) tmp "Transmission $(VERSION_PACKAGE)" \
	  Transmission-$(VERSION_PACKAGE).dmg
	mkdir -p tmp
	cp -R macosx/Transmission.app tmp/
	cp AUTHORS tmp/AUTHORS.txt
	cp LICENSE tmp/LICENSE.txt
	cp NEWS tmp/NEWS.txt
	$(URL) "/" > tmp/Homepage.url
	$(URL) "/forum" > tmp/Forums.url
	$(URL) "/contribute.php" > tmp/Contribute.url
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
