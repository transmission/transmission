// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cctype> /* isdigit() */
#include <cstddef> // size_t, std::byte
#include <cstdint> // int64_t
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/compile.h>
#include <fmt/format.h>

#define LIBTRANSMISSION_VARIANT_MODULE

#include "libtransmission/benc.h"
#include "libtransmission/quark.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

using namespace std::literals;

auto constexpr MaxBencStrLength = size_t{ 128 * 1024 * 1024 }; // arbitrary

// ---

namespace transmission::benc::impl
{

/**
 * The initial i and trailing e are beginning and ending delimiters.
 * You can have negative numbers such as i-3e. You cannot prefix the
 * number with a zero such as i04e. However, i0e is valid.
 * Example: i3e represents the integer "3"
 *
 * The maximum number of bit of this integer is unspecified,
 * but to handle it as a signed 64bit integer is mandatory to handle
 * "large files" aka .torrent for more that 4Gbyte
 */
std::optional<int64_t> ParseInt(std::string_view* benc)
{
    auto constexpr Prefix = "i"sv;
    auto constexpr Suffix = "e"sv;

    // find the beginning delimiter
    auto walk = *benc;
    if (std::size(walk) < 3 || !tr_strv_starts_with(walk, Prefix))
    {
        return {};
    }

    // find the ending delimiter
    walk.remove_prefix(std::size(Prefix));
    if (auto const pos = walk.find(Suffix); pos == std::string_view::npos)
    {
        return {};
    }

    // leading zeroes are not allowed
    if ((walk[0] == '0' && (isdigit(static_cast<unsigned char>(walk[1])) != 0)) ||
        (walk[0] == '-' && walk[1] == '0' && (isdigit(static_cast<unsigned char>(walk[2])) != 0)))
    {
        return {};
    }

    // parse the string and make sure the next char is `Suffix`
    auto value = tr_num_parse<int64_t>(walk, &walk);
    if (!value || !tr_strv_starts_with(walk, Suffix))
    {
        return {};
    }

    walk.remove_prefix(std::size(Suffix));
    *benc = walk;
    return value;
}

/**
 * Byte strings are encoded as follows:
 * <string length encoded in base ten ASCII>:<string data>
 * Note that there is no constant beginning delimiter, and no ending delimiter.
 * Example: 4:spam represents the string "spam"
 */
std::optional<std::string_view> ParseString(std::string_view* benc)
{
    // find the ':' delimiter
    auto const colon_pos = benc->find(':');
    if (colon_pos == std::string_view::npos)
    {
        return {};
    }

    // get the string length
    auto svtmp = benc->substr(0, colon_pos);
    if (!std::all_of(std::begin(svtmp), std::end(svtmp), [](auto ch) { return isdigit(static_cast<unsigned char>(ch)) != 0; }))
    {
        return {};
    }

    auto const len = tr_num_parse<size_t>(svtmp, &svtmp);
    if (!len || *len >= MaxBencStrLength)
    {
        return {};
    }

    // do we have `len` bytes of string data?
    svtmp = benc->substr(colon_pos + 1);
    if (std::size(svtmp) < len)
    {
        return {};
    }

    auto const string = svtmp.substr(0, *len);
    *benc = svtmp.substr(*len);
    return string;
}

} // namespace transmission::benc::impl

// ---

namespace
{
namespace parse_helpers
{
struct MyHandler : public transmission::benc::Handler
{
    tr_variant* const top_;
    bool inplace_;
    std::deque<tr_variant*> stack_;
    std::optional<tr_quark> key_;

    MyHandler(tr_variant* top, bool inplace)
        : top_{ top }
        , inplace_{ inplace }
    {
    }

    MyHandler(MyHandler&&) = delete;
    MyHandler(MyHandler const&) = delete;
    MyHandler& operator=(MyHandler&&) = delete;
    MyHandler& operator=(MyHandler const&) = delete;

    ~MyHandler() override = default;

    bool Int64(int64_t value, Context const& /*context*/) final
    {
        auto* const variant = get_node();
        if (variant == nullptr)
        {
            return false;
        }

        *variant = value;
        return true;
    }

    bool String(std::string_view sv, Context const& /*context*/) final
    {
        if (auto* const variant = get_node(); variant != nullptr)
        {
            *variant = inplace_ ? tr_variant::unmanaged_string(sv) : tr_variant{ sv };
            return true;
        }

        return false;
    }

    bool StartDict(Context const& /*context*/) final
    {
        auto* const variant = get_node();
        if (variant == nullptr)
        {
            return false;
        }

        tr_variantInitDict(variant, 0);
        stack_.push_back(variant);
        return true;
    }

    bool Key(std::string_view sv, Context const& /*context*/) final
    {
        key_ = tr_quark_new(sv);

        return true;
    }

    bool EndDict(Context const& /*context*/) final
    {
        if (std::empty(stack_))
        {
            return false;
        }

        stack_.pop_back();
        return true;
    }

    bool StartArray(Context const& /*context*/) final
    {
        auto* const variant = get_node();
        if (variant == nullptr)
        {
            return false;
        }

        tr_variantInitList(variant, 0);
        stack_.push_back(variant);
        return true;
    }

    bool EndArray(Context const& /*context*/) final
    {
        if (std::empty(stack_))
        {
            return false;
        }

        stack_.pop_back();
        return true;
    }

private:
    tr_variant* get_node()
    {
        tr_variant* node = nullptr;

        if (std::empty(stack_))
        {
            node = top_;
        }
        else if (auto* parent = stack_.back(); parent != nullptr && parent->holds_alternative<tr_variant::Vector>())
        {
            node = tr_variantListAdd(parent);
        }
        else if (key_ && parent != nullptr && parent->holds_alternative<tr_variant::Map>())
        {
            node = tr_variantDictAdd(parent, *key_);
            key_.reset();
        }

        return node;
    }
};
} // namespace parse_helpers
} // namespace

std::optional<tr_variant> tr_variant_serde::parse_benc(std::string_view input)
{
    using namespace parse_helpers;
    using Stack = transmission::benc::ParserStack<512>;

    auto top = tr_variant{};
    auto stack = Stack{};
    auto handler = MyHandler{ &top, parse_inplace_ };
    if (transmission::benc::parse(input, stack, handler, &end_, &error_) && std::empty(stack))
    {
        return std::optional<tr_variant>{ std::move(top) };
    }

    return {};
}

// ---

namespace
{
namespace to_string_helpers
{
using OutBuf = fmt::memory_buffer;

[[nodiscard]] auto sorted_entries(tr_variant::Map const& map)
{
    auto entries = std::vector<std::pair<std::string_view, tr_variant const*>>{};
    entries.reserve(map.size());
    for (auto const& [key, child] : map)
    {
        entries.emplace_back(tr_quark_get_string_view(key), &child);
    }

    std::sort(std::begin(entries), std::end(entries));
    return entries;
}

struct BencWriter
{
    void operator()(std::monostate /*unused*/) const
    {
    }

    void operator()(std::nullptr_t) const
    {
        write_string(""sv);
    }

    void operator()(bool val) const
    {
        append_literal(val ? "i1e"sv : "i0e"sv);
    }

    void operator()(int64_t val) const
    {
        write_int(val);
    }

    void operator()(double val) const
    {
        write_real(val);
    }

    void operator()(std::string_view sv) const
    {
        write_string(sv);
    }

    void operator()(tr_variant::Vector const& vec) const
    {
        out_.push_back('l');
        for (auto const& child : vec)
        {
            child.visit(*this);
        }
        out_.push_back('e');
    }

    void operator()(tr_variant::Map const& map) const
    {
        out_.push_back('d');
        auto entries = sorted_entries(map);
        for (auto const& [key, child] : entries)
        {
            write_string(key);
            child->visit(*this);
        }
        out_.push_back('e');
    }

    OutBuf& out_;

private:
    void write_string(std::string_view sv) const
    {
        fmt::format_to(fmt::appender(out_), "{:d}:{:s}", std::size(sv), sv);
    }

    void write_int(int64_t val) const
    {
        fmt::format_to(fmt::appender(out_), "i{:d}e", val);
    }

    void write_real(double val) const
    {
        auto buf = std::array<char, 64>{};
        auto const* const out_ptr = fmt::format_to(std::data(buf), "{:f}", val);
        write_string({ std::data(buf), static_cast<size_t>(out_ptr - std::data(buf)) });
    }

    void append_literal(std::string_view literal) const
    {
        out_.append(std::data(literal), std::data(literal) + std::size(literal));
    }
};

} // namespace to_string_helpers
} // namespace

std::string tr_variant_serde::to_benc_string(tr_variant const& var)
{
    using namespace to_string_helpers;

    auto buf = OutBuf{};
    var.visit(BencWriter{ buf });
    return fmt::to_string(buf);
}
