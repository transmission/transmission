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
private:
    std::shared_ptr<evbuffer> const privbuf{ evbuffer_new(), evbuffer_free };
    std::shared_ptr<CURL> const easy_handle{ curl_easy_init(), curl_easy_cleanup };
    tr_web_options const options;

public:
    tr_web_task(tr_session* session_in, tr_web_options&& options_in)
        : options{ std::move(options_in) }
        , session{ session_in }
    {
    }

    [[nodiscard]] auto* easy() const
    {
        return easy_handle.get();
    }

    [[nodiscard]] auto* response() const
    {
        return options.buffer != nullptr ? options.buffer : privbuf.get();
    }

    [[nodiscard]] auto const& torrent_id() const
    {
        return options.torrent_id;
    }

    [[nodiscard]] auto const& url() const
    {
        return options.url;
    }

    [[nodiscard]] auto const& range() const
    {
        return options.range;
    }

    [[nodiscard]] auto const& cookies() const
    {
        return options.cookies;
    }

    [[nodiscard]] auto const& sndbuf() const
    {
        return options.sndbuf;
    }

    [[nodiscard]] auto const& rcvbuf() const
    {
        return options.rcvbuf;
    }

    [[nodiscard]] auto const& timeoutSecs() const
    {
        return options.timeout_secs;
    }

    void done() const
    {
        if (options.done_func == nullptr)
        {
            return;
        }

        auto const sv = std::string_view{ reinterpret_cast<char const*>(evbuffer_pullup(response(), -1)),
                                          evbuffer_get_length(response()) };
        options.done_func(session, did_connect, did_timeout, response_code, sv, options.done_func_user_data);
    }

    tr_session* const session;

    tr_web_task* next = nullptr;

    long response_code = 0;
    bool did_connect = false;
    bool did_timeout = false;
};

/***
****
***/

struct tr_web
{
    bool const curl_verbose = tr_env_key_exists("TR_CURL_VERBOSE");
    bool const curl_ssl_verify = !tr_env_key_exists("TR_CURL_SSL_NO_VERIFY");
    bool const curl_proxy_ssl_verify = !tr_env_key_exists("TR_CURL_PROXY_SSL_NO_VERIFY");

    char* curl_ca_bundle;
    int close_mode = ~0;

    std::recursive_mutex web_tasks_mutex;
    tr_web_task* tasks = nullptr;

    std::string cookie_filename;
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
    if (auto const& torrent_id = task->torrent_id(); torrent_id)
    {
        tr_torrent const* const tor = tr_torrentFindFromId(task->session, *torrent_id);

        if (tor != nullptr && tor->bandwidth->clamp(TR_DOWN, nmemb) == 0)
        {
            task->session->web->paused_easy_handles.insert(task->easy());
            return CURL_WRITEFUNC_PAUSE;
        }
    }

    evbuffer_add(task->response(), ptr, byteCount);
    dbgmsg("wrote %zu bytes to task %p's buffer", byteCount, (void*)task);
    return byteCount;
}

#ifdef USE_LIBCURL_SOCKOPT

static int sockoptfunction(void* vtask, curl_socket_t fd, curlsocktype /*purpose*/)
{
    auto const* const task = static_cast<tr_web_task const*>(vtask);

    // Ignore the sockopt() return values -- these are suggestions
    // rather than hard requirements & it's OK for them to fail

    if (auto const& buf = task->sndbuf(); buf)
    {
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &*buf, sizeof(*buf));
    }
    if (auto const& buf = task->rcvbuf(); buf)
    {
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &*buf, sizeof(&*buf));
    }

    // return nonzero if this function encountered an error
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

static void initEasy(tr_session* s, struct tr_web* web, struct tr_web_task* task)
{
    auto* const e = task->easy();

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

    if (!web->curl_ssl_verify)
    {
        curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 0L);
    }
    else if (web->curl_ca_bundle != nullptr)
    {
        curl_easy_setopt(e, CURLOPT_CAINFO, web->curl_ca_bundle);
    }
    else
    {
        curl_easy_setopt(e, CURLOPT_SSL_CTX_FUNCTION, ssl_context_func);
    }

    if (!web->curl_proxy_ssl_verify)
    {
        curl_easy_setopt(e, CURLOPT_PROXY_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(e, CURLOPT_PROXY_SSL_VERIFYPEER, 0L);
    }
    else if (web->curl_ca_bundle != nullptr)
    {
        curl_easy_setopt(e, CURLOPT_PROXY_CAINFO, web->curl_ca_bundle);
    }

    curl_easy_setopt(e, CURLOPT_TIMEOUT, task->timeoutSecs());
    curl_easy_setopt(e, CURLOPT_URL, task->url().c_str());
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

    if (auto const& cookies = task->cookies(); !std::empty(cookies))
    {
        (void)curl_easy_setopt(e, CURLOPT_COOKIE, cookies.c_str());
    }

    if (auto const& filename = web->cookie_filename; !std::empty(filename))
    {
        (void)curl_easy_setopt(e, CURLOPT_COOKIEFILE, filename.c_str());
    }

    if (auto const& range = task->range(); !std::empty(range))
    {
        curl_easy_setopt(e, CURLOPT_RANGE, range.c_str());
        /* don't bother asking the server to compress webseed fragments */
        curl_easy_setopt(e, CURLOPT_ENCODING, "identity");
    }
}

static void task_finish_func(void* vtask)
{
    auto* task = static_cast<tr_web_task*>(vtask);
    task->done();
    delete task;
}

/****
*****
****/

static void tr_webThreadFunc(void* vsession);

void tr_webRun(tr_session* session, tr_web_options&& options)
{
    if (session->isClosing())
    {
        return;
    }

    if (session->web == nullptr)
    {
        std::thread(tr_webThreadFunc, session).detach();
        while (session->web == nullptr)
        {
            tr_wait_msec(20);
        }
    }

    auto const lock = std::unique_lock(session->web->web_tasks_mutex);
    auto* const task = new tr_web_task{ session, std::move(options) };
    task->next = session->web->tasks;
    session->web->tasks = task;
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

    auto* const web = new tr_web{};
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
        web->cookie_filename = str;
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
                auto* const task = web->tasks;
                web->tasks = task->next;
                task->next = nullptr;

                dbgmsg("adding task to curl: [%s]", task->url().c_str());
                initEasy(session, web, task);
                curl_multi_add_handle(multi, task->easy());
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
                auto* const e = msg->easy_handle;

                tr_web_task* task = nullptr;
                curl_easy_getinfo(e, CURLINFO_PRIVATE, (void*)&task);
                TR_ASSERT(e == task->curl_easy);

                auto req_bytes_sent = long{};
                auto total_time = double{};
                curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &task->response_code);
                curl_easy_getinfo(e, CURLINFO_REQUEST_SIZE, &req_bytes_sent);
                curl_easy_getinfo(e, CURLINFO_TOTAL_TIME, &total_time);
                task->did_connect = task->response_code > 0 || req_bytes_sent > 0;
                task->did_timeout = task->response_code == 0 && total_time >= task->timeoutSecs();
                curl_multi_remove_handle(multi, e);
                web->paused_easy_handles.erase(e);
                tr_runInEventThread(task->session, task_finish_func, task);
            }
        }
    }

    /* Discard any remaining tasks.
     * This is rare, but can happen on shutdown with unresponsive trackers. */
    while (web->tasks != nullptr)
    {
        auto* const task = web->tasks;
        web->tasks = task->next;
        dbgmsg("Discarding task \"%s\"", task->url().c_str());
        delete task;
    }

    /* cleanup */
    curl_multi_cleanup(multi);
    tr_free(web->curl_ca_bundle);
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
