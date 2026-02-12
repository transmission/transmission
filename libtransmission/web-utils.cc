// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib> // for strtoul()
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#define PSL_STATIC
#include <libpsl.h>

#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/punycode.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-macros.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/utils.h"
#include "libtransmission/web-utils.h"

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

// --- Character classification helpers ---
[[nodiscard]] constexpr bool is_digit(char ch) noexcept
{
    return ch >= '0' && ch <= '9';
}

[[nodiscard]] constexpr bool is_hex_digit(char ch) noexcept
{
    return is_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

[[nodiscard]] constexpr bool is_alpha(char ch) noexcept
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

[[nodiscard]] constexpr bool is_ldh(char ch) noexcept
{
    return is_alpha(ch) || is_digit(ch) || ch == '-';
}

// --- IPv4 validation ---
// Expects "D.D.D.D" where D is 0-255 with no leading zeros.
[[nodiscard]] constexpr bool is_valid_ipv4(std::string_view str) noexcept
{
    if (str.empty()) [[unlikely]]
    {
        return false;
    }

    int octets = 0;

    while (!str.empty())
    {
        if (octets == 4)
        {
            return false; // too many octets
        }

        // find the end of this octet
        auto const dot_pos = str.find('.');
        auto const octet_str = str.substr(0, dot_pos);

        if (octet_str.empty() || octet_str.size() > 3)
        {
            return false;
        }

        // no leading zeros (except "0" itself)
        if (octet_str.size() > 1 && octet_str.starts_with('0'))
        {
            return false;
        }

        // all digits?
        if (!std::ranges::all_of(octet_str, is_digit))
        {
            return false;
        }

        // parse and range-check
        int value = 0;
        for (char const ch : octet_str)
        {
            value = value * 10 + (ch - '0');
        }
        if (value > 255)
        {
            return false;
        }

        ++octets;

        if (dot_pos == std::string_view::npos)
        {
            str = {};
        }
        else
        {
            str = str.substr(dot_pos + 1);
            // trailing dot with nothing after it is invalid for IPv4
            if (str.empty())
            {
                return false;
            }
        }
    }

    return octets == 4;
}

// --- IPv6 validation ---
// Expects up to 8 groups of 1-4 hex digits separated by ':'.
// Exactly one "::" abbreviation is allowed.
// Does NOT expect surrounding brackets (caller strips them).
[[nodiscard]] constexpr bool is_valid_ipv6(std::string_view str) noexcept
{
    if (str.empty()) [[unlikely]]
    {
        return false;
    }

    int groups = 0;
    bool seen_double_colon = false;

    // handle leading "::"
    if (str.starts_with("::"))
    {
        seen_double_colon = true;
        str.remove_prefix(2);
        if (str.empty())
        {
            return true; // "::" alone is valid (all zeros)
        }
    }
    else if (str.starts_with(':'))
    {
        return false; // single leading colon is invalid
    }

    while (!str.empty())
    {
        // find the end of this group
        auto const colon_pos = str.find(':');
        auto const group_str = str.substr(0, colon_pos);

        // check for "::" at this position
        if (group_str.empty())
        {
            if (seen_double_colon)
            {
                return false; // only one "::" allowed
            }
            seen_double_colon = true;
            str.remove_prefix(1); // skip the second ':'
            if (str.empty())
            {
                break; // trailing "::" is valid
            }
            continue;
        }

        // validate group: 1-4 hex digits
        if (group_str.size() > 4)
        {
            return false;
        }
        if (!std::ranges::all_of(group_str, is_hex_digit))
        {
            return false;
        }

        ++groups;

        if (colon_pos == std::string_view::npos)
        {
            str = {};
        }
        else
        {
            str = str.substr(colon_pos + 1);
            // trailing single colon with nothing after it is invalid
            if (str.empty())
            {
                return false;
            }
        }
    }

    if (seen_double_colon)
    {
        // "::" expands to fill the gap; total groups must be < 8
        return groups < 8;
    }

    return groups == 8;
}

// --- Domain name validation (RFC 1035 / RFC 1123) ---
// - total length <= 253
// - each label <= 63 characters
// - labels contain only [a-zA-Z0-9-]
// - labels do not start or end with hyphen
// - no empty labels (no leading, trailing, or consecutive dots)
// - at least one label must contain a letter
[[nodiscard]] constexpr bool is_valid_domain_name(std::string_view str) noexcept
{
    if (str.empty() || str.size() > 253) [[unlikely]]
    {
        return false;
    }

    // optional trailing dot (FQDN notation) — strip it for validation
    if (str.ends_with('.'))
    {
        str.remove_suffix(1);
        if (str.empty())
        {
            return false;
        }
    }

    bool has_non_digit_label = false;

    while (!str.empty())
    {
        auto const dot_pos = str.find('.');
        auto const label = str.substr(0, dot_pos);

        if (label.empty() || label.size() > 63)
        {
            return false;
        }

        // must not start or end with hyphen
        if (label.starts_with('-') || label.ends_with('-'))
        {
            return false;
        }

        // all characters must be LDH
        if (!std::ranges::all_of(label, is_ldh))
        {
            return false;
        }

        if (!has_non_digit_label && std::ranges::any_of(label, is_alpha))
        {
            has_non_digit_label = true;
        }

        if (dot_pos == std::string_view::npos)
        {
            str = {};
        }
        else
        {
            str = str.substr(dot_pos + 1);
        }
    }

    // at least one label must contain a letter, otherwise it looks
    // like an IPv4 address and should be validated as one.
    return has_non_digit_label;
}

// Returns true if 'host' is a syntactically valid:
//   - Domain name per RFC 1035/1123 (LDH rule)
//   - IPv4 dotted-decimal address
//   - IPv6 address (with or without square brackets)
[[nodiscard]] constexpr bool hostIsValid(std::string_view host) noexcept
{
    if (host.empty()) [[unlikely]]
    {
        return false;
    }

    // bracketed IPv6: "[::1]", "[2001:db8::1]"
    if (host.starts_with('[')) [[unlikely]]
    {
        if (host.size() < 4 || !host.ends_with(']'))
        {
            return false;
        }
        return is_valid_ipv6(host.substr(1, host.size() - 2));
    }

    // try IPv4 first if it looks numeric
    if (is_digit(host.front())) [[likely]]
    {
        if (is_valid_ipv4(host))
        {
            return true;
        }
        // fall through: could be a domain starting with a digit like "3com.com"
    }

    // try bare IPv6 (no brackets) — heuristic: contains ':'
    if (host.find(':') != std::string_view::npos)
    {
        return is_valid_ipv6(host);
    }

    return is_valid_domain_name(host);
}

constexpr std::optional<uint16_t> getPortForScheme(std::string_view scheme)
{
    auto constexpr KnownSchemes = std::array<std::pair<std::string_view, uint16_t>, 5>{ {
        { "ftp"sv, 21U },
        { "http"sv, 80U },
        { "https"sv, 443U },
        { "sftp"sv, 22U },
        { "udp"sv, 80U },
    } };

    for (auto const& [known_scheme, port] : KnownSchemes)
    {
        if (scheme == known_scheme)
        {
            return port;
        }
    }

    return {};
}

constexpr bool urlCharsAreValid(std::string_view url)
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

    return !std::empty(url) && std::ranges::all_of(url, [&ValidChars](auto ch) { return tr_strv_contains(ValidChars, ch); });
}

bool tr_isValidTrackerScheme(std::string_view scheme)
{
    auto constexpr Schemes = std::array<std::string_view, 3>{ "http"sv, "https"sv, "udp"sv };
    return std::ranges::find(Schemes, scheme) != std::ranges::end(Schemes);
}

bool isAsciiNonUpperCase(std::string_view host)
{
    return std::ranges::all_of(host, [](unsigned char ch) { return (ch < 128) && (std::isupper(ch) == 0); });
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

// Not part of the RFC3986 standard, but included for convenience
// when using the result with API that does not accept IPv6 address
// strings that are wrapped in square brackets (e.g. inet_pton())
std::string_view getHostWoBrackets(std::string_view host)
{
    if (tr_strv_starts_with(host, '['))
    {
        host.remove_prefix(1);
        host.remove_suffix(1);
    }
    return host;
}
} // namespace

std::optional<tr_url_parsed_t> tr_urlParse(std::string_view url)
{
    url = tr_strv_strip(url);

    auto parsed = tr_url_parsed_t{};
    parsed.full = url;

    // So many magnet links are malformed, e.g. not escaping text
    // in the display name, that we're better off handling magnets
    // as a special case before even scanning for invalid chars.
    if (auto constexpr MagnetStart = "magnet:?"sv; tr_strv_starts_with(url, MagnetStart))
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
    parsed.scheme = tr_strv_sep(&url, ':');
    if (std::empty(parsed.scheme))
    {
        return std::nullopt;
    }

    // authority
    // The authority component is preceded by a double slash ("//") and is
    // terminated by the next slash ("/"), question mark ("?"), or number
    // sign ("#") character, or by the end of the URI.
    if (auto key = "//"sv; tr_strv_starts_with(url, key))
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
        if (tr_strv_starts_with(remain, '['))
        {
            pos = remain.find(']');
            if (pos == std::string_view::npos)
            {
                return std::nullopt;
            }
            parsed.host = remain.substr(0, pos + 1);
            remain.remove_prefix(pos + 1);
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
            pos = remain.find(':');
            parsed.host = remain.substr(0, pos);
            remain.remove_prefix(std::size(parsed.host));
        }

        // just encode to punycode (RFC 3492), if not needed then this will do nothing
        // for now this allows us to check whether to host *would be* valid if used later
        auto host_idn = punycode::to_ascii(parsed.host);
        if (!hostIsValid(*host_idn))
        {
            return std::nullopt;
        }

        if (std::empty(remain))
        {
            auto const port = getPortForScheme(parsed.scheme);
            if (!port)
            {
                return std::nullopt;
            }
            parsed.port = *port;
        }
        else if (tr_strv_starts_with(remain, ':'))
        {
            remain.remove_prefix(1);
            auto const port = tr_num_parse<uint16_t>(remain);
            if (!port || *port == 0U)
            {
                return std::nullopt;
            }
            parsed.port = *port;
        }
        else
        {
            return std::nullopt;
        }

        parsed.host_wo_brackets = getHostWoBrackets(parsed.host);
        parsed.sitename = getSiteName(parsed.host);
    }

    //  The path is terminated by the first question mark ("?") or
    //  number sign ("#") character, or by the end of the URI.
    auto pos = url.find_first_of("?#");
    parsed.path = url.substr(0, pos);
    url = pos == std::string_view::npos ? ""sv : url.substr(pos);

    // query
    if (tr_strv_starts_with(url, '?'))
    {
        url.remove_prefix(1);
        pos = url.find('#');
        parsed.query = url.substr(0, pos);
        url = pos == std::string_view::npos ? ""sv : url.substr(pos);
    }

    // fragment
    if (tr_strv_starts_with(url, '#'))
    {
        parsed.fragment = url.substr(1);
    }

    return parsed;
}

std::optional<tr_url_parsed_t> tr_urlParseTracker(std::string_view url)
{
    auto const parsed = tr_urlParse(url);
    return parsed && tr_isValidTrackerScheme(parsed->scheme) ? parsed : std::nullopt;
}

bool tr_urlIsValidTracker(std::string_view url)
{
    return !!tr_urlParseTracker(url);
}

bool tr_urlIsValid(std::string_view url)
{
    auto constexpr Schemes = std::array<std::string_view, 5>{ "http"sv, "https"sv, "ftp"sv, "sftp"sv, "udp"sv };
    auto const parsed = tr_urlParse(url);
    return parsed && std::ranges::find(Schemes, parsed->scheme) != std::ranges::end(Schemes);
}

std::string tr_urlTrackerLogName(std::string_view url)
{
    if (auto const parsed = tr_urlParse(url); parsed)
    {
        return fmt::format("{:s}://{:s}:{:d}", parsed->scheme, parsed->host, parsed->port);
    }

    // we have an invalid URL, we log the full string
    return std::string{ url };
}

std::vector<std::pair<std::string_view, std::string_view>> tr_url_parsed_t::query_entries() const
{
    auto tmp = query;
    auto ret = std::vector<std::pair<std::string_view, std::string_view>>{};
    ret.reserve(std::count(std::begin(tmp), std::end(tmp), '&') + 1U);
    while (!std::empty(tmp))
    {
        auto val = tr_strv_sep(&tmp, '&');
        auto key = tr_strv_sep(&val, '=');
        ret.emplace_back(key, val);
    }
    return ret;
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
