#include <stdio.h>
#include <stdlib.h>
#include <libtransmission/transmission.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{
    tr_ctor* ctor = tr_ctorNew(NULL);
    tr_ctorSetMetainfo(ctor, Data, Size);

    tr_info inf;
    tr_parse_result err = tr_torrentParse(ctor, &inf);

    tr_ctorFree(ctor);
    tr_metainfoFree(&inf);

    return err;
}
