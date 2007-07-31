UKKQUEUE
--------

A wrapper class around the kqueue file change notification mechanism.

Simply create a UKKQueue (or use the singleton), add a few paths to it and listen to the change notifications via NSWorkspace's notification center.

LICENSE:

(c) 2003-06 by M. Uli Kusterer. You may redistribute, modify, use in
commercial products free of charge, however distributing modified copies
requires that you clearly mark them as having been modified by you, while
maintaining the original markings and copyrights. I don't like getting bug
reports about code I wasn't involved in.

I'd also appreciate if you gave credit in your app's about screen or a similar
place. A simple "Thanks to M. Uli Kusterer" is quite sufficient.
Also, I rarely turn down any postcards, gifts, complementary copies of
applications etc.


REVISION HISTORY:
0.1 - Initial release.
0.2 - Now calls delegate on main thread using UKMainThreadProxy, and checks retain count to make sure the object is released even when the thread is still holding on to it. Equivalent to SVN revision 79.
0.3 - Now adopts UKFileWatcher protocol to allow swapping out a kqueue for another scheme easily. Uses O_EVONLY instead of O_RDONLY to open the file without preventing it from being deleted or its drive ejected.
0.4 - Now includes UKFNSubscribeFileWatcher, and closes the kqueue file descriptor in a separate thread (thanks to Dominic Yu for the suggestion!) so you don't have to wait for close() to time out.
0.5 - Turns off all deprecated features. Changes the notifications to make it possible to subscribe to them more selectively. Changes notification constants to be safer for apps that expose KQueue to their plugins. FNSubscribeFileWatcher now also sends notifications (sorry, "write" only).


CONTACT:
Get the newest version at http://www.zathras.de
E-Mail me at witness (at) zathras (dot) de or witness (dot) of (dot) teachtext (at) gmx (dot) net
