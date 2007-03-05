# $Id$

include ../mk/config.mk
include ../mk/common.mk

SRCS = ../beos/TRApplication.cpp ../beos/TRWindow.cpp ../beos/TRTransfer.cpp \
       ../beos/TRPrefsWindow.cpp ../beos/TRInfoWindow.cpp
OBJS = $(SRCS:%.cpp=%.o)

CXXFLAGS += -I../libtransmission -I../beos/libPrefs
LDLIBS   += ../libtransmission/libtransmission.a
CXXFLAGS += -IlibPrefs
LDFLAGS  += -lbe -ltracker
LDLIBS   += ../beos/libPrefs/libPrefs.a

Transmission: $(OBJS) ../libtransmission/libtransmission.a ../beos/Transmission.rsrc
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
