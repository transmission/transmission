# $Id$

include mk/common.mk

default: all

# Before we do anything else, make sure we have config.mk and an
# updated version.mk

required: mk/config.mk mk/version.mk
mk/config.mk:
	@echo "Please run ./configure first."
	@false
mk/version.mk: FORCE
	@echo "Checking SVN revision..."
	@./version.sh
FORCE:

# Now call the Makefile that'll really build
# OS X has its special Makefile that wraps to Xcode

-include mk/config.mk
ifneq ($(SYSTEM),Darwin)
REALMAKE = $(MAKE) -f mk/default.mk
else
REALMAKE = $(MAKE) -f mk/osx.mk
endif

all: required
	@$(REALMAKE) all
clean: required
	@$(REALMAKE) clean
install: required
	@$(REALMAKE) install
package: required
	@$(REALMAKE) package
package-release: required
	@$(REALMAKE) package-release
