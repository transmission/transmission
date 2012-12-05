#include "transmission.h"
#include "bencode.h"
#include "rpcimpl.h"
#include "utils.h"

#undef VERBOSE
#include "libtransmission-test.h"

static int
test_list (void)
{
    int64_t      i;
    const char * str;
    tr_benc      top;

    tr_rpc_parse_list_str (&top, "12", -1);
    check (tr_bencIsInt (&top));
    check (tr_bencGetInt (&top, &i));
    check_int_eq (12, i);
    tr_bencFree (&top);

    tr_rpc_parse_list_str (&top, "12", 1);
    check (tr_bencIsInt (&top));
    check (tr_bencGetInt (&top, &i));
    check_int_eq (1, i);
    tr_bencFree (&top);

    tr_rpc_parse_list_str (&top, "6,7", -1);
    check (tr_bencIsList (&top));
    check (tr_bencListSize (&top) == 2);
    check (tr_bencGetInt (tr_bencListChild (&top, 0), &i));
    check_int_eq (6, i);
    check (tr_bencGetInt (tr_bencListChild (&top, 1), &i));
    check_int_eq (7, i);
    tr_bencFree (&top);

    tr_rpc_parse_list_str (&top, "asdf", -1);
    check (tr_bencIsString (&top));
    check (tr_bencGetStr (&top, &str));
    check_streq ("asdf", str);
    tr_bencFree (&top);

    tr_rpc_parse_list_str (&top, "1,3-5", -1);
    check (tr_bencIsList (&top));
    check (tr_bencListSize (&top) == 4);
    check (tr_bencGetInt (tr_bencListChild (&top, 0), &i));
    check_int_eq (1, i);
    check (tr_bencGetInt (tr_bencListChild (&top, 1), &i));
    check_int_eq (3, i);
    check (tr_bencGetInt (tr_bencListChild (&top, 2), &i));
    check_int_eq (4, i);
    check (tr_bencGetInt (tr_bencListChild (&top, 3), &i));
    check_int_eq (5, i);
    tr_bencFree (&top);

    return 0;
}

MAIN_SINGLE_TEST (test_list)
