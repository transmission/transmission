// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <condition_variable>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

#include <curl/curl.h>

#include <event2/buffer.h>

#include <fmt/core.h>
#include <fmt/format.h>

#include "crypto-utils.h"
#include "log.h"
#include "tr-assert.h"
#include "utils.h"
#include "web.h"

using namespace std::literals;

#if LIBCURL_VERSION_NUM >= 0x070F06 // CURLOPT_SOCKOPT* was added in 7.15.6
#define USE_LIBCURL_SOCKOPT
#endif

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

    static auto constexpr SysStoreNames = std::array<LPCWSTR, 2>{
        L"CA",
        L"ROOT",
    };

    for (auto& sys_store_name : SysStoreNames)
    {
        HCERTSTORE const sys_cert_store = CertOpenSystemStoreW(0, sys_store_name);
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
    explicit Impl(Mediator& mediator_in)
        : mediator{ mediator_in }
    {
        std::call_once(curl_init_flag, curlInit);

        if (auto bundle = tr_env_get_string("CURL_CA_BUNDLE"); !std::empty(bundle))
        {
            curl_ca_bundle = std::move(bundle);
        }

        shareEverything();

        if (curl_ssl_verify)
        {
            auto const* bundle = std::empty(curl_ca_bundle) ? "none" : curl_ca_bundle.c_str();
            tr_logAddInfo(
                fmt::format(_("Will verify tracker certs using envvar CURL_CA_BUNDLE: {bundle}"), fmt::arg("bundle", bundle)));
            tr_logAddInfo(_("NB: this only works if you built against libcurl with openssl or gnutls, NOT nss"));
            tr_logAddInfo(_("NB: Invalid certs will appear as 'Could not connect to tracker' like many other errors"));
        }

        if (auto const& file = mediator.cookieFile(); file)
        {
            this->cookie_file = *file;
        }

        if (auto const& ua = mediator.userAgent(); ua)
        {
            this->user_agent = *ua;
        }

        auto const lock = std::unique_lock(queued_tasks_mutex);
        curl_thread = std::make_unique<std::thread>(curlThreadFunc, this);
    }

    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;

    ~Impl()
    {
        run_mode = RunMode::CloseNow;
        queued_tasks_cv.notify_one();
        curl_thread->join();
    }

    void closeSoon()
    {
        run_mode = RunMode::CloseSoon;
        queued_tasks_cv.notify_one();
    }

    [[nodiscard]] bool isClosed() const noexcept
    {
        return is_closed_;
    }

    void fetch(FetchOptions&& options)
    {
        if (run_mode != RunMode::Run)
        {
            return;
        }

        auto const lock = std::unique_lock(queued_tasks_mutex);
        queued_tasks.emplace_back(new Task{ *this, std::move(options) });
        queued_tasks_cv.notify_one();
    }

private:
    class Task
    {
    private:
        std::shared_ptr<evbuffer> const privbuf{ evbuffer_new(), evbuffer_free };
        std::shared_ptr<CURL> const easy_handle{ curl_easy_init(), curl_easy_cleanup };
        tr_web::FetchOptions options;

    public:
        Task(tr_web::Impl& impl_in, tr_web::FetchOptions&& options_in)
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

        [[nodiscard]] auto ipProtocol() const
        {
            switch (options.ip_proto)
            {
            case FetchOptions::IPProtocol::V4:
                return CURL_IPRESOLVE_V4;
            case FetchOptions::IPProtocol::V6:
                return CURL_IPRESOLVE_V6;
            default:
                return CURL_IPRESOLVE_WHATEVER;
            }
        }

        void done()
        {
            if (options.done_func == nullptr)
            {
                return;
            }

            response.body.assign(reinterpret_cast<char const*>(evbuffer_pullup(body(), -1)), evbuffer_get_length(body()));
            impl.mediator.run(std::move(options.done_func), std::move(this->response));
        }

        tr_web::Impl& impl;
        tr_web::FetchResponse response;
    };

    static auto constexpr BandwidthPauseMsec = long{ 500 };
    static auto constexpr DnsCacheTimeoutSecs = long{ 60 * 60 };
    static auto constexpr MaxRedirects = long{ 10 };

    bool const curl_verbose = tr_env_key_exists("TR_CURL_VERBOSE");
    bool const curl_ssl_verify = !tr_env_key_exists("TR_CURL_SSL_NO_VERIFY");
    bool const curl_proxy_ssl_verify = !tr_env_key_exists("TR_CURL_PROXY_SSL_NO_VERIFY");

    Mediator& mediator;

    std::string curl_ca_bundle;

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

    static size_t onDataReceived(void* data, size_t size, size_t nmemb, void* vtask)
    {
        size_t const bytes_used = size * nmemb;
        auto* task = static_cast<Task*>(vtask);
        TR_ASSERT(std::this_thread::get_id() == task->impl.curl_thread->get_id());

        if (auto const& tag = task->speedLimitTag(); tag)
        {
            // If this is more bandwidth than is allocated for this tag,
            // then pause the torrent for a tick. curl will deliver `data`
            // again when the transfer is unpaused.
            if (task->impl.mediator.clamp(*tag, bytes_used) < bytes_used)
            {
                task->impl.paused_easy_handles.emplace(tr_time_msec(), task->easy());
                return CURL_WRITEFUNC_PAUSE;
            }

            task->impl.mediator.notifyBandwidthConsumed(*tag, bytes_used);
        }

        evbuffer_add(task->body(), data, bytes_used);
        tr_logAddTrace(fmt::format("wrote {} bytes to task {}'s buffer", bytes_used, fmt::ptr(task)));
        return bytes_used;
    }

#ifdef USE_LIBCURL_SOCKOPT
    static int onSocketCreated(void* vtask, curl_socket_t fd, curlsocktype /*purpose*/)
    {
        auto const* const task = static_cast<Task const*>(vtask);
        TR_ASSERT(std::this_thread::get_id() == task->impl.curl_thread->get_id());

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

    static void initEasy(tr_web::Impl* impl, Task* task)
    {
        TR_ASSERT(std::this_thread::get_id() == impl->curl_thread->get_id());
        auto* const e = task->easy();

        (void)curl_easy_setopt(e, CURLOPT_SHARE, impl->shared());
        (void)curl_easy_setopt(e, CURLOPT_DNS_CACHE_TIMEOUT, DnsCacheTimeoutSecs);
        (void)curl_easy_setopt(e, CURLOPT_AUTOREFERER, 1L);
        (void)curl_easy_setopt(e, CURLOPT_ENCODING, "");
        (void)curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
        (void)curl_easy_setopt(e, CURLOPT_MAXREDIRS, -1L);
        (void)curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
        (void)curl_easy_setopt(e, CURLOPT_PRIVATE, task);
        (void)curl_easy_setopt(e, CURLOPT_IPRESOLVE, task->ipProtocol());

#ifdef USE_LIBCURL_SOCKOPT
        (void)curl_easy_setopt(e, CURLOPT_SOCKOPTFUNCTION, onSocketCreated);
        (void)curl_easy_setopt(e, CURLOPT_SOCKOPTDATA, task);
#endif

        if (!impl->curl_ssl_verify)
        {
            (void)curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 0L);
            (void)curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 0L);
        }
        else if (!std::empty(impl->curl_ca_bundle))
        {
            (void)curl_easy_setopt(e, CURLOPT_CAINFO, impl->curl_ca_bundle.c_str());
        }
        else
        {
            (void)curl_easy_setopt(e, CURLOPT_SSL_CTX_FUNCTION, ssl_context_func);
        }

        if (!impl->curl_proxy_ssl_verify)
        {
            (void)curl_easy_setopt(e, CURLOPT_PROXY_SSL_VERIFYHOST, 0L);
            (void)curl_easy_setopt(e, CURLOPT_PROXY_SSL_VERIFYPEER, 0L);
        }
        else if (!std::empty(impl->curl_ca_bundle))
        {
            (void)curl_easy_setopt(e, CURLOPT_PROXY_CAINFO, impl->curl_ca_bundle.c_str());
        }

        if (auto const& ua = impl->user_agent; !std::empty(ua))
        {
            (void)curl_easy_setopt(e, CURLOPT_USERAGENT, ua.c_str());
        }

        (void)curl_easy_setopt(e, CURLOPT_TIMEOUT, task->timeoutSecs());
        (void)curl_easy_setopt(e, CURLOPT_URL, task->url().c_str());
        (void)curl_easy_setopt(e, CURLOPT_VERBOSE, impl->curl_verbose ? 1L : 0L);
        (void)curl_easy_setopt(e, CURLOPT_WRITEDATA, task);
        (void)curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, onDataReceived);
        (void)curl_easy_setopt(e, CURLOPT_MAXREDIRS, MaxRedirects);

        if (auto const addrstr = impl->mediator.publicAddress(); addrstr)
        {
            (void)curl_easy_setopt(e, CURLOPT_INTERFACE, addrstr->c_str());
        }

        if (auto const& cookies = task->cookies(); cookies)
        {
            (void)curl_easy_setopt(e, CURLOPT_COOKIE, cookies->c_str());
        }

        if (auto const& file = impl->cookie_file; !std::empty(file))
        {
            (void)curl_easy_setopt(e, CURLOPT_COOKIEFILE, file.c_str());
        }

        if (auto const& range = task->range(); range)
        {
            /* don't bother asking the server to compress webseed fragments */
            (void)curl_easy_setopt(e, CURLOPT_ENCODING, "identity");
            (void)curl_easy_setopt(e, CURLOPT_RANGE, range->c_str());
        }
    }

    void resumePausedTasks()
    {
        TR_ASSERT(std::this_thread::get_id() == curl_thread->get_id());

        auto& paused = paused_easy_handles;
        if (std::empty(paused))
        {
            return;
        }

        auto const now = tr_time_msec();

        for (auto it = std::begin(paused); it != std::end(paused);)
        {
            if (it->first + BandwidthPauseMsec < now)
            {
                curl_easy_pause(it->second, CURLPAUSE_CONT);
                it = paused.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // the thread started by Impl.curl_thread runs this function
    static void curlThreadFunc(Impl* impl)
    {
        auto const multi = std::shared_ptr<CURLM>(curl_multi_init(), curl_multi_cleanup);

        auto running_tasks = int{ 0 };
        auto repeats = unsigned{};
        for (;;)
        {
            if (impl->run_mode == RunMode::CloseNow)
            {
                break;
            }

            if (impl->run_mode == RunMode::CloseSoon && std::empty(impl->queued_tasks) && running_tasks == 0)
            {
                break;
            }

            {
                auto lock = std::unique_lock(impl->queued_tasks_mutex);

                // sleep until there's something to do
                auto const has_work = [&running_tasks, impl]()
                {
                    return running_tasks > 0 || !std::empty(impl->queued_tasks) || impl->run_mode != RunMode::Run;
                };
                if (!has_work())
                {
                    impl->queued_tasks_cv.wait(lock, has_work);
                }

                // add queued tasks
                for (auto* task : impl->queued_tasks)
                {
                    tr_logAddTrace(fmt::format("adding task to curl: '{}'", task->url()));
                    initEasy(impl, task);
                    curl_multi_add_handle(multi.get(), task->easy());
                }
                impl->queued_tasks.clear();
            }

            impl->resumePausedTasks();

            // Adapted from https://curl.se/libcurl/c/curl_multi_wait.html docs.
            // 'numfds' being zero means either a timeout or no file descriptors to
            // wait for. Try timeout on first occurrence, then assume no file
            // descriptors and no file descriptors to wait for means wait for 100
            // milliseconds.
            auto numfds = int{};
            curl_multi_wait(multi.get(), nullptr, 0, 1000, &numfds);
            if (numfds == 0)
            {
                ++repeats;
                if (repeats > 1U)
                {
                    tr_wait_msec(100);
                }
            }
            else
            {
                repeats = 0;
            }

            // nonblocking update of the tasks
            curl_multi_perform(multi.get(), &running_tasks);

            // process any tasks that just finished
            CURLMsg* msg = nullptr;
            auto unused = int{};
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

        // Discard any queued tasks.
        // This shouldn't happen, but do it just in case
        std::for_each(std::begin(impl->queued_tasks), std::end(impl->queued_tasks), [](auto* task) { delete task; });

        impl->is_closed_ = true;
    }

private:
    std::shared_ptr<CURLSH> const curlsh_{ curl_share_init(), curl_share_cleanup };

    std::mutex queued_tasks_mutex;
    std::condition_variable queued_tasks_cv;
    std::list<Task*> queued_tasks;

    CURLSH* shared()
    {
        return curlsh_.get();
    }

    void shareEverything()
    {
        // Tell curl to share whatever it can.
        // https://curl.se/libcurl/c/CURLSHOPT_SHARE.html
        //
        // The user's system probably has a different version of curl than
        // we're compiling with; so instead of listing fields by name, just
        // loop until curl says we've exhausted the list.

        auto* const sh = shared();
        for (long type = CURL_LOCK_DATA_COOKIE;; ++type)
        {
            if (curl_share_setopt(sh, CURLSHOPT_SHARE, type) != CURLSHE_OK)
            {
                tr_logAddDebug(fmt::format("CURLOPT_SHARE ended at {}", type));
                return;
            }
        }
    }

    static std::once_flag curl_init_flag;

    bool is_closed_ = false;

    std::multimap<uint64_t /*tr_time_msec()*/, CURL*> paused_easy_handles;

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

tr_web::tr_web(Mediator& mediator)
    : impl_{ std::make_unique<Impl>(mediator) }
{
}

tr_web::~tr_web() = default;

std::unique_ptr<tr_web> tr_web::create(Mediator& mediator)
{
    return std::unique_ptr<tr_web>(new tr_web(mediator));
}

void tr_web::fetch(FetchOptions&& options)
{
    impl_->fetch(std::move(options));
}

bool tr_web::isClosed() const noexcept
{
    return impl_->isClosed();
}

void tr_web::closeSoon()
{
    impl_->closeSoon();
}
