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

static void
create_text_file (const char * path, const char * contents)
{
  FILE * fp;
  char * dir;

  dir = tr_dirname (path);
  tr_mkdirp (dir, 0700);
  tr_free (dir);

  tr_remove (path);
  fp = fopen (path, "w+");
  fprintf (fp, "%s", contents);
  fclose (fp);

  sync ();
}

static bool
address_is_blocked (tr_session * session, const char * address_str)
{
  struct tr_address addr;
  tr_address_from_string (&addr, address_str);
  return tr_sessionIsAddressBlocked (session, &addr);
}

static int
test_parsing (void)
{
  char * path;
  tr_session * session;

  /* init the session */
  session = libttest_session_init (NULL);
  check (!tr_blocklistExists (session));
  check_int_eq (0, tr_blocklistGetRuleCount (session));

  /* init the blocklist */
  path = tr_buildPath (tr_sessionGetConfigDir(session), "blocklists", "level1", NULL);
  create_text_file (path, contents1);
  tr_free (path);
  tr_sessionReloadBlocklists (session);
  check (tr_blocklistExists (session));
  check_int_eq (4, tr_blocklistGetRuleCount (session));

  /* enable the blocklist */
  check (!tr_blocklistIsEnabled (session));
  tr_blocklistSetEnabled (session, true);
  check (tr_blocklistIsEnabled (session));

  /* test blocked addresses */
  check (!address_is_blocked (session, "216.16.1.143"));
  check ( address_is_blocked (session, "216.16.1.144"));
  check ( address_is_blocked (session, "216.16.1.145"));
  check ( address_is_blocked (session, "216.16.1.146"));
  check ( address_is_blocked (session, "216.16.1.147"));
  check ( address_is_blocked (session, "216.16.1.148"));
  check ( address_is_blocked (session, "216.16.1.149"));
  check ( address_is_blocked (session, "216.16.1.150"));
  check ( address_is_blocked (session, "216.16.1.151"));
  check (!address_is_blocked (session, "216.16.1.152"));
  check (!address_is_blocked (session, "216.16.1.153"));
  check (!address_is_blocked (session, "217.0.0.1"));
  check (!address_is_blocked (session, "255.0.0.1"));

  /* cleanup */
  libttest_session_close (session);
  return 0;
}

/***
****
***/

static int
test_updating (void)
{
  char * path;
  tr_session * session;

  /* init the session */
  session = libttest_session_init (NULL);
  path = tr_buildPath (tr_sessionGetConfigDir(session), "blocklists", "level1", NULL);

  /* no blocklist to start with... */
  check_int_eq (0, tr_blocklistGetRuleCount (session));

  /* test that updated source files will get loaded */
  create_text_file (path, contents1);
  tr_sessionReloadBlocklists (session);
  check_int_eq (4, tr_blocklistGetRuleCount (session));

  /* test that updated source files will get loaded */
  create_text_file (path, contents2);
  tr_sessionReloadBlocklists (session);
  check_int_eq (5, tr_blocklistGetRuleCount (session));

  /* test that updated source files will get loaded */
  create_text_file (path, contents1);
  tr_sessionReloadBlocklists (session);
  check_int_eq (4, tr_blocklistGetRuleCount (session));

  /* ensure that new files, if bad, get skipped */
  create_text_file (path,  "# nothing useful\n");
  tr_sessionReloadBlocklists (session);
  check_int_eq (4, tr_blocklistGetRuleCount (session));

  /* cleanup */
  libttest_session_close (session);
  tr_free (path);
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

  return runTests (tests, NUM_TESTS (tests));
}
