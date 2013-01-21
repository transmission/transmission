#include <assert.h>
#include <stdio.h>
#include <unistd.h> /* sync() */

#include "transmission.h"
#include "blocklist.h"
#include "net.h"
#include "session.h" /* tr_sessionIsAddressBlocked() */
#include "utils.h"

#include "libtransmission-test.h"

static const char * contents1 =
  "Austin Law Firm:216.16.1.144-216.16.1.151\n"
  "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255\n"
  "Corel Corporation:216.21.157.192-216.21.157.223\n"
  "Fox Speed Channel:216.79.131.192-216.79.131.223\n";

static const char * contents2 =
  "Austin Law Firm:216.16.1.144-216.16.1.151\n"
  "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255\n"
  "Corel Corporation:216.21.157.192-216.21.157.223\n"
  "Fox Speed Channel:216.79.131.192-216.79.131.223\n"
  "Evilcorp:216.88.88.0-216.88.88.255\n";

static char *
create_blocklist_text_file (const char * basename, const char * contents)
{
  FILE * fp;
  char * path;

  assert (blocklistDir != NULL);

  path = tr_buildPath (blocklistDir, basename, NULL);
  remove (path);
  fp = fopen (path, "w+");
  fprintf (fp, "%s", contents);
  fclose (fp);
  sync ();
  return path;
}

static bool
address_is_blocked (const char * address_str)
{
  struct tr_address addr;
  tr_address_from_string (&addr, address_str);
  return tr_sessionIsAddressBlocked (session, &addr);
}

static int
test_parsing (void)
{
  char * text_file;

  libtransmission_test_session_init_sandbox ();
  text_file = create_blocklist_text_file ("level1", contents1);
  libtransmission_test_session_init_session ();

  check (!tr_blocklistIsEnabled (session));
  tr_blocklistSetEnabled (session, true);
  check (tr_blocklistIsEnabled (session));

  check (tr_blocklistExists (session));
  check_int_eq (4, tr_blocklistGetRuleCount (session));

  check (!address_is_blocked ("216.16.1.143"));
  check ( address_is_blocked ("216.16.1.144"));
  check ( address_is_blocked ("216.16.1.145"));
  check ( address_is_blocked ("216.16.1.146"));
  check ( address_is_blocked ("216.16.1.147"));
  check ( address_is_blocked ("216.16.1.148"));
  check ( address_is_blocked ("216.16.1.149"));
  check ( address_is_blocked ("216.16.1.150"));
  check ( address_is_blocked ("216.16.1.151"));
  check (!address_is_blocked ("216.16.1.152"));
  check (!address_is_blocked ("216.16.1.153"));
  check (!address_is_blocked ("217.0.0.1"));
  check (!address_is_blocked ("255.0.0.1"));

  libtransmission_test_session_close ();
  tr_free (text_file);
  return 0;
}

/***
****
***/

static int
test_updating (void)
{
  char * text_file;

  libtransmission_test_session_init_sandbox ();
  text_file = create_blocklist_text_file ("level1", contents1);
  libtransmission_test_session_init_session ();
  check_int_eq (4, tr_blocklistGetRuleCount (session));

  /* test that updated source files will get loaded */
  tr_free (text_file);
  text_file = create_blocklist_text_file ("level1", contents2);
  tr_sessionReloadBlocklists (session);
  check_int_eq (5, tr_blocklistGetRuleCount (session));

  /* test that updated source files will get loaded */
  tr_free (text_file);
  text_file = create_blocklist_text_file ("level1", contents1);
  tr_sessionReloadBlocklists (session);
  check_int_eq (4, tr_blocklistGetRuleCount (session));

  /* ensure that new files, if bad, get skipped */
  tr_free (text_file);
  text_file = create_blocklist_text_file ("level1", "# nothing useful\n");
  tr_sessionReloadBlocklists (session);
  check_int_eq (4, tr_blocklistGetRuleCount (session));

  libtransmission_test_session_close ();
  tr_free (text_file);
  return 0;
}

/***
****
***/

int
main (void)
{
  const testFunc tests[] = { test_parsing,
                             test_updating };

  libtransmission_test_session_init_formatters ();

  return runTests (tests, NUM_TESTS (tests));
}
