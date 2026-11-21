// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifdef _WIN32
#include <array>
#include <cstring>

#include <windows.h>
#include <wincrypt.h>
#endif

#include <curl/curl.h>

#ifdef _WIN32
#include "libtransmission/crypto-utils.h"
#endif
#include "libtransmission/curl-tls.h"

namespace
{
#ifdef _WIN32
CURLcode ssl_context_func(CURL* /*curl*/, void* ssl_ctx, void* /*user_data*/)
{
    auto const cert_store = tr_ssl_get_x509_store(ssl_ctx);
    if (cert_store == nullptr)
    {
        return CURLE_OK;
    }

    curl_version_info_data const* const curl_ver = curl_version_info(CURLVERSION_NOW);
    if (curl_ver->age >= 0 && std::strncmp(curl_ver->ssl_version, "Schannel", 8) == 0)
    {
        return CURLE_OK;
    }

    static auto constexpr SysStoreNames = std::array<LPCWSTR, 2>{
        L"CA",
        L"ROOT",
    };

    for (auto& sys_store_name : SysStoreNames)
    {
        auto* const sys_cert_store = CertOpenSystemStoreW(0, sys_store_name);
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

            auto* const cert = tr_x509_cert_new(sys_cert->pbCertEncoded, static_cast<long>(sys_cert->cbCertEncoded));
            if (cert == nullptr)
            {
                continue;
            }

            tr_x509_store_add(cert_store, cert);
            tr_x509_cert_free(cert);
        }

        CertCloseStore(sys_cert_store, 0);
    }

    return CURLE_OK;
}
#endif
} // namespace

void tr_curl_configure_tls(CURL* curl, std::string const& curl_ca_bundle, bool curl_ssl_verify, bool curl_proxy_ssl_verify)
{
    if (!curl_ssl_verify)
    {
#if LIBCURL_VERSION_NUM >= 0x073400 /* 7.52.0 */
        (void)curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        (void)curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
#endif
    }
    else if (!std::empty(curl_ca_bundle))
    {
        (void)curl_easy_setopt(curl, CURLOPT_CAINFO, curl_ca_bundle.c_str());
    }
    else
    {
#ifdef _WIN32
        (void)curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, ssl_context_func);
#endif
    }

    if (!curl_proxy_ssl_verify)
    {
        (void)curl_easy_setopt(curl, CURLOPT_CAINFO, NULL);
        (void)curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
#if LIBCURL_VERSION_NUM >= 0x073400 /* 7.52.0 */
        (void)curl_easy_setopt(curl, CURLOPT_PROXY_SSL_VERIFYHOST, 0L);
        (void)curl_easy_setopt(curl, CURLOPT_PROXY_SSL_VERIFYPEER, 0L);
#endif
    }
    else if (!std::empty(curl_ca_bundle))
    {
#if LIBCURL_VERSION_NUM >= 0x073400 /* 7.52.0 */
        (void)curl_easy_setopt(curl, CURLOPT_PROXY_CAINFO, curl_ca_bundle.c_str());
#endif
    }
}
