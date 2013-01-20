
#include <stdio.h>

#include "transmission.h"
#include "libtransmission-test.h"

bool verbose = false;

int current_test = 0;

bool
should_print (bool pass)
{
  if (!pass)
    return true;

  if (verbose)
    return true;

  return false;
#ifdef VERBOSE
  return true;
#else
  return false;
#endif
}

bool
check_condition_impl (const char * file, int line, bool condition)
{
  const bool pass = condition;

  if (should_print (pass))
    fprintf (stderr, "%s %s:%d\n", pass?"PASS":"FAIL", file, line);

  return pass;
}

bool
check_streq_impl (const char * file, int line, const char * expected, const char * actual)
{
  const bool pass = !tr_strcmp0 (expected, actual);

  if (should_print (pass)) {
    if (pass)
      fprintf (stderr, "PASS %s:%d\n", file, line);
    else
      fprintf (stderr, "FAIL %s:%d, expected \"%s\", got \"%s\"\n", file, line, expected?expected:" (null)", actual?actual:" (null)");
  }

  return pass;
}

bool
check_int_eq_impl (const char * file, int line, int64_t expected, int64_t actual)
{
  const bool pass = expected == actual;

  if (should_print (pass)) {
    if (pass)
      fprintf (stderr, "PASS %s:%d\n", file, line);
    else
      fprintf (stderr, "FAIL %s:%d, expected \"%"PRId64"\", got \"%"PRId64"\"\n", file, line, expected, actual);
  }

  return pass;
}

bool
check_ptr_eq_impl (const char * file, int line, const void * expected, const void * actual)
{
  const bool pass = expected == actual;

  if (should_print (pass)) {
    if (pass)
      fprintf (stderr, "PASS %s:%d\n", file, line);
    else
      fprintf (stderr, "FAIL %s:%d, expected \"%p\", got \"%p\"\n", file, line, expected, actual);
  }

  return pass;
}

int
runTests (const testFunc * const tests, int numTests)
{
  int i;
  int ret;

  (void) current_test; /* Use test even if we don't have any tests to run */

  for (i=0; i<numTests; i++)
    if ((ret = (*tests[i])()))
      return ret;

  return 0; /* All tests passed */
}

/***
****
***/

#include <sys/types.h> /* stat(), opendir() */
#include <sys/stat.h> /* stat() */
#include <dirent.h> /* opendir() */
#include <unistd.h> /* getcwd() */

#include <errno.h>
#include <string.h> /* strcmp() */

#include "variant.h"

tr_session * session = NULL;
static char * sandbox = NULL;

static char*
tr_getcwd (void)
{
  char * result;
  char buf[2048];

#ifdef WIN32
  result = _getcwd (buf, sizeof (buf));
#else
  result = getcwd (buf, sizeof (buf));
#endif

  if (result == NULL)
    {
      fprintf (stderr, "getcwd error: \"%s\"", tr_strerror (errno));
      *buf = '\0';
    }

  return tr_strdup (buf);
}

static void
rm_rf (const char * killme)
{
  struct stat sb;

  if (!stat (killme, &sb))
    {
      DIR * odir;

      if (S_ISDIR (sb.st_mode) && ((odir = opendir (killme))))
        {
          struct dirent *d;
          for (d = readdir(odir); d != NULL; d=readdir(odir))
            {
              if (d->d_name && strcmp(d->d_name,".") && strcmp(d->d_name,".."))
                {
                  char * tmp = tr_buildPath (killme, d->d_name, NULL);
                  rm_rf (tmp);
                  tr_free (tmp);
                }
            }
          closedir (odir);
        }

      if (verbose)
        fprintf (stderr, "cleanup: removing %s\n", killme);

      remove (killme);
    }
}

void
libtransmission_test_session_init (void)
{
  char * cwd;
  char * downloadDir;
  tr_variant dict;

  /* create a sandbox for the test session */
  cwd = tr_getcwd ();
  sandbox = tr_buildPath (cwd, "sandbox-XXXXXX", NULL);
  tr_mkdtemp (sandbox);
  downloadDir = tr_buildPath (sandbox, "Downloads", NULL);
  tr_mkdirp (downloadDir, 0700);

  /* create a test session */
  tr_variantInitDict    (&dict, 3);
  tr_variantDictAddStr  (&dict, TR_KEY_download_dir, downloadDir);
  tr_variantDictAddBool (&dict, TR_KEY_port_forwarding_enabled, false);
  tr_variantDictAddBool (&dict, TR_KEY_dht_enabled, false);
  session = tr_sessionInit ("rename-test", sandbox, true, &dict);

  /* cleanup locals*/
  tr_variantFree (&dict);
  tr_free (downloadDir);
  tr_free (cwd);
}

void
libtransmission_test_session_close (void)
{
  tr_sessionClose (session);
  tr_freeMessageList (tr_getQueuedMessages ());
  rm_rf (sandbox);
  tr_free (sandbox);
}
