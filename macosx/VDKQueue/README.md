VDKQueue
=======

A modern, faster, better version of UKKQueue.

<http://incident57.com/codekit>


about
-----

VDKQueue is an Objective-C wrapper around kernel queues (kQueues).
It allows you to watch a file or folder for changes and be notified when they occur.

VDKQueue is a modern, streamlined and much faster version of UKKQueue, which was originally written in 2003 by Uli Kusterer.
Objective-C has come a long way in the past nine years and UKKQueue was long in the tooth. VDKQueue is better in several ways:

	-- The number of method calls is vastly reduced.
	-- Grand Central Dispatch is used in place of Uli's "threadProxy" notifications (much faster)
	-- Memory footprint is roughly halved, since VDKQueue creates less overhead
	-- Fewer locks are taken, especially in loops (faster)
	-- The code is *much* cleaner and simpler!
	-- There is only one .h and one .m file to include.
	
VDKQueue also fixes long-standing bugs in UKKQueue. For example: OS X limits the number of open file descriptors each process
may have to about 3,000. If UKKQueue fails to open a new file descriptor because it has hit this limit, it will crash. VDKQueue will not.
	
	
	
performance
-----------

Adding 1,945 file paths to a UKKQueue instance took, on average, 80ms. 
Adding those same files to a VDKQueue instance took, on average, 65ms.

VDKQueue processes and pushes out notifications about file changes roughly 50-70% faster than UKKQueue.

All tests conducted on a 2008 MacBook Pro 2.5Ghz with 4GB of RAM running OS 10.7.3 using Xcode and Instruments (time profiler).

	


requirements
------------

VDKQueue requires Mac OS X 10.6+ because it uses Grand Central Dispatch.

VDKQueue does not support garbage collection. If you use garbage collection, you are lazy. Shape up.

VDKQueue does not currently use ARC, although it should be straightforward to convert if you wish. (Don't be the guy that can't manually manage memory, though.)




license
-------

Created by Bryan D K Jones on 28 March 2012
Copyright 2013 Bryan D K Jones

Based heavily on UKKQueue, which was created and copyrighted by Uli Kusterer on 21 Dec 2003.

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software. Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
	   
3. This notice may not be removed or altered from any source distribution.
