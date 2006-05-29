# $Id$

-include Makefile.config
ifndef CONFIGURE_RUN
$(error You must run ./configure first)
endif

-include Makefile.version
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

.lib: .version
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

install: all $(foreach SUB,$(TARGETS),.install$(SUB))

.install.cli: .cli
	@echo "* Installing Transmission CLI client"
	@$(MAKE) -C cli install

.install.gtk: .gtk
	@echo "* Installing Transmission GTK+ client"
	@$(MAKE) -C gtk install

.install.beos:

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

all: .version
	@$(MAKE) -C macosx
	@xcodebuild -alltargets -activeconfiguration | grep -v "^$$"

clean:
	@xcodebuild -alltargets -activeconfiguration clean | grep -v "^$$"
	@$(MAKE) -C macosx clean

MAKELINK = printf "[InternetShortcut]\nURL=http://transmission.m0k.org%s\n"
define PACKAGE_RULE1
	$(RM) tmp "Transmission $(VERSION_STRING)" \
	  Transmission-$(VERSION_STRING).dmg
	mkdir -p tmp
	cp -r macosx/Transmission.app tmp/
	cp AUTHORS tmp/AUTHORS.txt
	cp LICENSE tmp/LICENSE.txt
	cp NEWS tmp/NEWS.txt
	$(MAKELINK) "/" > tmp/Homepage.url
	$(MAKELINK) "/forum" > tmp/Forums.url
	$(MAKELINK) "/contribute.php" > tmp/Contribute.url
endef
define PACKAGE_RULE2
	mv tmp "Transmission $(VERSION_STRING)"
	hdiutil create -format UDZO -srcfolder \
	  "Transmission $(VERSION_STRING)" Transmission-$(VERSION_STRING).dmg
	rm -rf "Transmission $(VERSION_STRING)"
endef

package:
	$(PACKAGE_RULE1)
	$(PACKAGE_RULE2)

package-release:
	$(PACKAGE_RULE1)
	strip -S tmp/Transmission.app/Contents/MacOS/Transmission
	$(PACKAGE_RULE2)

endif

Makefile.version: .version

.version:
	@echo "Checking SVN revision..."
	@./version.sh
