#compdef transmissioncli 

_arguments -s -S \
'(-h --help)'{-h,--help}'[Print help and exit]' \
'(-i --info)'{-i,--info}'[Print metainfo and exit]' \
'(-d --download)'{-d,--download}'[Maximum download rate (nolimit = -1) (default = -1)]:integer:' \
'(-n --net-traversal)'{-n,--nat-traversal}'[Attempt NAT traversal using NAT-PMP or UPnP IGD]' \
'(-p --port)'{-p,--port}'[Port we should listen on (default = 9090)]:integer:' \
'(-s --scrape)'{-s,--scrape}'[Print counts of seeders/leechers and exit]' \
'(-u --upload)'{-u,--upload}'[Maximum upload rate (nolimit = -1) (default = 20)]:integer:' \
'(-v --verbose)'{-v,--verbose}'[Verbose level]:level:((0\:Low 1\:Medium 2\:High))' \
"*:torrent files:_files -g '*.torrent'" 
