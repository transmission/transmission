# $Id$

include ../mk/config.mk
include ../mk/common.mk

SRCS = TRApplication.cpp TRWindow.cpp TRTransfer.cpp \
       TRPrefsWindow.cpp TRInfoWindow.cpp
OBJS = $(SRCS:%.cpp=%.o)

CXXFLAGS += -I.. -IlibPrefs
LDLIBS   += ../libtransmission/libtransmission.a
CXXFLAGS += -IlibPrefs
LDFLAGS  += -lbe -ltracker
LDLIBS   += libPrefs/libPrefs.a

Transmission: $(OBJS) ../libtransmission/libtransmission.a Transmission.rsrc
	$(LINK_RULE_CXX)
	$(XRES_RULE)
	$(MIMESET_RULE)

%.o: %.cpp ../mk/config.mk ../mk/common.mk ../mk/beos.mk
	$(CXX_RULE)

clean:
	@echo "Clean Transmission"
	@$(RM) Transmission
	@echo "Clean $(OBJS)"
	@$(RM) $(OBJS)

.depend: $(SRCS) ../mk/config.mk ../mk/common.mk ../mk/beos.mk
	$(DEP_RULE_CXX)

install:
	@true

-include .depend
