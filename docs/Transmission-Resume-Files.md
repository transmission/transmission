Transmission keeps working information on each torrent in a "resume" file. This file is stored in the 'resume' directory.

Filename: `<torrent file name>.<hash?>.resume`

The file contains the following **per-torrent** properties:
<table>
<tr><th>Property</th><th>Description</th></tr>
<tr><td><tt>activity-date</tt></td><td>Date we last uploaded/downloaded a piece of data</td></tr>
<tr><td><tt>added-date</tt></td><td>Date torrent was added</td></tr>
<tr><td><tt>corrupt</tt></td><td>total number of corrupt bytes downloaded</td></tr>
<tr><td><tt>done-date</tt></td><td>Date torrent finished downloading</td></tr>
<tr><td><tt>destination</tt></td><td>Download directory</td></tr>
<tr><td><tt>dnd</tt></td><td>Do not download file integer list (one item per file in torrent) 0=download, 1=dnd</td></tr>
<tr><td><tt>downloaded</tt></td><td>Total non-corrupt bytes downloaded</td></tr>
<tr><td><tt>incomplete-dir</tt></td><td>Location of incomplete torrent files</td></tr>
<tr><td><tt>max-peers</tt></td><td>Maximum number of connected peers</td></tr>
<tr><td><tt>paused</tt></td><td>true if torrent is paused</td></tr>
<tr><td><tt>peers2</tt></td><td>IPv4 peers</td></tr>
<tr><td><tt>peers2-6</tt></td><td>IPv6 peers</td></tr>
<tr><td><tt>priority</tt></td><td>list of file download priorities (one item per file in torrent),<br/>each value is -1 (low), 0 (std), +1 (high)</td></tr>
<tr><td><tt>bandwidth-priority</tt></td><td></td></tr>
<tr><td><tt>progress</tt></td><td></td></tr>
<tr><td><tt>speed-limit</tt></td><td></td></tr>
<tr><td><tt>speed-limit-up</tt></td><td>Torrent upload speed limit</td></tr>
<tr><td><tt>speed-limit-down</tt></td><td>Torrent download speed limit</td></tr>
<tr><td><tt>ratio-limit</tt></td><td>Torrent file limit</td></tr>
<tr><td><tt>uploaded</tt></td><td></td></tr>
<tr><td><tt>speed</tt></td><td></td></tr>
<tr><td><tt>use-global-speed-limit</tt></td><td></td></tr>
<tr><td><tt>use-speed-limit</tt></td><td></td></tr>
<tr><td><tt>down-speed</tt></td><td></td></tr>
<tr><td><tt>down-mode</tt></td><td></td></tr>
<tr><td><tt>up-speed</tt></td><td></td></tr>
<tr><td><tt>up-mode</tt></td><td></td></tr>
<tr><td><tt>ratio-mode</tt></td><td></td></tr>
<tr><td><tt>mtimes</tt></td><td></td></tr>
<tr><td><tt>bitfield</tt></td><td></td></tr>
</table>

## Constants
<table>
<tr><td>Maximum number of remembered peers</td><td><tt>MAX_REMEMBERED_PEERS</tt></td><td>200</td></tr>
</table>
