#include <string.h> /* strlen() */

#include "transmission.h"
#include "quark.h"
#include "libtransmission-test.h"

static int
test_static_quarks (void)
{
  int i;

  for (i=0; i<TR_N_KEYS; i++)
    {
      tr_quark q;
      size_t len;
      const char * str;

      str = tr_quark_get_string (i, &len);
      check_int_eq (strlen(str), len);
      check (tr_quark_lookup (str, len, &q));
      check_int_eq (i, q);
    }

  for (i=0; i+1<TR_N_KEYS; i++)
    {
      size_t len1, len2;
      const char *str1, *str2;

      str1 = tr_quark_get_string (i, &len1);
      str2 = tr_quark_get_string (i+1, &len2);

      check (strcmp (str1, str2) < 0);
    }

  return 0;
}

MAIN_SINGLE_TEST(test_static_quarks)
