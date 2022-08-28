#include "../libtransmission/magnet-metainfo.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string s(data, data + size);
    std::string_view sv(s);
    tr_magnet_metainfo m;
    m.parseMagnet(sv);
    return 0;
}
