/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>

#include <iostream> // NOCOMMIT

#include "error.h"

namespace transmission::benc
{

namespace impl
{

std::optional<int64_t> ParseInt(std::string_view* benc);

std::optional<std::string_view> ParseString(std::string_view* benc);

} // namespace impl

struct Handler
{
    virtual bool Int64(int64_t) = 0;
    virtual bool String(std::string_view) = 0;

    virtual bool StartDict() = 0;
    virtual bool Key(std::string_view) = 0;
    virtual bool EndDict() = 0;

    virtual bool StartArray() = 0;
    virtual bool EndArray() = 0;
};

template<std::size_t MaxDepth>
struct ParserStack
{
    enum class ParentType
    {
        Array,
        Dict
    };
    struct Node
    {
        ParentType parent_type;
        size_t n_children_walked;
    };
    std::array<Node, MaxDepth> stack;
    std::size_t depth = 0;

    void clear()
    {
        depth = 0;
    }

    void tokenWalked()
    {
        ++stack[depth].n_children_walked;
        std::cerr << __FILE__ << ':' << __LINE__ << " stack[" << depth << "].n_children_walked is now "
                  << stack[depth].n_children_walked << std::endl;
    }

    Node& current()
    {
        return stack[depth];
    }
    Node& current() const
    {
        return stack[depth];
    }

    bool expectingDictKey() const
    {
        return depth > 0 && stack[depth].parent_type == ParentType::Dict && (stack[depth].n_children_walked % 2) == 0;
    }

    std::optional<ParentType> parentType() const
    {
        if (depth == 0)
        {
            return {};
        }

        return stack[depth].parent_type;
    }

    std::optional<ParentType> pop(tr_error** error)
    {
        std::cerr << __FILE__ << ':' << __LINE__ << " popping stack depth " << depth << " --> " << depth - 1 << std::endl;
        if (depth == 0)
        {
            tr_error_set_literal(error, EILSEQ, "Cannot pop empty stack");
            return {};
        }

        if (stack[depth].parent_type == ParentType::Dict && ((stack[depth].n_children_walked % 2) != 0))
        {
            tr_error_set_literal(error, EILSEQ, "Premature end-of-dict found. Malformed benc?");
            return {};
        }

        auto const ret = stack[depth].parent_type;
        --depth;
        return ret;
    }

    bool push(ParentType parent_type, tr_error** error)
    {
        if (depth + 1 >= std::size(stack))
        {
            tr_error_set_literal(error, E2BIG, "Max stack depth reached; unable to continue parsing");
            return false;
        }

        ++depth;
        std::cerr << __FILE__ << ':' << __LINE__ << " pushed stack to depth " << depth << std::endl;
        current() = { parent_type, 0 };
        return true;
    }
};

template<size_t MaxDepth>
bool parse(
    std::string_view benc,
    ParserStack<MaxDepth>& stack,
    Handler& handler,
    char const** setme_end = nullptr,
    tr_error** error = nullptr)
{
    stack.clear();

    int err = 0;
    for (;;)
    {
        if (std::empty(benc))
        {
            err = EILSEQ;
        }

        if (err != 0)
        {
            break;
        }

        switch (benc.front())
        {
        case 'i': // int
            {
                auto const value = impl::ParseInt(&benc);
                if (!value)
                {
                    tr_error_set_literal(error, err, "Malformed benc? Unable to parse integer");
                    break;
                }

                if (!handler.Int64(*value))
                {
                    err = ECANCELED;
                    tr_error_set_literal(error, err, "Handler indicated parser should stop");
                    break;
                }

                stack.tokenWalked();
                break;
            }
        case 'l': // list
        case 'd': // dict
            {
                bool ok = benc.front() == 'l' ? stack.push(ParserStack<MaxDepth>::ParentType::Array, error) :
                                                stack.push(ParserStack<MaxDepth>::ParentType::Dict, error);
                if (!ok)
                {
                    err = EILSEQ;
                    break;
                }

                ok = benc.front() == 'l' ? handler.StartArray() : handler.StartDict();
                if (!ok)
                {
                    err = ECANCELED;
                    tr_error_set_literal(error, err, "Handler indicated parser should stop");
                    break;
                }

                benc.remove_prefix(1);
                break;
            }
        case 'e': // end of list or dict
            {
                benc.remove_prefix(1);

                auto const parent_type = stack.pop(error);
                if (!parent_type)
                {
                    err = EILSEQ;
                    break;
                }

                stack.tokenWalked();

                bool ok = *parent_type == ParserStack<MaxDepth>::ParentType::Array ? handler.EndArray() : handler.EndDict();

                if (!ok)
                {
                    err = ECANCELED;
                    tr_error_set_literal(error, err, "Handler indicated parser should stop");
                    break;
                }

                break;
            }

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': // string
            {
                auto const sv = impl::ParseString(&benc);
                if (!sv)
                {
                    err = EILSEQ;
                    tr_error_set_literal(error, err, "Malformed benc? Unable to parse string");
                    break;
                }

                bool const ok = stack.expectingDictKey() ? handler.Key(*sv) : handler.String(*sv);

                if (!ok)
                {
                    err = ECANCELED;
                    tr_error_set_literal(error, err, "Handler indicated parser should stop");
                    break;
                }
                stack.tokenWalked();
                break;
            }

        default: // invalid bencoded text... march past it
            benc.remove_prefix(1);
            break;
        }

        if (stack.depth == 0)
        {
            break;
        }
    }

    if (err != 0)
    {
        errno = err;
        std::cerr << __FILE__ << ':' << __LINE__ << " transmission::benc::parse returning false" << std::endl;
        return false;
    }

    if (stack.depth != 0)
    {
        err = EILSEQ;
        tr_error_set_literal(error, err, "premature end-of-data reached");
        std::cerr << __FILE__ << ':' << __LINE__ << " transmission::benc::parse returning false" << std::endl;
        errno = err;
        return false;
    }

    if (stack.stack[0].n_children_walked == 0)
    {
        err = EILSEQ;
        tr_error_set_literal(error, err, "no data found");
        std::cerr << __FILE__ << ':' << __LINE__ << " transmission::benc::parse returning false" << std::endl;
        errno = err;
        return false;
    }

    if (setme_end != nullptr)
    {
        *setme_end = std::data(benc);
    }

    std::cerr << __FILE__ << ':' << __LINE__ << " transmission::benc::parse returning false" << std::endl;
    return true;
}

} // namespace transmission::benc
