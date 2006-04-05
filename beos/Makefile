include ../Makefile.config
include ../Makefile.common

SRCS = TRApplication.cpp TRWindow.cpp TRTransfer.cpp \
       TRPrefsWindow.cpp TRInfoWindow.cpp
OBJS = $(SRCS:%.cpp=%.o)

CXXFLAGS += -I../libtransmission
LDFLAGS  += ../libtransmission/libtransmission.a
CXXFLAGS += -IlibPrefs
LDFLAGS  += -lbe -ltracker libPrefs/libPrefs.a

Transmission: $(OBJS) Transmission.rsrc
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)
	xres -o Transmission Transmission.rsrc
	mimeset -f Transmission

%.o: %.cpp ../Makefile.config ../Makefile.common Makefile
	$(CXX) $(CXXFLAGS) -o $@ -c $<

clean:
	$(RM) Transmission $(OBJS)

.depend: $(SRCS) Makefile
	$(RM) .depend
	$(foreach SRC, $(SRCS), $(CXX) $(CXXFLAGS) -MM $(SRC) >> .depend;)

-include .depend
