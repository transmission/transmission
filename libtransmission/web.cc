// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

#include <curl/curl.h>

#include <event2/buffer.h>

#include "crypto-utils.h"
#include "log.h"
#include "utils.h"
#include "web.h"

using namespace std::literals;

#if LIBCURL_VERSION_NUM >= 0x070F06 // CURLOPT_SOCKOPT* was added in 7.15.6
#define USE_LIBCURL_SOCKOPT
#endif

#define dbgmsg(...) tr_logAddDeepNamed("web", __VA_ARGS__)

/***
****
***/

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

/***
****
***/

class tr_web::Impl
{
public:
    Impl(Controller& controller_in)
        : controller{ controller_in }
    {
        std::call_once(curl_init_flag, curlInit);

        if (auto* bundle = tr_env_get_string("CURL_CA_BUNDLE", nullptr); bundle != nullptr)
        {
            curl_ca_bundle = bundle;
            tr_free(bundle);
        }

        if (curl_ssl_verify)
        {
            auto const* bundle = std::empty(curl_ca_bundle) ? "none" : curl_ca_bundle.c_str();
            tr_logAddNamedInfo("web", "will verify tracker certs using envvar CURL_CA_BUNDLE: %s", bundle);
            tr_logAddNamedInfo("web", "NB: this only works if you built against libcurl with openssl or gnutls, NOT nss");
            tr_logAddNamedInfo("web", "NB: Invalid certs will appear as 'Could not connect to tracker' like many other errors");
        }

        if (auto const& file = controller.cookieFile(); file)
        {
            this->cookie_file = *file;
        }

        if (auto const& ua = controller.userAgent(); ua)
        {
            this->user_agent = *ua;
        }

        curl_thread = std::make_unique<std::thread>(tr_webThreadFunc, this);
    }

    ~Impl()
    {
        run_mode = RunMode::CloseNow;
        curl_thread->join();
    }

    void closeSoon()
    {
        run_mode = RunMode::CloseSoon;
    }

    [[nodiscard]] bool isClosed() const
    {
        return is_closed_;
    }

    void run(RunOptions&& options)
    {
        if (run_mode != RunMode::Run)
        {
            return;
        }

        auto const lock = std::unique_lock(web_tasks_mutex);
        auto* const task = new Task{ *this, std::move(options) };
        task->next = tasks;
        tasks = task;
    }

private:
    class Task
    {
    private:
        std::shared_ptr<evbuffer> const privbuf{ evbuffer_new(), evbuffer_free };
        std::shared_ptr<CURL> const easy_handle{ curl_easy_init(), curl_easy_cleanup };
        tr_web::RunOptions const options;

    public:
        Task(tr_web::Impl& impl_in, tr_web::RunOptions&& options_in)
            : options{ std::move(options_in) }
            , impl{ impl_in }
        {
            response.user_data = options.done_func_user_data;
        }

        [[nodiscard]] auto* easy() const
        {
            return easy_handle.get();
        }

        [[nodiscard]] auto* body() const
        {
            return options.buffer != nullptr ? options.buffer : privbuf.get();
        }

        [[nodiscard]] auto const& speedLimitTag() const
        {
            return options.speed_limit_tag;
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

        void done()
        {
            if (options.done_func == nullptr)
            {
                return;
            }

            response.body.assign(reinterpret_cast<char const*>(evbuffer_pullup(body(), -1)), evbuffer_get_length(body()));
            impl.controller.run(options.done_func, std::move(this->response));
        }

        tr_web::Impl& impl;
        tr_web::Response response;
        Task* next = nullptr;
    };

    static auto constexpr ThreadfuncMaxSleepMsec = long{ 200 };

    bool const curl_verbose = tr_env_key_exists("TR_CURL_VERBOSE");
    bool const curl_ssl_verify = !tr_env_key_exists("TR_CURL_SSL_NO_VERIFY");
    bool const curl_proxy_ssl_verify = !tr_env_key_exists("TR_CURL_PROXY_SSL_NO_VERIFY");

    Controller& controller;

    std::string curl_ca_bundle;

    std::recursive_mutex web_tasks_mutex;
    Task* tasks = nullptr;

    std::string cookie_file;
    std::string user_agent;

    std::unique_ptr<std::thread> curl_thread;

    enum class RunMode
    {
        Run,
        CloseSoon, // no new tasks; exit when running tasks finish
        CloseNow // exit now even if tasks are running
    };

    RunMode run_mode = RunMode::Run;

    static size_t onDataReceived(void* ptr, size_t size, size_t nmemb, void* vtask)
    {
        size_t const bytes_used = size * nmemb;
        auto* task = static_cast<Task*>(vtask);

        // Pause the handle for a moment if we exceed our speed limit.
        // Note: do this _before_ evbuffer_add() because curl will save
        // `ptr` and re-send us to it when we unpause
        if (auto const& tag = task->speedLimitTag(); tag)
        {
            task->impl.controller.notifyBandwidthConsumed(*tag, bytes_used);

            if (task->impl.controller.clamp(*tag, 1) == 0)
            {
                task->impl.paused_easy_handles.emplace(tr_time(), task->easy());
                return CURL_WRITEFUNC_PAUSE;
            }
        }

        evbuffer_add(task->body(), ptr, bytes_used);
        dbgmsg("wrote %zu bytes to task %p's buffer", bytes_used, (void*)task);
        return bytes_used;
    }

#ifdef USE_LIBCURL_SOCKOPT
    static int onSocketCreated(void* vtask, curl_socket_t fd, curlsocktype /*purpose*/)
    {
        auto const* const task = static_cast<Task const*>(vtask);

        // Ignore the sockopt() return values -- these are suggestions
        // rather than hard requirements & it's OK for them to fail

        if (auto const& buf = task->sndbuf(); buf)
        {
            (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char const*>(&*buf), sizeof(*buf));
        }
        if (auto const& buf = task->rcvbuf(); buf)
        {
            (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&*buf), sizeof(*buf));
        }

        // return nonzero if this function encountered an error
        return 0;
    }
#endif

    static void initEasy(Controller& controller, tr_web::Impl* impl, Task* task)
    {
        auto* const e = task->easy();

        curl_easy_setopt(e, CURLOPT_AUTOREFERER, 1L);
        curl_easy_setopt(e, CURLOPT_ENCODING, "");
        curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(e, CURLOPT_MAXREDIRS, -1L);
        curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(e, CURLOPT_PRIVATE, task);

#ifdef USE_LIBCURL_SOCKOPT
        curl_easy_setopt(e, CURLOPT_SOCKOPTFUNCTION, onSocketCreated);
        curl_easy_setopt(e, CURLOPT_SOCKOPTDATA, task);
#endif

        if (!impl->curl_ssl_verify)
        {
            curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 0L);
        }
        else if (!std::empty(impl->curl_ca_bundle))
        {
            curl_easy_setopt(e, CURLOPT_CAINFO, impl->curl_ca_bundle.c_str());
        }
        else
        {
            curl_easy_setopt(e, CURLOPT_SSL_CTX_FUNCTION, ssl_context_func);
        }

        if (!impl->curl_proxy_ssl_verify)
        {
            curl_easy_setopt(e, CURLOPT_PROXY_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(e, CURLOPT_PROXY_SSL_VERIFYPEER, 0L);
        }
        else if (!std::empty(impl->curl_ca_bundle))
        {
            curl_easy_setopt(e, CURLOPT_PROXY_CAINFO, impl->curl_ca_bundle.c_str());
        }

        if (auto const& ua = impl->user_agent; !std::empty(ua))
        {
            curl_easy_setopt(e, CURLOPT_USERAGENT, ua.c_str());
        }

        curl_easy_setopt(e, CURLOPT_TIMEOUT, task->timeoutSecs());
        curl_easy_setopt(e, CURLOPT_URL, task->url().c_str());
        curl_easy_setopt(e, CURLOPT_VERBOSE, impl->curl_verbose ? 1L : 0L);
        curl_easy_setopt(e, CURLOPT_WRITEDATA, task);
        curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, onDataReceived);

        if (auto const addrstr = controller.publicAddress(); addrstr)
        {
            (void)curl_easy_setopt(e, CURLOPT_INTERFACE, addrstr->c_str());
        }

        if (auto const& cookies = task->cookies(); !std::empty(cookies))
        {
            (void)curl_easy_setopt(e, CURLOPT_COOKIE, cookies.c_str());
        }

        if (auto const& file = impl->cookie_file; !std::empty(file))
        {
            (void)curl_easy_setopt(e, CURLOPT_COOKIEFILE, file.c_str());
        }

        if (auto const& range = task->range(); !std::empty(range))
        {
            curl_easy_setopt(e, CURLOPT_RANGE, range.c_str());
            /* don't bother asking the server to compress webseed fragments */
            curl_easy_setopt(e, CURLOPT_ENCODING, "identity");
        }
    }

    static void tr_webThreadFunc(void* vimpl)
    {
        auto* impl = static_cast<tr_web::Impl*>(vimpl);

        auto const multi = std::shared_ptr<CURLM>(curl_multi_init(), curl_multi_cleanup);

        auto repeats = unsigned{};
        for (;;)
        {
            if (impl->run_mode == RunMode::CloseNow)
            {
                break;
            }

            if (impl->run_mode == RunMode::CloseSoon && impl->tasks == nullptr)
            {
                break;
            }

            /* add tasks from the queue */
            {
                auto const lock = std::unique_lock(impl->web_tasks_mutex);

                while (impl->tasks != nullptr)
                {
                    /* pop the task */
                    auto* const task = impl->tasks;
                    impl->tasks = task->next;
                    task->next = nullptr;

                    dbgmsg("adding task to curl: [%s]", task->url().c_str());
                    initEasy(impl->controller, impl, task);
                    curl_multi_add_handle(multi.get(), task->easy());
                }
            }

            // resume any paused tasks
            auto& paused = impl->paused_easy_handles;
            auto const now = tr_time();
            for (auto it = std::begin(paused); it != std::end(paused);)
            {
                if (it->first < now)
                {
                    curl_easy_pause(it->second, CURLPAUSE_CONT);
                    it = paused.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            /* 'numfds' being zero means either a timeout or no file descriptors to
                wait for. Try timeout on first occurrence, then assume no file
                descriptors and no file descriptors to wait for means wait for 100
                milliseconds. */
            auto numfds = int{};
            curl_multi_wait(multi.get(), nullptr, 0, 1000, &numfds);
            if (numfds == 0)
            {
                repeats++;
                if (repeats > 1U)
                {
                    tr_wait_msec(100);
                }
            }
            else
            {
                repeats = 0;
            }

            /* call curl_multi_perform() */
            auto unused = int{};
            curl_multi_perform(multi.get(), &unused);

            /* pump completed tasks from the multi */
            CURLMsg* msg = nullptr;
            while ((msg = curl_multi_info_read(multi.get(), &unused)) != nullptr)
            {
                if (msg->msg == CURLMSG_DONE && msg->easy_handle != nullptr)
                {
                    auto* const e = msg->easy_handle;

                    Task* task = nullptr;
                    curl_easy_getinfo(e, CURLINFO_PRIVATE, (void*)&task);

                    auto req_bytes_sent = long{};
                    auto total_time = double{};
                    curl_easy_getinfo(e, CURLINFO_REQUEST_SIZE, &req_bytes_sent);
                    curl_easy_getinfo(e, CURLINFO_TOTAL_TIME, &total_time);
                    curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &task->response.status);
                    task->response.did_connect = task->response.status > 0 || req_bytes_sent > 0;
                    task->response.did_timeout = task->response.status == 0 && total_time >= task->timeoutSecs();
                    curl_multi_remove_handle(multi.get(), e);
                    task->done();
                    delete task;
                }
            }
        }

        /* Discard any remaining tasks.
         * This is rare, but can happen on shutdown with unresponsive trackers. */
        while (impl->tasks != nullptr)
        {
            auto* const task = impl->tasks;
            impl->tasks = task->next;
            dbgmsg("Discarding task \"%s\"", task->url().c_str());
            delete task;
        }

        impl->is_closed_ = true;
    }

private:
    static std::once_flag curl_init_flag;

    bool is_closed_ = false;

    std::multimap<time_t, CURL*> paused_easy_handles;

    static void curlInit()
    {
        // try to enable ssl for https support;
        // but if that fails, try a plain vanilla init
        if (curl_global_init(CURL_GLOBAL_SSL) != CURLE_OK)
        {
            curl_global_init(0);
        }
    }
};

std::once_flag tr_web::Impl::curl_init_flag;

tr_web::tr_web(Controller& controller)
    : impl_{ std::make_unique<Impl>(controller) }
{
}

tr_web::~tr_web() = default;

std::unique_ptr<tr_web> tr_web::create(Controller& controller)
{
    return std::unique_ptr<tr_web>(new tr_web(controller));
}

void tr_web::run(RunOptions&& options)
{
    impl_->run(std::move(options));
}

bool tr_web::isClosed() const
{
    return impl_->isClosed();
}

void tr_web::closeSoon()
{
    impl_->closeSoon();
}
