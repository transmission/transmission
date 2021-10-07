/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <ctype.h> /* isdigit() */
#include <deque>
#include <errno.h>
#include <stdlib.h> /* strtoul() */
#include <string.h> /* strlen(), memchr() */

#include <event2/buffer.h>

#define LIBTRANSMISSION_VARIANT_MODULE

#include "transmission.h"
#include "utils.h" /* tr_snprintf() */
#include "variant.h"
#include "variant-common.h"

#define MAX_BENC_STR_LENGTH (128 * 1024 * 1024) /* arbitrary */

/***
****  tr_variantParse()
****  tr_variantLoad()
***/

/**
 * The initial i and trailing e are beginning and ending delimiters.
 * You can have negative numbers such as i-3e. You cannot prefix the
 * number with a zero such as i04e. However, i0e is valid.
 * Example: i3e represents the integer "3"
 * NOTE: The maximum number of bit of this integer is unspecified,
 * but to handle it as a signed 64bit integer is mandatory to handle
 * "large files" aka .torrent for more that 4Gbyte
 */
int tr_bencParseInt(void const* vbuf, void const* vbufend, uint8_t const** setme_end, int64_t* setme_val)
{
    uint8_t const* const buf = (uint8_t const*)vbuf;
    uint8_t const* const bufend = (uint8_t const*)vbufend;

    if (buf >= bufend)
    {
        return EILSEQ;
    }

    if (*buf != 'i')
    {
        return EILSEQ;
    }

    void const* begin = buf + 1;
    void const* end = memchr(begin, 'e', (bufend - buf) - 1);

    if (end == nullptr)
    {
        return EILSEQ;
    }

    errno = 0;
    char* endptr;
    int64_t val = evutil_strtoll(static_cast<char const*>(begin), &endptr, 10);

    if (errno != 0 || endptr != end) /* incomplete parse */
    {
        return EILSEQ;
    }

    if (val != 0 && *(char const*)begin == '0') /* no leading zeroes! */
    {
        return EILSEQ;
    }

    *setme_end = (uint8_t const*)end + 1;
    *setme_val = val;
    return 0;
}

/**
 * Byte strings are encoded as follows:
 * <string length encoded in base ten ASCII>:<string data>
 * Note that there is no constant beginning delimiter, and no ending delimiter.
 * Example: 4:spam represents the string "spam"
 */
int tr_bencParseStr(
    void const* vbuf,
    void const* vbufend,
    uint8_t const** setme_end,
    uint8_t const** setme_str,
    size_t* setme_strlen)
{
    uint8_t const* const buf = (uint8_t const*)vbuf;
    uint8_t const* const bufend = (uint8_t const*)vbufend;

    if ((buf < bufend) && isdigit(*buf))
    {
        void const* end = memchr(buf, ':', bufend - buf);

        if (end != nullptr)
        {
            errno = 0;
            char* ulend;
            size_t len = strtoul((char const*)buf, &ulend, 10);

            if (errno == 0 && ulend == end && len <= MAX_BENC_STR_LENGTH)
            {
                uint8_t const* strbegin = (uint8_t const*)end + 1;
                uint8_t const* strend = strbegin + len;

                if (strbegin <= strend && strend <= bufend)
                {
                    *setme_end = (uint8_t const*)end + 1 + len;
                    *setme_str = (uint8_t const*)end + 1;
                    *setme_strlen = len;
                    return 0;
                }
            }
        }
    }

    *setme_end = nullptr;
    *setme_str = nullptr;
    *setme_strlen = 0;
    return EILSEQ;
}

static tr_variant* get_node(std::deque<tr_variant*>& stack, tr_quark* key, tr_variant* top, int* err)
{
    tr_variant* node = nullptr;

    if (std::empty(stack))
    {
        node = top;
    }
    else
    {
        auto* parent = stack.back();

        if (tr_variantIsList(parent))
        {
            node = tr_variantListAdd(parent);
        }
        else if (*key != 0 && tr_variantIsDict(parent))
        {
            node = tr_variantDictAdd(parent, *key);
            *key = 0;
        }
        else
        {
            *err = EILSEQ;
        }
    }

    return node;
}

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted bencoded data. (#667)
 */
int tr_variantParseBenc(void const* buf_in, void const* bufend_in, tr_variant* top, char const** setme_end)
{
    int err = 0;
    auto const* buf = static_cast<uint8_t const*>(buf_in);
    auto const* const bufend = static_cast<uint8_t const*>(bufend_in);
    auto stack = std::deque<tr_variant*>{};
    tr_quark key = 0;

    if ((buf_in == nullptr) || (bufend_in == nullptr) || (top == nullptr))
    {
        return EINVAL;
    }

    tr_variantInit(top, 0);

    while (buf != bufend)
    {
        if (buf > bufend) /* no more text to parse... */
        {
            err = EILSEQ;
        }

        if (err != 0)
        {
            break;
        }

        if (*buf == 'i') /* int */
        {
            int64_t val;
            uint8_t const* end;
            tr_variant* v;

            if ((err = tr_bencParseInt(buf, bufend, &end, &val)) != 0)
            {
                break;
            }

            buf = end;

            if ((v = get_node(stack, &key, top, &err)) != nullptr)
            {
                tr_variantInitInt(v, val);
            }
        }
        else if (*buf == 'l') /* list */
        {
            tr_variant* v;

            ++buf;

            if ((v = get_node(stack, &key, top, &err)) != nullptr)
            {
                tr_variantInitList(v, 0);
                stack.push_back(v);
            }
        }
        else if (*buf == 'd') /* dict */
        {
            tr_variant* v;

            ++buf;

            if ((v = get_node(stack, &key, top, &err)) != nullptr)
            {
                tr_variantInitDict(v, 0);
                stack.push_back(v);
            }
        }
        else if (*buf == 'e') /* end of list or dict */
        {
            ++buf;

            if (std::empty(stack) || key != 0)
            {
                err = EILSEQ;
                break;
            }
            else
            {
                stack.pop_back();

                if (std::empty(stack))
                {
                    break;
                }
            }
        }
        else if (isdigit(*buf)) /* string? */
        {
            tr_variant* v;
            uint8_t const* end;
            uint8_t const* str;
            size_t str_len;

            if ((err = tr_bencParseStr(buf, bufend, &end, &str, &str_len)) != 0)
            {
                break;
            }

            buf = end;

            if (key == 0 && !std::empty(stack) && tr_variantIsDict(stack.back()))
            {
                key = tr_quark_new(str, str_len);
            }
            else if ((v = get_node(stack, &key, top, &err)) != nullptr)
            {
                tr_variantInitStr(v, str, str_len);
            }
        }
        else /* invalid bencoded text... march past it */
        {
            ++buf;
        }

        if (std::empty(stack))
        {
            break;
        }
    }

    if (err == 0 && (top->type == 0 || !std::empty(stack)))
    {
        err = EILSEQ;
    }

    if (err == 0)
    {
        if (setme_end != nullptr)
        {
            *setme_end = (char const*)buf;
        }
    }
    else if (top->type != 0)
    {
        tr_variantFree(top);
        tr_variantInit(top, 0);
    }

    return err;
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
    char buf[128];
    int const len = tr_snprintf(buf, sizeof(buf), "%f", val->val.d);

    auto* evbuf = static_cast<struct evbuffer*>(vevbuf);
    evbuffer_add_printf(evbuf, "%d:", len);
    evbuffer_add(evbuf, buf, len);
}

static void saveStringFunc(tr_variant const* v, void* vevbuf)
{
    size_t len;
    char const* str;
    if (!tr_variantGetStr(v, &str, &len))
    {
        len = 0;
        str = nullptr;
    }

    auto* evbuf = static_cast<struct evbuffer*>(vevbuf);
    evbuffer_add_printf(evbuf, "%zu:", len);
    evbuffer_add(evbuf, str, len);
}

static void saveDictBeginFunc(tr_variant const* val, void* vevbuf)
{
    TR_UNUSED(val);

    auto* evbuf = static_cast<struct evbuffer*>(vevbuf);
    evbuffer_add(evbuf, "d", 1);
}

static void saveListBeginFunc(tr_variant const* val, void* vevbuf)
{
    TR_UNUSED(val);

    auto* evbuf = static_cast<struct evbuffer*>(vevbuf);
    evbuffer_add(evbuf, "l", 1);
}

static void saveContainerEndFunc(tr_variant const* val, void* vevbuf)
{
    TR_UNUSED(val);

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
