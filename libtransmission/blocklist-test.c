#include <stdio.h>
#include "transmission.h"
#include "blocklist.h"
#include "net.h"
#include "utils.h"

#include "libtransmission-test.h"

#ifndef WIN32
    #define TEMPDIR_PREFIX "/tmp/"
#else
    #define TEMPDIR_PREFIX 
#endif

#define TEMPFILE_TXT  TEMPDIR_PREFIX "transmission-blocklist-test.txt"
#define TEMPFILE_BIN  TEMPDIR_PREFIX "transmission-blocklist-test.bin"

static void
createTestBlocklist (const char * tmpfile)
{
    const char * lines[] = { "Austin Law Firm:216.16.1.144-216.16.1.151",
                             "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255",
                             "Corel Corporation:216.21.157.192-216.21.157.223",
                             "Fox Speed Channel:216.79.131.192-216.79.131.223" };
    FILE *       out;
    int          i;
    const int    lineCount = sizeof (lines) / sizeof (lines[0]);

    /* create the ascii file to feed to libtransmission */
    out = fopen (tmpfile, "w+");
    for (i = 0; i < lineCount; ++i)
        fprintf (out, "%s\n", lines[i]);
    fclose (out);
}

static int
testBlockList (void)
{
    const char * tmpfile_txt = TEMPFILE_TXT;
    const char * tmpfile_bin = TEMPFILE_BIN;
    struct tr_address addr;
    tr_blocklistFile * b;

    remove (tmpfile_txt);
    remove (tmpfile_bin);

    b = tr_blocklistFileNew (tmpfile_bin, true);
    createTestBlocklist (tmpfile_txt);
    tr_blocklistFileSetContent (b, tmpfile_txt);

    /* now run some tests */
    check (tr_address_from_string (&addr, "216.16.1.143"));
    check (!tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "216.16.1.144"));
    check (tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "216.16.1.145"));
    check (tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "216.16.1.146"));
    check (tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "216.16.1.147"));
    check (tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "216.16.1.148"));
    check (tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "216.16.1.149"));
    check (tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "216.16.1.150"));
    check (tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "216.16.1.151"));
    check (tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "216.16.1.152"));
    check (!tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "216.16.1.153"));
    check (!tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "217.0.0.1"));
    check (!tr_blocklistFileHasAddress (b, &addr));
    check (tr_address_from_string (&addr, "255.0.0.1"));

    /* cleanup */
    tr_blocklistFileFree (b);
    remove (tmpfile_txt);
    remove (tmpfile_bin);
    return 0;
}

MAIN_SINGLE_TEST (testBlockList)

