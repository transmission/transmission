/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cstdlib>
#include <cctype> /* isdigit() */
#include <deque>
#include <cerrno>
#include <cstdlib> /* strtoul() */
#include <cstring> /* strlen(), memchr() */
#include <string_view>
#include <optional>

#include <event2/buffer.h>

#define LIBTRANSMISSION_VARIANT_MODULE

#include "transmission.h"

#include "benc.h"
#include "tr-assert.h"
#include "utils.h" /* tr_snprintf() */
#include "variant-common.h"
#include "variant.h"

using namespace std::literals;

#define MAX_BENC_STR_LENGTH (128 * 1024 * 1024) /* arbitrary */

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
    // find the beginning delimiter
    auto walk = *benc;
    if (std::size(walk) < 3 || walk.front() != 'i')
    {
        return {};
    }

    // find the ending delimiter
    walk.remove_prefix(1);
    auto const pos = walk.find('e');
    if (pos == walk.npos)
    {
        return {};
    }

    // leading zeroes are not allowed
    if ((walk[0] == '0' && isdigit(walk[1])) || (walk[0] == '-' && walk[1] == '0' && isdigit(walk[2])))
    {
        return {};
    }

    errno = 0;
    char* endptr = nullptr;
    auto const value = std::strtoll(std::data(walk), &endptr, 10);
    if (errno != 0 || endptr != std::data(walk) + pos)
    {
        return {};
    }

    walk.remove_prefix(pos + 1);
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
    if (colon_pos == benc->npos)
    {
        return {};
    }

    // get the string length
    errno = 0;
    char* ulend = nullptr;
    auto const len = strtoul(std::data(*benc), &ulend, 10);
    if (errno != 0 || ulend != std::data(*benc) + colon_pos)
    {
        return {};
    }

    // do we have `len` bytes of string data?
    auto walk = *benc;
    walk.remove_prefix(colon_pos + 1);
    if (std::size(walk) < len)
    {
        return {};
    }

    auto const string = walk.substr(0, len);
    walk.remove_prefix(len);
    *benc = walk;
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

    bool Int64(int64_t value) final
    {
        if (tr_variant* variant = get_node(); variant != nullptr)
        {
            tr_variantInitInt(variant, value);
        }

        return true;
    }

    bool String(std::string_view sv) final
    {
        if (tr_variant* variant = get_node(); variant != nullptr)
        {
            if ((parse_opts_ & TR_VARIANT_PARSE_INPLACE) != 0)
            {
                tr_variantInitStrView(variant, sv);
            }
            else
            {
                tr_variantInitStr(variant, sv);
            }
        }

        return true;
    }

    bool StartDict() final
    {
        if (tr_variant* variant = get_node(); variant != nullptr)
        {
            tr_variantInitDict(variant, 0);
            stack_.push_back(variant);
        }

        return true;
    }

    bool Key(std::string_view sv) final
    {
        key_ = tr_quark_new(sv);

        return true;
    }

    bool EndDict() final
    {
        stack_.pop_back();

        return true;
    }

    bool StartArray() final
    {
        if (tr_variant* variant = get_node(); variant != nullptr)
        {
            tr_variantInitList(variant, 0);
            stack_.push_back(variant);
        }

        return true;
    }

    bool EndArray() final
    {
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
        else
        {
            auto* parent = stack_.back();

            if (tr_variantIsList(parent))
            {
                node = tr_variantListAdd(parent);
            }
            else if (key_ && tr_variantIsDict(parent))
            {
                node = tr_variantDictAdd(parent, *key_);
                key_.reset();
            }
        }

        return node;
    }
};

bool tr_variantParseBenc(tr_variant& top, int parse_opts, std::string_view benc, char const** setme_end, tr_error** error)
{
    using Stack = transmission::benc::ParserStack<512>;
    auto stack = Stack{};
    auto handler = MyHandler{ &top, parse_opts };
    bool const ok = transmission::benc::parse(benc, stack, handler, setme_end, error);
    if (!ok)
    {
        tr_variantFree(&top);
    }
    return ok;
}

/****
*****
****/

static void saveIntFunc(tr_variant const* val, void* vevbuf)
{
    auto* evbuf = static_cast<struct evbuffer*>(vevbuf);
    evbuffer_add_printf(evbuf, "i%" PRId64 "e", val->val.i);
}

static void saveBoolFunc(tr_variant const* val, void* vevbuf)
{
    auto* evbuf = static_cast<struct evbuffer*>(vevbuf);
    if (val->val.b)
    {
        evbuffer_add(evbuf, "i1e", 3);
    }
    else
    {
        evbuffer_add(evbuf, "i0e", 3);
    }
}

static void saveRealFunc(tr_variant const* val, void* vevbuf)
{
    auto buf = std::array<char, 64>{};
    int const len = tr_snprintf(std::data(buf), std::size(buf), "%f", val->val.d);

    auto* evbuf = static_cast<evbuffer*>(vevbuf);
    evbuffer_add_printf(evbuf, "%d:", len);
    evbuffer_add(evbuf, std::data(buf), len);
}

static void saveStringFunc(tr_variant const* v, void* vevbuf)
{
    auto sv = std::string_view{};
    (void)!tr_variantGetStrView(v, &sv);

    auto* evbuf = static_cast<struct evbuffer*>(vevbuf);
    evbuffer_add_printf(evbuf, "%zu:", std::size(sv));
    evbuffer_add(evbuf, std::data(sv), std::size(sv));
}

static void saveDictBeginFunc(tr_variant const* /*val*/, void* vevbuf)
{
    auto* evbuf = static_cast<struct evbuffer*>(vevbuf);
    evbuffer_add(evbuf, "d", 1);
}

static void saveListBeginFunc(tr_variant const* /*val*/, void* vevbuf)
{
    auto* evbuf = static_cast<struct evbuffer*>(vevbuf);
    evbuffer_add(evbuf, "l", 1);
}

static void saveContainerEndFunc(tr_variant const* /*val*/, void* vevbuf)
{
    auto* evbuf = static_cast<struct evbuffer*>(vevbuf);
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

void tr_variantToBufBenc(tr_variant const* top, struct evbuffer* buf)
{
    tr_variantWalk(top, &walk_funcs, buf, true);
}
