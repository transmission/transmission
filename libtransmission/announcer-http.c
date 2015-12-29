/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <limits.h> /* USHRT_MAX */
#include <stdio.h> /* fprintf () */
#include <string.h> /* strchr (), memcmp (), memcpy () */

#include <event2/buffer.h>
#include <event2/http.h> /* for HTTP_OK */

#define __LIBTRANSMISSION_ANNOUNCER_MODULE__

#include "transmission.h"
#include "announcer-common.h"
#include "log.h"
#include "net.h" /* tr_globalIPv6 () */
#include "peer-mgr.h" /* pex */
#include "torrent.h"
#include "trevent.h" /* tr_runInEventThread () */
#include "utils.h"
#include "variant.h"
#include "web.h" /* tr_http_escape () */

#define dbgmsg(name, ...) \
  do \
    { \
      if (tr_logGetDeepEnabled ()) \
        tr_logAddDeep (__FILE__, __LINE__, name, __VA_ARGS__); \
    } \
  while (0)

/****
*****
*****  ANNOUNCE
*****
****/

static const char*
get_event_string (const tr_announce_request * req)
{
    if (req->partial_seed)
        if (req->event != TR_ANNOUNCE_EVENT_STOPPED)
            return "paused";

    return tr_announce_event_get_string (req->event);
}

static char*
announce_url_new (const tr_session * session, const tr_announce_request * req)
{
    const char * str;
    const unsigned char * ipv6;
    struct evbuffer * buf = evbuffer_new ();
    char escaped_info_hash[SHA_DIGEST_LENGTH*3 + 1];

    tr_http_escape_sha1 (escaped_info_hash, req->info_hash);

    evbuffer_expand (buf, 1024);

    evbuffer_add_printf (buf, "%s"
                              "%c"
                              "info_hash=%s"
                              "&peer_id=%*.*s"
                              "&port=%d"
                              "&uploaded=%" PRIu64
                              "&downloaded=%" PRIu64
                              "&left=%" PRIu64
                              "&numwant=%d"
                              "&key=%x"
                              "&compact=1"
                              "&supportcrypto=1",
                              req->url,
                              strchr (req->url, '?') ? '&' : '?',
                              escaped_info_hash,
                              PEER_ID_LEN, PEER_ID_LEN, req->peer_id,
                              req->port,
                              req->up,
                              req->down,
                              req->leftUntilComplete,
                              req->numwant,
                              req->key);

    if (session->encryptionMode == TR_ENCRYPTION_REQUIRED)
        evbuffer_add_printf (buf, "&requirecrypto=1");

    if (req->corrupt)
        evbuffer_add_printf (buf, "&corrupt=%" PRIu64, req->corrupt);

    str = get_event_string (req);
    if (str && *str)
        evbuffer_add_printf (buf, "&event=%s", str);

    str = req->tracker_id_str;
    if (str && *str)
        evbuffer_add_printf (buf, "&trackerid=%s", str);

    /* There are two incompatible techniques for announcing an IPv6 address.
       BEP-7 suggests adding an "ipv6=" parameter to the announce URL,
       while OpenTracker requires that peers announce twice, once over IPv4
       and once over IPv6.

       To be safe, we should do both: add the "ipv6=" parameter and
       announce twice. At any rate, we're already computing our IPv6
       address (for the LTEP handshake), so this comes for free. */

    ipv6 = tr_globalIPv6 ();
    if (ipv6) {
        char ipv6_readable[INET6_ADDRSTRLEN];
        evutil_inet_ntop (AF_INET6, ipv6, ipv6_readable, INET6_ADDRSTRLEN);
        evbuffer_add_printf (buf, "&ipv6=");
        tr_http_escape (buf, ipv6_readable, TR_BAD_SIZE, true);
    }

    return evbuffer_free_to_str (buf, NULL);
}

static tr_pex*
listToPex (tr_variant * peerList, size_t * setme_len)
{
    size_t i;
    size_t n;
    const size_t len = tr_variantListSize (peerList);
    tr_pex * pex = tr_new0 (tr_pex, len);

    for (i=n=0; i<len; ++i)
    {
        int64_t port;
        const char * ip;
        tr_address addr;
        tr_variant * peer = tr_variantListChild (peerList, i);

        if (peer == NULL)
            continue;
        if (!tr_variantDictFindStr (peer, TR_KEY_ip, &ip, NULL))
            continue;
        if (!tr_address_from_string (&addr, ip))
            continue;
        if (!tr_variantDictFindInt (peer, TR_KEY_port, &port))
            continue;
        if ((port < 0) || (port > USHRT_MAX))
            continue;
        if (!tr_address_is_valid_for_peers (&addr, port))
            continue;

        pex[n].addr = addr;
        pex[n].port = htons ((uint16_t)port);
        ++n;
    }

    *setme_len = n;
    return pex;
}

struct announce_data
{
    tr_announce_response response;
    tr_announce_response_func response_func;
    void * response_func_user_data;
    char log_name[128];
};

static void
on_announce_done_eventthread (void * vdata)
{
    struct announce_data * data = vdata;

    if (data->response_func != NULL)
        data->response_func (&data->response, data->response_func_user_data);

    tr_free (data->response.pex6);
    tr_free (data->response.pex);
    tr_free (data->response.tracker_id_str);
    tr_free (data->response.warning);
    tr_free (data->response.errmsg);
    tr_free (data);
}


static void
on_announce_done (tr_session   * session,
                  bool           did_connect,
                  bool           did_timeout,
                  long           response_code,
                  const void   * msg,
                  size_t         msglen,
                  void         * vdata)
{
    tr_announce_response * response;
    struct announce_data * data = vdata;

    response = &data->response;
    response->did_connect = did_connect;
    response->did_timeout = did_timeout;
    dbgmsg (data->log_name, "Got announce response");

    if (response_code != HTTP_OK)
    {
        const char * fmt = _("Tracker gave HTTP response code %1$ld (%2$s)");
        const char * response_str = tr_webGetResponseStr (response_code);
        response->errmsg = tr_strdup_printf (fmt, response_code, response_str);
    }
    else
    {
        tr_variant benc;
        const bool variant_loaded = !tr_variantFromBenc (&benc, msg, msglen);

        if (tr_env_key_exists ("TR_CURL_VERBOSE"))
        {
            if (!variant_loaded)
                fprintf (stderr, "%s", "Announce response was not in benc format\n");
            else {
                size_t i, len;
                char * str = tr_variantToStr (&benc, TR_VARIANT_FMT_JSON, &len);
                fprintf (stderr, "%s", "Announce response:\n< ");
                for (i=0; i<len; ++i)
                    fputc (str[i], stderr);
                fputc ('\n', stderr);
                tr_free (str);
            }
        }

        if (variant_loaded && tr_variantIsDict (&benc))
        {
            int64_t i;
            size_t len;
            tr_variant * tmp;
            const char * str;
            const uint8_t * raw;

            if (tr_variantDictFindStr (&benc, TR_KEY_failure_reason, &str, &len))
                response->errmsg = tr_strndup (str, len);

            if (tr_variantDictFindStr (&benc, TR_KEY_warning_message, &str, &len))
                response->warning = tr_strndup (str, len);

            if (tr_variantDictFindInt (&benc, TR_KEY_interval, &i))
                response->interval = i;

            if (tr_variantDictFindInt (&benc, TR_KEY_min_interval, &i))
                response->min_interval = i;

            if (tr_variantDictFindStr (&benc, TR_KEY_tracker_id, &str, &len))
                response->tracker_id_str = tr_strndup (str, len);

            if (tr_variantDictFindInt (&benc, TR_KEY_complete, &i))
                response->seeders = i;

            if (tr_variantDictFindInt (&benc, TR_KEY_incomplete, &i))
                response->leechers = i;

            if (tr_variantDictFindInt (&benc, TR_KEY_downloaded, &i))
                response->downloads = i;

            if (tr_variantDictFindRaw (&benc, TR_KEY_peers6, &raw, &len)) {
                dbgmsg (data->log_name, "got a peers6 length of %zu", len);
                response->pex6 = tr_peerMgrCompact6ToPex (raw, len,
                                              NULL, 0, &response->pex6_count);
            }

            if (tr_variantDictFindRaw (&benc, TR_KEY_peers, &raw, &len)) {
                dbgmsg (data->log_name, "got a compact peers length of %zu", len);
                response->pex = tr_peerMgrCompactToPex (raw, len,
                                               NULL, 0, &response->pex_count);
            } else if (tr_variantDictFindList (&benc, TR_KEY_peers, &tmp)) {
                response->pex = listToPex (tmp, &response->pex_count);
                dbgmsg (data->log_name, "got a peers list with %zu entries",
                        response->pex_count);
            }
        }

        if (variant_loaded)
            tr_variantFree (&benc);
    }

    tr_runInEventThread (session, on_announce_done_eventthread, data);
}

void
tr_tracker_http_announce (tr_session                 * session,
                          const tr_announce_request  * request,
                          tr_announce_response_func    response_func,
                          void                       * response_func_user_data)
{
    struct announce_data * d;
    char * url = announce_url_new (session, request);

    d = tr_new0 (struct announce_data, 1);
    d->response.seeders = -1;
    d->response.leechers = -1;
    d->response.downloads = -1;
    d->response_func = response_func;
    d->response_func_user_data = response_func_user_data;
    memcpy (d->response.info_hash, request->info_hash, SHA_DIGEST_LENGTH);
    tr_strlcpy (d->log_name, request->log_name, sizeof (d->log_name));

    dbgmsg (request->log_name, "Sending announce to libcurl: \"%s\"", url);
    tr_webRun (session, url, on_announce_done, d);

    tr_free (url);
}

/****
*****
*****  SCRAPE
*****
****/

struct scrape_data
{
    tr_scrape_response response;
    tr_scrape_response_func response_func;
    void * response_func_user_data;
    char log_name[128];
};

static void
on_scrape_done_eventthread (void * vdata)
{
    struct scrape_data * data = vdata;

    if (data->response_func != NULL)
        data->response_func (&data->response, data->response_func_user_data);

    tr_free (data->response.errmsg);
    tr_free (data->response.url);
    tr_free (data);
}

static void
on_scrape_done (tr_session   * session,
                bool           did_connect,
                bool           did_timeout,
                long           response_code,
                const void   * msg,
                size_t         msglen,
                void         * vdata)
{
    tr_scrape_response * response;
    struct scrape_data * data = vdata;

    response = &data->response;
    response->did_connect = did_connect;
    response->did_timeout = did_timeout;
    dbgmsg (data->log_name, "Got scrape response for \"%s\"", response->url);

    if (response_code != HTTP_OK)
    {
        const char * fmt = _("Tracker gave HTTP response code %1$ld (%2$s)");
        const char * response_str = tr_webGetResponseStr (response_code);
        response->errmsg = tr_strdup_printf (fmt, response_code, response_str);
    }
    else
    {
        tr_variant top;
        int64_t intVal;
        tr_variant * files;
        tr_variant * flags;
        size_t len;
        const char * str;
        const bool variant_loaded = !tr_variantFromBenc (&top, msg, msglen);

        if (tr_env_key_exists ("TR_CURL_VERBOSE"))
        {
            if (!variant_loaded)
                fprintf (stderr, "%s", "Scrape response was not in benc format\n");
            else {
                size_t i, len;
                char * str = tr_variantToStr (&top, TR_VARIANT_FMT_JSON, &len);
                fprintf (stderr, "%s", "Scrape response:\n< ");
                for (i=0; i<len; ++i)
                    fputc (str[i], stderr);
                fputc ('\n', stderr);
                tr_free (str);
            }
        }

        if (variant_loaded)
        {
            if (tr_variantDictFindStr (&top, TR_KEY_failure_reason, &str, &len))
                response->errmsg = tr_strndup (str, len);

            if (tr_variantDictFindDict (&top, TR_KEY_flags, &flags))
                if (tr_variantDictFindInt (flags, TR_KEY_min_request_interval, &intVal))
                    response->min_request_interval = intVal;

            if (tr_variantDictFindDict (&top, TR_KEY_files, &files))
            {
                int i = 0;

                for (;;)
                {
                    int j;
                    tr_quark key;
                    tr_variant * val;

                    /* get the next "file" */
                    if (!tr_variantDictChild (files, i++, &key, &val))
                        break;

                    /* populate the corresponding row in our response array */
                    for (j=0; j<response->row_count; ++j)
                    {
                        struct tr_scrape_response_row * row = &response->rows[j];
                        if (!memcmp (tr_quark_get_string(key,NULL), row->info_hash, SHA_DIGEST_LENGTH))
                        {
                            if (tr_variantDictFindInt (val, TR_KEY_complete, &intVal))
                                row->seeders = intVal;
                            if (tr_variantDictFindInt (val, TR_KEY_incomplete, &intVal))
                                row->leechers = intVal;
                            if (tr_variantDictFindInt (val, TR_KEY_downloaded, &intVal))
                                row->downloads = intVal;
                            if (tr_variantDictFindInt (val, TR_KEY_downloaders, &intVal))
                                row->downloaders = intVal;
                            break;
                        }
                    }
                }
            }

            tr_variantFree (&top);
        }
    }

    tr_runInEventThread (session, on_scrape_done_eventthread, data);
}

static char *
scrape_url_new (const tr_scrape_request * req)
{
    int i;
    char delimiter;
    struct evbuffer * buf = evbuffer_new ();

    evbuffer_add_printf (buf, "%s", req->url);
    delimiter = strchr (req->url, '?') ? '&' : '?';
    for (i=0; i<req->info_hash_count; ++i)
    {
        char str[SHA_DIGEST_LENGTH*3 + 1];
        tr_http_escape_sha1 (str, req->info_hash[i]);
        evbuffer_add_printf (buf, "%cinfo_hash=%s", delimiter, str);
        delimiter = '&';
    }

    return evbuffer_free_to_str (buf, NULL);
}

void
tr_tracker_http_scrape (tr_session               * session,
                        const tr_scrape_request  * request,
                        tr_scrape_response_func    response_func,
                        void                     * response_func_user_data)
{
    int i;
    struct scrape_data * d;
    char * url = scrape_url_new (request);

    d = tr_new0 (struct scrape_data, 1);
    d->response.url = tr_strdup (request->url);
    d->response_func = response_func;
    d->response_func_user_data = response_func_user_data;
    d->response.row_count = request->info_hash_count;
    for (i=0; i<d->response.row_count; ++i)
    {
        memcpy (d->response.rows[i].info_hash, request->info_hash[i], SHA_DIGEST_LENGTH);
        d->response.rows[i].seeders = -1;
        d->response.rows[i].leechers = -1;
        d->response.rows[i].downloads = -1;
    }
    tr_strlcpy (d->log_name, request->log_name, sizeof (d->log_name));

    dbgmsg (request->log_name, "Sending scrape to libcurl: \"%s\"", url);
    tr_webRun (session, url, on_scrape_done, d);

    tr_free (url);
}
