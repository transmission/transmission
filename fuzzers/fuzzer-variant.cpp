#include "../libtransmission/variant.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string s(data, data + size);
    std::string_view sv(s);
    auto val = tr_variant{};
    char const* benc_end = nullptr;
    if ( tr_variantFromBuf(
                &val,
                TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE,
                sv,
                &benc_end) ) {
        tr_variantToStr(&val, TR_VARIANT_FMT_BENC);
    }
    return 0;
}
