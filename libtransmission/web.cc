// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

#include <curl/curl.h>

#include <event2/buffer.h>

#include "transmission.h"
#include "crypto-utils.h"
#include "file.h"
#include "log.h"
#include "net.h" /* tr_address */
#include "torrent.h"
#include "session.h"
#include "tr-assert.h"
#include "tr-macros.h"
#include "trevent.h" /* tr_runInEventThread() */
#include "utils.h"
#include "version.h" /* User-Agent */
#include "web.h"

using namespace std::literals;

#if LIBCURL_VERSION_NUM >= 0x070F06 /* CURLOPT_SOCKOPT* was added in 7.15.6 */
#define USE_LIBCURL_SOCKOPT
#endif

static auto constexpr ThreadfuncMaxSleepMsec = int{ 200 };

#define dbgmsg(...) tr_logAddDeepNamed("web", __VA_ARGS__)

/***
****
***/

struct tr_web_task
{
    std::string cookies;
    std::string range;
    std::string url;

    CURL* curl_easy = nullptr;
    evbuffer* freebuf = nullptr;
    evbuffer* response = nullptr;
    tr_session* session = nullptr;
    tr_web_done_func done_func = nullptr;
    tr_web_task* next = nullptr;
    void* done_func_user_data = nullptr;

    long code = 0;
    long timeout_secs = 0;

    int torrentId = 0;

    bool did_connect = false;
    bool did_timeout = false;
};

static void task_free(struct tr_web_task* task)
{
    if (task->freebuf != nullptr)
    {
        evbuffer_free(task->freebuf);
    }

    delete task;
}

/***
****
***/

struct tr_web
{
    bool curl_verbose;
    bool curl_ssl_verify;
    char* curl_ca_bundle;
    int close_mode;

    std::recursive_mutex web_tasks_mutex;
    struct tr_web_task* tasks;

    char* cookie_filename;
    std::set<CURL*> paused_easy_handles;
};

/***
****
***/

static size_t writeFunc(void* ptr, size_t size, size_t nmemb, void* vtask)
{
    size_t const byteCount = size * nmemb;
    auto* task = static_cast<struct tr_web_task*>(vtask);

    /* webseed downloads should be speed limited */
    if (task->torrentId != -1)
    {
        tr_torrent const* const tor = tr_torrentFindFromId(task->session, task->torrentId);

        if (tor != nullptr && tor->bandwidth->clamp(TR_DOWN, nmemb) == 0)
        {
            task->session->web->paused_easy_handles.insert(task->curl_easy);
            return CURL_WRITEFUNC_PAUSE;
        }
    }

    evbuffer_add(task->response, ptr, byteCount);
    dbgmsg("wrote %zu bytes to task %p's buffer", byteCount, (void*)task);
    return byteCount;
}

#ifdef USE_LIBCURL_SOCKOPT

static int sockoptfunction(void* vtask, curl_socket_t fd, curlsocktype /*purpose*/)
{
    auto* task = static_cast<struct tr_web_task*>(vtask);

    /* announce and scrape requests have tiny payloads. */
    if (auto const is_scrape = tr_strvContains(task->url, "scrape"sv), is_announce = tr_strvContains(task->url, "announce"sv);
        is_scrape || is_announce)
    {
        int const sndbuf = is_scrape ? 4096 : 1024;
        int const rcvbuf = is_scrape ? 4096 : 3072;
        /* ignore the sockopt() return values -- these are suggestions
           rather than hard requirements & it's OK for them to fail */
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char const*>(&sndbuf), sizeof(sndbuf));
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&rcvbuf), sizeof(rcvbuf));
    }

    /* return nonzero if this function encountered an error */
    return 0;
}

#endif

static CURLcode ssl_context_func(CURL* /*curl*/, void* ssl_ctx, void* /*user_data*/)
{
    auto const cert_store = tr_ssl_get_x509_store(ssl_ctx);
    if (cert_store == nullptr)
    {
        return CURLE_OK;
    }

#ifdef _WIN32

    curl_version_info_data const* const curl_ver = curl_version_info(CURLVERSION_NOW);
    if (curl_ver->age >= 0 && strncmp(curl_ver->ssl_version, "Schannel", 8) == 0)
    {
        return CURLE_OK;
    }

    static LPCWSTR const sys_store_names[] = {
        L"CA",
        L"ROOT",
    };

    for (size_t i = 0; i < TR_N_ELEMENTS(sys_store_names); ++i)
    {
        HCERTSTORE const sys_cert_store = CertOpenSystemStoreW(0, sys_store_names[i]);
        if (sys_cert_store == nullptr)
        {
            continue;
        }

        PCCERT_CONTEXT sys_cert = nullptr;

        while (true)
        {
            sys_cert = CertFindCertificateInStore(sys_cert_store, X509_ASN_ENCODING, 0, CERT_FIND_ANY, nullptr, sys_cert);
            if (sys_cert == nullptr)
            {
                break;
            }

            tr_x509_cert_t const cert = tr_x509_cert_new(sys_cert->pbCertEncoded, sys_cert->cbCertEncoded);
            if (cert == nullptr)
            {
                continue;
            }

            tr_x509_store_add(cert_store, cert);
            tr_x509_cert_free(cert);
        }

        CertCloseStore(sys_cert_store, 0);
    }

#endif

    return CURLE_OK;
}

static long getTimeoutFromURL(struct tr_web_task const* task)
{
    if (auto const* const session = task->session; session == nullptr || session->isClosed)
    {
        return 20L;
    }

    if (tr_strvContains(task->url, "scrape"sv))
    {
        return 30L;
    }

    if (tr_strvContains(task->url, "announce"sv))
    {
        return 90L;
    }

    return 240L;
}

static CURL* createEasy(tr_session* s, struct tr_web* web, struct tr_web_task* task)
{
    CURL* const e = curl_easy_init();

    task->curl_easy = e;
    task->timeout_secs = getTimeoutFromURL(task);

    curl_easy_setopt(e, CURLOPT_AUTOREFERER, 1L);
    curl_easy_setopt(e, CURLOPT_ENCODING, "");
    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(e, CURLOPT_MAXREDIRS, -1L);
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(e, CURLOPT_PRIVATE, task);

#ifdef USE_LIBCURL_SOCKOPT
    curl_easy_setopt(e, CURLOPT_SOCKOPTFUNCTION, sockoptfunction);
    curl_easy_setopt(e, CURLOPT_SOCKOPTDATA, task);
#endif

    if (web->curl_ssl_verify)
    {
        if (web->curl_ca_bundle != nullptr)
        {
            curl_easy_setopt(e, CURLOPT_CAINFO, web->curl_ca_bundle);
        }
        else
        {
            curl_easy_setopt(e, CURLOPT_SSL_CTX_FUNCTION, ssl_context_func);
        }
    }
    else
    {
        curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 0L);
    }

    curl_easy_setopt(e, CURLOPT_TIMEOUT, task->timeout_secs);
    curl_easy_setopt(e, CURLOPT_URL, task->url.c_str());
    curl_easy_setopt(e, CURLOPT_USERAGENT, TR_NAME "/" SHORT_VERSION_STRING);
    curl_easy_setopt(e, CURLOPT_VERBOSE, (long)(web->curl_verbose ? 1 : 0));
    curl_easy_setopt(e, CURLOPT_WRITEDATA, task);
    curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, writeFunc);

    auto is_default_value = bool{};
    tr_address const* addr = tr_sessionGetPublicAddress(s, TR_AF_INET, &is_default_value);
    if (addr != nullptr && !is_default_value)
    {
        (void)curl_easy_setopt(e, CURLOPT_INTERFACE, tr_address_to_string(addr));
    }

    addr = tr_sessionGetPublicAddress(s, TR_AF_INET6, &is_default_value);
    if (addr != nullptr && !is_default_value)
    {
        (void)curl_easy_setopt(e, CURLOPT_INTERFACE, tr_address_to_string(addr));
    }

    if (!std::empty(task->cookies))
    {
        (void)curl_easy_setopt(e, CURLOPT_COOKIE, task->cookies.c_str());
    }

    if (web->cookie_filename != nullptr)
    {
        (void)curl_easy_setopt(e, CURLOPT_COOKIEFILE, web->cookie_filename);
    }

    if (!std::empty(task->range))
    {
        curl_easy_setopt(e, CURLOPT_RANGE, task->range.c_str());
        /* don't bother asking the server to compress webseed fragments */
        curl_easy_setopt(e, CURLOPT_ENCODING, "identity");
    }

    return e;
}

/***
****
***/

static void task_finish_func(void* vtask)
{
    auto* task = static_cast<struct tr_web_task*>(vtask);
    dbgmsg("finished web task %p; got %ld", (void*)task, task->code);

    if (task->done_func != nullptr)
    {
        auto const sv = std::string_view{ reinterpret_cast<char const*>(evbuffer_pullup(task->response, -1)),
                                          evbuffer_get_length(task->response) };
        (*task->done_func)(task->session, task->did_connect, task->did_timeout, task->code, sv, task->done_func_user_data);
    }

    task_free(task);
}

/****
*****
****/

static void tr_webThreadFunc(void* vsession);

static struct tr_web_task* tr_webRunImpl(
    tr_session* session,
    int torrentId,
    std::string_view url,
    std::string_view range,
    std::string_view cookies,
    tr_web_done_func done_func,
    void* done_func_user_data,
    struct evbuffer* buffer)
{
    struct tr_web_task* task = nullptr;

    if (!session->isClosing())
    {
        if (session->web == nullptr)
        {
            std::thread(tr_webThreadFunc, session).detach();
            while (session->web == nullptr)
            {
                tr_wait_msec(20);
            }
        }

        task = new tr_web_task{};
        task->session = session;
        task->torrentId = torrentId;
        task->url = url;
        task->range = range;
        task->cookies = cookies;
        task->done_func = done_func;
        task->done_func_user_data = done_func_user_data;
        task->response = buffer != nullptr ? buffer : evbuffer_new();
        task->freebuf = buffer != nullptr ? nullptr : task->response;

        auto const lock = std::unique_lock(session->web->web_tasks_mutex);
        task->next = session->web->tasks;
        session->web->tasks = task;
    }

    return task;
}

struct tr_web_task* tr_webRunWithCookies(
    tr_session* session,
    std::string_view url,
    std::string_view cookies,
    tr_web_done_func done_func,
    void* done_func_user_data)
{
    return tr_webRunImpl(session, -1, url, {}, cookies, done_func, done_func_user_data, nullptr);
}

struct tr_web_task* tr_webRun(tr_session* session, std::string_view url, tr_web_done_func done_func, void* done_func_user_data)
{
    return tr_webRunWithCookies(session, url, {}, done_func, done_func_user_data);
}

struct tr_web_task* tr_webRunWebseed(
    tr_torrent* tor,
    std::string_view url,
    std::string_view range,
    tr_web_done_func done_func,
    void* done_func_user_data,
    struct evbuffer* buffer)
{
    return tr_webRunImpl(tor->session, tr_torrentId(tor), url, range, {}, done_func, done_func_user_data, buffer);
}

static void tr_webThreadFunc(void* vsession)
{
    auto* session = static_cast<tr_session*>(vsession);

    /* try to enable ssl for https support; but if that fails,
     * try a plain vanilla init */
    if (curl_global_init(CURL_GLOBAL_SSL) != CURLE_OK)
    {
        curl_global_init(0);
    }

    auto* web = new tr_web{};
    web->close_mode = ~0;
    web->tasks = nullptr;
    web->curl_verbose = tr_env_key_exists("TR_CURL_VERBOSE");
    web->curl_ssl_verify = !tr_env_key_exists("TR_CURL_SSL_NO_VERIFY");
    web->curl_ca_bundle = tr_env_get_string("CURL_CA_BUNDLE", nullptr);

    if (web->curl_ssl_verify)
    {
        tr_logAddNamedInfo(
            "web",
            "will verify tracker certs using envvar CURL_CA_BUNDLE: %s",
            web->curl_ca_bundle == nullptr ? "none" : web->curl_ca_bundle);
        tr_logAddNamedInfo("web", "NB: this only works if you built against libcurl with openssl or gnutls, NOT nss");
        tr_logAddNamedInfo("web", "NB: invalid certs will show up as 'Could not connect to tracker' like many other errors");
    }

    auto const str = tr_strvPath(session->config_dir, "cookies.txt");
    if (tr_sys_path_exists(str.c_str(), nullptr))
    {
        web->cookie_filename = tr_strvDup(str);
    }

    auto* const multi = curl_multi_init();
    session->web = web;

    auto repeats = uint32_t{};
    for (;;)
    {
        if (web->close_mode == TR_WEB_CLOSE_NOW)
        {
            break;
        }

        if (web->close_mode == TR_WEB_CLOSE_WHEN_IDLE && web->tasks == nullptr)
        {
            break;
        }

        /* add tasks from the queue */
        {
            auto const lock = std::unique_lock(web->web_tasks_mutex);

            while (web->tasks != nullptr)
            {
                /* pop the task */
                struct tr_web_task* task = web->tasks;
                web->tasks = task->next;
                task->next = nullptr;

                dbgmsg("adding task to curl: [%s]", task->url.c_str());
                curl_multi_add_handle(multi, createEasy(session, web, task));
            }
        }

        /* resume any paused curl handles.
           swap paused_easy_handles to prevent oscillation
           between writeFunc this while loop */
        auto paused = decltype(web->paused_easy_handles){};
        std::swap(paused, web->paused_easy_handles);
        std::for_each(std::begin(paused), std::end(paused), [](auto* curl) { curl_easy_pause(curl, CURLPAUSE_CONT); });

        /* maybe wait a little while before calling curl_multi_perform() */
        auto msec = long{};
        curl_multi_timeout(multi, &msec);

        if (msec < 0)
        {
            msec = ThreadfuncMaxSleepMsec;
        }

        if (session->isClosed)
        {
            msec = 100; /* on shutdown, call perform() more frequently */
        }

        if (msec > 0)
        {
            if (msec > ThreadfuncMaxSleepMsec)
            {
                msec = ThreadfuncMaxSleepMsec;
            }

            auto numfds = int{};
            curl_multi_wait(multi, nullptr, 0, msec, &numfds);
            if (numfds == 0)
            {
                repeats++;
                if (repeats > 1)
                {
                    /* curl_multi_wait() returns immediately if there are
                     * no fds to wait for, so we need an explicit wait here
                     * to emulate select() behavior */
                    tr_wait_msec(std::min(msec, ThreadfuncMaxSleepMsec / 2L));
                }
            }
            else
            {
                repeats = 0;
            }
        }

        /* call curl_multi_perform() */
        auto mcode = CURLMcode{};
        auto unused = int{};
        do
        {
            mcode = curl_multi_perform(multi, &unused);
        } while (mcode == CURLM_CALL_MULTI_PERFORM);

        /* pump completed tasks from the multi */
        CURLMsg* msg = nullptr;
        while ((msg = curl_multi_info_read(multi, &unused)) != nullptr)
        {
            if (msg->msg == CURLMSG_DONE && msg->easy_handle != nullptr)
            {
                CURL* const e = msg->easy_handle;

                struct tr_web_task* task = nullptr;
                curl_easy_getinfo(e, CURLINFO_PRIVATE, (void*)&task);
                TR_ASSERT(e == task->curl_easy);

                auto req_bytes_sent = long{};
                auto total_time = double{};
                curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &task->code);
                curl_easy_getinfo(e, CURLINFO_REQUEST_SIZE, &req_bytes_sent);
                curl_easy_getinfo(e, CURLINFO_TOTAL_TIME, &total_time);
                task->did_connect = task->code > 0 || req_bytes_sent > 0;
                task->did_timeout = task->code == 0 && total_time >= task->timeout_secs;
                curl_multi_remove_handle(multi, e);
                web->paused_easy_handles.erase(e);
                curl_easy_cleanup(e);
                tr_runInEventThread(task->session, task_finish_func, task);
            }
        }
    }

    /* Discard any remaining tasks.
     * This is rare, but can happen on shutdown with unresponsive trackers. */
    while (web->tasks != nullptr)
    {
        struct tr_web_task* task = web->tasks;
        web->tasks = task->next;
        dbgmsg("Discarding task \"%s\"", task->url.c_str());
        task_free(task);
    }

    /* cleanup */
    curl_multi_cleanup(multi);
    tr_free(web->curl_ca_bundle);
    tr_free(web->cookie_filename);
    delete web;
    session->web = nullptr;
}

void tr_webClose(tr_session* session, tr_web_close_mode close_mode)
{
    if (session->web != nullptr)
    {
        session->web->close_mode = close_mode;

        if (close_mode == TR_WEB_CLOSE_NOW)
        {
            while (session->web != nullptr)
            {
                tr_wait_msec(100);
            }
        }
    }
}

long tr_webGetTaskResponseCode(struct tr_web_task* task)
{
    long code = 0;
    curl_easy_getinfo(task->curl_easy, CURLINFO_RESPONSE_CODE, &code);
    return code;
}

char const* tr_webGetTaskRealUrl(struct tr_web_task* task)
{
    char* url = nullptr;
    curl_easy_getinfo(task->curl_easy, CURLINFO_EFFECTIVE_URL, &url);
    return url;
}
