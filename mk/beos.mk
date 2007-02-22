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

Transmission: $(OBJS) ../beos/Transmission.rsrc
	$(CXX) -o $@ $(OBJS) $(LDLIBS) $(LDFLAGS)
	xres -o Transmission ../beos/Transmission.rsrc
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
