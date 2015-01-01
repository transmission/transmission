b64: Base64 Encoding/Decoding Routines
======================================

Overview:
--------
libb64 is a library of ANSI C routines for fast encoding/decoding data into and
from a base64-encoded format. C++ wrappers are included, as well as the source
code for standalone encoding and decoding executables.

base64 consists of ASCII text, and is therefore a useful encoding for storing 
binary data in a text file, such as xml, or sending binary data over text-only
email.

References:
----------
* Wikipedia article:
	http://en.wikipedia.org/wiki/Base64
* base64, another implementation of a commandline en/decoder:
	http://www.fourmilab.ch/webtools/base64/

Why?
---
I did this because I need an implementation of base64 encoding and decoding,
without any licensing problems. Most OS implementations are released under
either the GNU/GPL, or a BSD-variant, which is not what I require.

Also, the chance to actually use the co-routine implementation in code is rare,
and its use here is fitting. I couldn't pass up the chance.
For more information on this technique, see "Coroutines in C", by Simon Tatham,
which can be found online here: 
http://www.chiark.greenend.org.uk/~sgtatham/coroutines.html

So then, under which license do I release this code? On to the next section...

License:
-------
This work is released under into the Public Domain.
It basically boils down to this: I put this work in the public domain, and you
can take it and do whatever you want with it.

An example of this "license" is the Creative Commons Public Domain License, a
copy of which can be found in the LICENSE file, and also online at
http://creativecommons.org/licenses/publicdomain/

Commandline Use:
---------------
There is a new executable available, it is simply called base64.
It can encode and decode files, as instructed by the user.

To encode a file:
$ ./base64 -e filea fileb
fileb will now be the base64-encoded version of filea.

To decode a file:
$ ./base64 -d fileb filec
filec will now be identical to filea.

Programming:
-----------
Some C++ wrappers are provided as well, so you don't have to get your hands
dirty. Encoding from standard input to standard output is as simple as

	#include <b64/encode.h>
	#include <iostream>
	int main()
	{
		base64::encoder E;
		E.encode(std::cin, std::cout);
		return 0;
	}

Both standalone executables and a static library is provided in the package,

Example code:
------------
The 'examples' directory contains some simple example C code, that demonstrates
how to use the C interface of the library.

Implementation:
--------------
It is DAMN fast, if I may say so myself. The C code uses a little trick which
has been used to implement coroutines, of which one can say that this
implementation is an example.

(To see how the libb64 codebase compares with some other BASE64 implementations
available, see the BENCHMARKS file)

The trick involves the fact that a switch-statement may legally cross into
sub-blocks. A very thorough and enlightening essay on co-routines in C, using
this method, can be found in the above mentioned "Coroutines in C", by Simon
Tatham: http://www.chiark.greenend.org.uk/~sgtatham/coroutines.html

For example, an RLE decompressing routine, adapted from the article:
1	static int STATE = 0;
2	static int len, c;
3	switch (STATE)
4	{
5		while (1)
6		{
7			c = getchar();
8			if (c == EOF) return EOF;
9			if (c == 0xFF) {
10				len = getchar();
11				c = getchar();
12				while (len--)
13				{
14					STATE = 0;
15					return c;
16	case 0:
17				}
18			} else
19				STATE = 1;
20				return c;
21	case 1:
22			}
23		}
24	}

As can be seen from this example, a coroutine depends on a state variable,
which it sets directly before exiting (lines 14 and 119). The next time the
routine is entered, the switch moves control to the specific point directly
after the previous exit (lines 16 and 21).hands

(As an aside, in the mentioned article the combination of the top-level switch,
the various setting of the state, the return of a value, and the labelling of
the exit point is wrapped in #define macros, making the structure of the
routine even clearer.)

The obvious problem with any such routine is the static keyword.
Any static variables in a function spell doom for multithreaded applications.
Also, in situations where this coroutine is used by more than one other
coroutines, the consistency is disturbed.

What is needed is a structure for storing these variabled, which is passed to
the routine seperately. This obviously breaks the modularity of the function,
since now the caller has to worry about and care for the internal state of the
routine (the callee). This allows for a fast, multithreading-enabled
implementation, which may (obviously) be wrapped in a C++ object for ease of
use.

The base64 encoding and decoding functionality in this package is implemented
in exactly this way, providing both a high-speed high-maintanence C interface,
and a wrapped C++ which is low-maintanence and only slightly less performant.
