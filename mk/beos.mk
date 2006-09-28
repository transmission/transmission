# $Id$

include ../mk/config.mk
include ../mk/common.mk

SRCS = TRApplication.cpp TRWindow.cpp TRTransfer.cpp \
       TRPrefsWindow.cpp TRInfoWindow.cpp
OBJS = $(SRCS:%.cpp=%.o)

CXXFLAGS += -I../libtransmission
LDLIBS   += ../libtransmission/libtransmission.a
CXXFLAGS += -IlibPrefs
LDFLAGS  += -lbe -ltracker
LDLIBS   += libPrefs/libPrefs.a

Transmission: $(OBJS) Transmission.rsrc
	$(CXX) -o $@ $(OBJS) $(LDLIBS) $(LDFLAGS)
	xres -o Transmission Transmission.rsrc
	mimeset -f Transmission

%.o: %.cpp ../mk/config.mk ../mk/common.mk ../mk/beos.mk
	$(CXX) $(CXXFLAGS) -o $@ -c $<

clean:
	$(RM) Transmission $(OBJS)

.depend: $(SRCS) ../mk/config.mk ../mk/common.mk ../mk/beos.mk
	$(RM) .depend
	$(foreach SRC, $(SRCS), $(CXX) $(CXXFLAGS) -MM $(SRC) >> .depend;)

install:
	@true

-include .depend
