// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cctype> /* isdigit() */
#include <deque>
#include <string_view>
#include <optional>

#include <event2/buffer.h>

#include <fmt/compile.h>
#include <fmt/format.h>

#define LIBTRANSMISSION_VARIANT_MODULE

#include "transmission.h"

#include "benc.h"
#include "tr-assert.h"
#include "quark.h"
#include "utils.h"
#include "variant-common.h"
#include "variant.h"

using namespace std::literals;

auto constexpr MaxBencStrLength = size_t{ 128 * 1024 * 1024 }; // arbitrary

/***
****
****
***/

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
    if (std::size(walk) < 3 || !tr_strvStartsWith(walk, Prefix))
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
    auto const value = tr_parseNum<int64_t>(walk, &walk);
    if (!value || !tr_strvStartsWith(walk, Suffix))
    {
        return {};
    }

    walk.remove_prefix(std::size(Suffix));
    *benc = walk;
    return *value;
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

    auto const len = tr_parseNum<size_t>(svtmp, &svtmp);
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

/***
****  tr_variantParse()
****  tr_variantLoad()
***/

struct MyHandler : public transmission::benc::Handler
{
    tr_variant* const top_;
    int const parse_opts_;
    std::deque<tr_variant*> stack_;
    std::optional<tr_quark> key_;

    MyHandler(tr_variant* top, int parse_opts)
        : top_{ top }
        , parse_opts_{ parse_opts }
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

        tr_variantInitInt(variant, value);
        return true;
    }

    bool String(std::string_view sv, Context const& /*context*/) final
    {
        auto* const variant = get_node();
        if (variant == nullptr)
        {
            return false;
        }

        if ((parse_opts_ & TR_VARIANT_PARSE_INPLACE) != 0)
        {
            tr_variantInitStrView(variant, sv);
        }
        else
        {
            tr_variantInitStr(variant, sv);
        }

        return true;
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
        else if (auto* parent = stack_.back(); tr_variantIsList(parent))
        {
            node = tr_variantListAdd(parent);
        }
        else if (key_ && tr_variantIsDict(parent))
        {
            node = tr_variantDictAdd(parent, *key_);
            key_.reset();
        }

        return node;
    }
};

bool tr_variantParseBenc(tr_variant& top, int parse_opts, std::string_view benc, char const** setme_end, tr_error** error)
{
    using Stack = transmission::benc::ParserStack<512>;
    auto stack = Stack{};
    auto handler = MyHandler{ &top, parse_opts };
    return transmission::benc::parse(benc, stack, handler, setme_end, error) && std::empty(stack);
}

/****
*****
****/

static void saveIntFunc(tr_variant const* val, void* vevbuf)
{
    auto buf = std::array<char, 64>{};
    auto const out = fmt::format_to(std::data(buf), FMT_COMPILE("i{:d}e"), val->val.i);
    auto* const evbuf = static_cast<evbuffer*>(vevbuf);
    evbuffer_add(evbuf, std::data(buf), static_cast<size_t>(out - std::data(buf)));
}

static void saveBoolFunc(tr_variant const* val, void* vevbuf)
{
    auto* const evbuf = static_cast<evbuffer*>(vevbuf);
    if (val->val.b)
    {
        evbuffer_add(evbuf, "i1e", 3);
    }
    else
    {
        evbuffer_add(evbuf, "i0e", 3);
    }
}

static void saveStringImpl(evbuffer* evbuf, std::string_view sv)
{
    // `${sv.size()}:${sv}`
    auto prefix = std::array<char, 32>{};
    auto out = fmt::format_to(std::data(prefix), FMT_COMPILE("{:d}:"), std::size(sv));
    evbuffer_add(evbuf, std::data(prefix), out - std::data(prefix));
    evbuffer_add(evbuf, std::data(sv), std::size(sv));
}

static void saveStringFunc(tr_variant const* v, void* vevbuf)
{
    auto sv = std::string_view{};
    (void)!tr_variantGetStrView(v, &sv);
    auto* const evbuf = static_cast<evbuffer*>(vevbuf);
    saveStringImpl(evbuf, sv);
}

static void saveRealFunc(tr_variant const* val, void* vevbuf)
{
    // the benc spec doesn't handle floats; save it as a string.

    auto buf = std::array<char, 64>{};
    auto out = fmt::format_to(std::data(buf), FMT_COMPILE("{:f}"), val->val.d);
    auto* const evbuf = static_cast<evbuffer*>(vevbuf);
    saveStringImpl(evbuf, { std::data(buf), static_cast<size_t>(out - std::data(buf)) });
}

static void saveDictBeginFunc(tr_variant const* /*val*/, void* vevbuf)
{
    auto* const evbuf = static_cast<evbuffer*>(vevbuf);
    evbuffer_add(evbuf, "d", 1);
}

static void saveListBeginFunc(tr_variant const* /*val*/, void* vevbuf)
{
    auto* const evbuf = static_cast<evbuffer*>(vevbuf);
    evbuffer_add(evbuf, "l", 1);
}

static void saveContainerEndFunc(tr_variant const* /*val*/, void* vevbuf)
{
    auto* const evbuf = static_cast<evbuffer*>(vevbuf);
    evbuffer_add(evbuf, "e", 1);
}

static struct VariantWalkFuncs const walk_funcs = {
    saveIntFunc, //
    saveBoolFunc, //
    saveRealFunc, //
    saveStringFunc, //
    saveDictBeginFunc, //
    saveListBeginFunc, //
    saveContainerEndFunc, //
};

void tr_variantToBufBenc(tr_variant const* top, evbuffer* buf)
{
    tr_variantWalk(top, &walk_funcs, buf, true);
}
