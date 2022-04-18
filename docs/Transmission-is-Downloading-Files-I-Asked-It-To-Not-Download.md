## Summary ##
BitTorrent content is grouped into pieces that are typically somewhere between 64 KiB and 1 MiB in size. The .torrent file comes with a checksum for each piece, and every byte in that piece needs to be downloaded before the checksum can be compared for correctness.

So the problem arises when a piece overlaps two (or more) files, one of which you've flagged for download and the other you've flagged to ''not'' download. In order to run verify the checksum for the file you want, Transmission has to download the fragment of the other file where the piece overlaps.

This is what causes chunks of unwanted files to show up sometimes. Transmission only downloads the chunks needed to complete the piece and run the checksum tests.

## Future ##
There is a [https://trac.transmissionbt.com/ticket/532 trac ticket] for this to collect all the unwanted fragments into a single file to avoid clutter.
