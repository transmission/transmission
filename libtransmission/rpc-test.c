#include "transmission.h"
#include "rpcimpl.h"
#include "utils.h"
#include "variant.h"

#include "libtransmission-test.h"

static int
test_list (void)
{
    size_t len;
    int64_t i;
    const char * str;
    tr_variant top;

    tr_rpc_parse_list_str (&top, "12", -1);
    check (tr_variantIsInt (&top));
    check (tr_variantGetInt (&top, &i));
    check_int_eq (12, i);
    tr_variantFree (&top);

    tr_rpc_parse_list_str (&top, "12", 1);
    check (tr_variantIsInt (&top));
    check (tr_variantGetInt (&top, &i));
    check_int_eq (1, i);
    tr_variantFree (&top);

    tr_rpc_parse_list_str (&top, "6,7", -1);
    check (tr_variantIsList (&top));
    check (tr_variantListSize (&top) == 2);
    check (tr_variantGetInt (tr_variantListChild (&top, 0), &i));
    check_int_eq (6, i);
    check (tr_variantGetInt (tr_variantListChild (&top, 1), &i));
    check_int_eq (7, i);
    tr_variantFree (&top);

    tr_rpc_parse_list_str (&top, "asdf", -1);
    check (tr_variantIsString (&top));
    check (tr_variantGetStr (&top, &str, &len));
    check_int_eq (4, len);
    check_streq ("asdf", str);
    tr_variantFree (&top);

    tr_rpc_parse_list_str (&top, "1,3-5", -1);
    check (tr_variantIsList (&top));
    check (tr_variantListSize (&top) == 4);
    check (tr_variantGetInt (tr_variantListChild (&top, 0), &i));
    check_int_eq (1, i);
    check (tr_variantGetInt (tr_variantListChild (&top, 1), &i));
    check_int_eq (3, i);
    check (tr_variantGetInt (tr_variantListChild (&top, 2), &i));
    check_int_eq (4, i);
    check (tr_variantGetInt (tr_variantListChild (&top, 3), &i));
    check_int_eq (5, i);
    tr_variantFree (&top);

    return 0;
}

MAIN_SINGLE_TEST (test_list)
