include Makefile.config
include Makefile.common

ifneq ($(SYSTEM),Darwin)

TARGETS = .cli
ifeq ($(GTK),yes)
TARGETS += .gtk
endif
ifeq ($(SYSTEM),BeOS)
TARGETS += .beos
endif

all: $(TARGETS)

.lib:
	@echo "* Building libtransmission"
	@$(MAKE) -C libtransmission

.cli: .lib
	@echo "* Building Transmission CLI client"
	@$(MAKE) -C cli

.gtk: .lib
	@echo "* Building Transmission GTK+ client"
	@$(MAKE) -C gtk

.beos: .lib
	@echo "* Building Transmission BeOS client"
	@make -C beos

clean:
	@$(MAKE) -C libtransmission clean
	@$(MAKE) -C cli clean
ifeq ($(GTK),yes)
	@$(MAKE) -C gtk clean
endif
ifeq ($(SYSTEM),BeOS)
	@$(MAKE) -C beos clean
endif

else

all:
	@$(MAKE) -C macosx
	@xcodebuild -alltargets -activeconfiguration | grep -v "^$$"

clean:
	@xcodebuild -alltargets -activeconfiguration clean | grep -v "^$$"
	@$(MAKE) -C macosx clean

MAKELINK = printf "[InternetShortcut]\nURL=http://%s\n"

package:
	$(RM) tmp "Transmission $(VERSION_STRING)" \
	    Transmission-$(VERSION_STRING).dmg && \
	  mkdir -p tmp && \
	  cp -r macosx/Transmission.app tmp/ && \
	  cp AUTHORS tmp/AUTHORS.txt && \
	  cp LICENSE tmp/LICENSE.txt && \
	  cp NEWS tmp/NEWS.txt && \
	  strip -S tmp/Transmission.app/Contents/MacOS/Transmission && \
	  $(MAKELINK) "transmission.m0k.org/" > tmp/Homepage.url && \
	  $(MAKELINK) "transmission.m0k.org/forum" > tmp/Forums.url && \
	  $(MAKELINK) "transmission.m0k.org/contribute.php" > tmp/Contribute.url && \
	  mv tmp "Transmission $(VERSION_STRING)" && \
	  hdiutil create -format UDZO -srcfolder \
	    "Transmission $(VERSION_STRING)" Transmission-$(VERSION_STRING).dmg && \
	  rm -rf "Transmission $(VERSION_STRING)"

endif
