Can I sequence the files I download? This is quite a common question. This entry will try to answer it and offer solutions.

## Why is this "feature" not implemented in Transmission?
Because it makes the overall swarm's health worse.

## But I want to sequence the files I download, how do I do it?
There are basically two methods, one DIY method that requires some code modification and the "user friendly" version.

1. DIY code changing approach: Tweak _compareRefillPiece()_ in _libtransmission/peer-mgr.c_
   Replace:
   ```c
   /* otherwise go with our random seed */
   return tr_compareUint16( a->random, b->random );
   ```

   With:
   ```c
   /* otherwise download the pieces in order */
   return tr_compareUint16( a->piece, b->piece );
   ```

2. "User-friendly" approach: In the file inspector, change the priorities "by hand".
