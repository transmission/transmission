SYSTEM         = Linux
PREFIX         = /usr/local
LOCALEDIR      = /usr/local/share/locale
BEOS_NETSERVER = no
PTHREAD        = yes
OPENSSL        = yes
GTK            = yes
CC             = cc
CFLAGS         = 
CXX            = c++
CXXFLAGS       = 
LDFLAGS        =  -lm
CFLAGS_GTK     = -DXTHREADS -I/usr/include/gtk-2.0 -I/usr/lib/gtk-2.0/include -I/usr/X11R6/include -I/usr/include/atk-1.0 -I/usr/include/pango-1.0 -I/usr/include/freetype2 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include   -DLOCALEDIR=\"/usr/local/share/locale\"
LDFLAGS_GTK    = -Wl,--export-dynamic -lgtk-x11-2.0 -lgdk-x11-2.0 -latk-1.0 -lgdk_pixbuf-2.0 -lm -lpangoxft-1.0 -lpangox-1.0 -lpango-1.0 -lgobject-2.0 -lgmodule-2.0 -ldl -lglib-2.0  
