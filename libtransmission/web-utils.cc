// This file Copyright © 2021-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib> // for strtoul()
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#define PSL_STATIC
#include <libpsl.h>

#include "transmission.h"

#include "log.h"
#include "net.h"
#include "tr-assert.h"
#include "tr-strbuf.h"
#include "utils.h"
#include "web-utils.h"

using namespace std::literals;

// ---

bool tr_addressIsIP(char const* address)
{
    return address != nullptr && tr_address::from_string(address).has_value();
}

char const* tr_webGetResponseStr(long code)
{
    switch (code)
    {
    case 0:
        return "No Response";

    case 101:
        return "Switching Protocols";

    case 200:
        return "OK";

    case 201:
        return "Created";

    case 202:
        return "Accepted";

    case 203:
        return "Non-Authoritative Information";

    case 204:
        return "No Content";

    case 205:
        return "Reset Content";

    case 206:
        return "Partial Content";

    case 300:
        return "Multiple Choices";

    case 301:
        return "Moved Permanently";

    case 302:
        return "Found";

    case 303:
        return "See Other";

    case 304:
        return "Not Modified";

    case 305:
        return "Use Proxy";

    case 306:
        return " (Unused)";

    case 307:
        return "Temporary Redirect";

    case 400:
        return "Bad Request";

    case 401:
        return "Unauthorized";

    case 402:
        return "Payment Required";

    case 403:
        return "Forbidden";

    case 404:
        return "Not Found";

    case 405:
        return "Method Not Allowed";

    case 406:
        return "Not Acceptable";

    case 407:
        return "Proxy Authentication Required";

    case 408:
        return "Request Timeout";

    case 409:
        return "Conflict";

    case 410:
        return "Gone";

    case 411:
        return "Length Required";

    case 412:
        return "Precondition Failed";

    case 413:
        return "Request Entity Too Large";

    case 414:
        return "Request-URI Too Long";

    case 415:
        return "Unsupported Media Type";

    case 416:
        return "Requested Range Not Satisfiable";

    case 417:
        return "Expectation Failed";

    case 421:
        return "Misdirected Request";

    case 500:
        return "Internal Server Error";

    case 501:
        return "Not Implemented";

    case 502:
        return "Bad Gateway";

    case 503:
        return "Service Unavailable";

    case 504:
        return "Gateway Timeout";

    case 505:
        return "HTTP Version Not Supported";

    default:
        return "Unknown Error";
    }
}

// --- URLs

namespace
{

auto parsePort(std::string_view port_sv)
{
    auto const port = tr_parseNum<int>(port_sv);

    using PortLimits = std::numeric_limits<uint16_t>;
    return port && PortLimits::min() <= *port && *port <= PortLimits::max() ? *port : -1;
}

constexpr std::string_view getPortForScheme(std::string_view scheme)
{
    auto constexpr KnownSchemes = std::array<std::pair<std::string_view, std::string_view>, 5>{ {
        { "ftp"sv, "21"sv },
        { "http"sv, "80"sv },
        { "https"sv, "443"sv },
        { "sftp"sv, "22"sv },
        { "udp"sv, "80"sv },
    } };

    for (auto const& [known_scheme, port] : KnownSchemes)
    {
        if (scheme == known_scheme)
        {
            return port;
        }
    }

    return "-1"sv;
}

TR_CONSTEXPR20 bool urlCharsAreValid(std::string_view url)
{
    // rfc2396
    auto constexpr ValidChars = std::string_view{
        "abcdefghijklmnopqrstuvwxyz" // lowalpha
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ" // upalpha
        "0123456789" // digit
        "-_.!~*'()" // mark
        ";/?:@&=+$," // reserved
        "<>#%<\"" // delims
        "{}|\\^[]`" // unwise
    };

    return !std::empty(url) &&
        std::all_of(std::begin(url), std::end(url), [&ValidChars](auto ch) { return tr_strvContains(ValidChars, ch); });
}

bool tr_isValidTrackerScheme(std::string_view scheme)
{
    auto constexpr Schemes = std::array<std::string_view, 3>{ "http"sv, "https"sv, "udp"sv };
    return std::find(std::begin(Schemes), std::end(Schemes), scheme) != std::end(Schemes);
}

bool isAsciiNonUpperCase(std::string_view host)
{
    return std::all_of(
        std::begin(host),
        std::end(host),
        [](unsigned char ch) { return (ch < 128) && (std::isupper(ch) == 0); });
}

// www.example.com -> example
// www.example.co.uk -> example
// 127.0.0.1 -> 127.0.0.1
std::string_view getSiteName(std::string_view host)
{
    // is it empty?
    if (std::empty(host))
    {
        return host;
    }

    // is it an IP?
    if (auto const addr = tr_address::from_string(host); addr)
    {
        return host;
    }

    TR_ASSERT(psl_builtin() != nullptr);
    if (psl_builtin() == nullptr)
    {
        tr_logAddWarn("psl_builtin is null");
        return host;
    }

    // psl needs a zero-terminated hostname
    auto const szhost = tr_urlbuf{ host };

    // is it a registered name?
    if (isAsciiNonUpperCase(host))
    {
        // www.example.co.uk -> example.co.uk
        if (char const* const top = psl_registrable_domain(psl_builtin(), std::data(szhost)); top != nullptr)
        {
            host.remove_prefix(top - std::data(szhost));
        }
    }
    else if (char* lower = nullptr; psl_str_to_utf8lower(std::data(szhost), nullptr, nullptr, &lower) == PSL_SUCCESS)
    {
        // www.example.co.uk -> example.co.uk
        if (char const* const top = psl_registrable_domain(psl_builtin(), lower); top != nullptr)
        {
            host.remove_prefix(top - lower);
        }

        psl_free_string(lower);
    }

    // example.co.uk -> example
    if (auto const dot_pos = host.find('.'); dot_pos != std::string_view::npos)
    {
        host = host.substr(0, dot_pos);
    }

    return host;
}
} // namespace

std::optional<tr_url_parsed_t> tr_urlParse(std::string_view url)
{
    url = tr_strvStrip(url);

    auto parsed = tr_url_parsed_t{};
    parsed.full = url;

    // So many magnet links are malformed, e.g. not escaping text
    // in the display name, that we're better off handling magnets
    // as a special case before even scanning for invalid chars.
    if (auto constexpr MagnetStart = "magnet:?"sv; tr_strvStartsWith(url, MagnetStart))
    {
        parsed.scheme = "magnet"sv;
        parsed.query = url.substr(std::size(MagnetStart));
        return parsed;
    }

    if (!urlCharsAreValid(url))
    {
        return std::nullopt;
    }

    // scheme
    parsed.scheme = tr_strvSep(&url, ':');
    if (std::empty(parsed.scheme))
    {
        return std::nullopt;
    }

    // authority
    // The authority component is preceded by a double slash ("//") and is
    // terminated by the next slash ("/"), question mark ("?"), or number
    // sign ("#") character, or by the end of the URI.
    if (auto key = "//"sv; tr_strvStartsWith(url, key))
    {
        url.remove_prefix(std::size(key));
        auto pos = url.find_first_of("/?#");
        parsed.authority = url.substr(0, pos);
        url = pos == std::string_view::npos ? ""sv : url.substr(pos);

        // A host identified by an Internet Protocol literal address, version 6
        // [RFC3513] or later, is distinguished by enclosing the IP literal
        // within square brackets ("[" and "]").  This is the only place where
        // square bracket characters are allowed in the URI syntax.
        auto remain = parsed.authority;
        if (tr_strvStartsWith(remain, '['))
        {
            remain.remove_prefix(1); // '['
            parsed.host = tr_strvSep(&remain, ']');
            if (tr_strvStartsWith(remain, ':'))
            {
                remain.remove_prefix(1);
            }
        }
        // Not legal by RFC3986 standards, but sometimes users omit
        // square brackets for an IPv6 address with an implicit port
        else if (std::count(std::begin(remain), std::end(remain), ':') > 1U)
        {
            parsed.host = remain;
            remain = ""sv;
        }
        else
        {
            parsed.host = tr_strvSep(&remain, ':');
        }
        parsed.sitename = getSiteName(parsed.host);
        parsed.port = parsePort(!std::empty(remain) ? remain : getPortForScheme(parsed.scheme));
    }

    //  The path is terminated by the first question mark ("?") or
    //  number sign ("#") character, or by the end of the URI.
    auto pos = url.find_first_of("?#");
    parsed.path = url.substr(0, pos);
    url = pos == std::string_view::npos ? ""sv : url.substr(pos);

    // query
    if (tr_strvStartsWith(url, '?'))
    {
        url.remove_prefix(1);
        pos = url.find('#');
        parsed.query = url.substr(0, pos);
        url = pos == std::string_view::npos ? ""sv : url.substr(pos);
    }

    // fragment
    if (tr_strvStartsWith(url, '#'))
    {
        parsed.fragment = url.substr(1);
    }

    return parsed;
}

std::optional<tr_url_parsed_t> tr_urlParseTracker(std::string_view url)
{
    auto const parsed = tr_urlParse(url);
    return parsed && tr_isValidTrackerScheme(parsed->scheme) ? std::make_optional(*parsed) : std::nullopt;
}

bool tr_urlIsValidTracker(std::string_view url)
{
    return !!tr_urlParseTracker(url);
}

bool tr_urlIsValid(std::string_view url)
{
    auto constexpr Schemes = std::array<std::string_view, 5>{ "http"sv, "https"sv, "ftp"sv, "sftp"sv, "udp"sv };
    auto const parsed = tr_urlParse(url);
    return parsed && std::find(std::begin(Schemes), std::end(Schemes), parsed->scheme) != std::end(Schemes);
}

std::string tr_urlTrackerLogName(std::string_view url)
{
    if (auto const parsed = tr_urlParse(url); parsed)
    {
        return fmt::format(FMT_STRING("{:s}://{:s}:{:d}"), parsed->scheme, parsed->host, parsed->port);
    }

    // we have an invalid URL, we log the full string
    return std::string{ url };
}

tr_url_query_view::iterator& tr_url_query_view::iterator::operator++()
{
    auto pair = tr_strvSep(&remain, '&');
    keyval.first = tr_strvSep(&pair, '=');
    keyval.second = pair;
    return *this;
}

tr_url_query_view::iterator tr_url_query_view::begin() const
{
    auto it = iterator{};
    it.remain = query;
    ++it;
    return it;
}

std::string tr_urlPercentDecode(std::string_view in)
{
    auto out = std::string{};
    out.reserve(std::size(in));

    for (;;)
    {
        auto pos = in.find('%');
        out += in.substr(0, pos);
        if (pos == std::string_view::npos)
        {
            break;
        }

        in.remove_prefix(pos);
        if (std::size(in) >= 3 && in[0] == '%' && (std::isxdigit(in[1]) != 0) && (std::isxdigit(in[2]) != 0))
        {
            auto hexstr = std::array<char, 3>{ in[1], in[2], '\0' };
            auto const hex = strtoul(std::data(hexstr), nullptr, 16);
            out += char(hex);
            in.remove_prefix(3);
        }
        else
        {
            out += in.front();
            in.remove_prefix(1);
        }
    }

    return out;
}
