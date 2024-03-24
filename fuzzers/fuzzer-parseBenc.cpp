#include "../libtransmission/torrent-metainfo.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string s(data, data + size);
    std::string_view benc(s);
    tr_torrent_metainfo m;
    m.parseBenc(benc);
    return 0;
}
