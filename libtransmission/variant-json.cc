// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno> /* EILSEQ, EINVAL */
#include <cmath> /* fabs() */
#include <cstddef> // std::byte
#include <cstdint> // uint16_t
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iterator> // std::back_inserter
#include <string>
#include <string_view>
#include <utility>

#define UTF_CPP_CPLUSPLUS 201703L
#include <utf8.h>

#include <fmt/core.h>
#include <fmt/compile.h>

#include <jsonsl.h>

#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#define LIBTRANSMISSION_VARIANT_MODULE

#include "libtransmission/error.h"
#include "libtransmission/quark.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-buffer.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

using namespace std::literals;

namespace
{
[[nodiscard]] constexpr size_t variant_size(tr_variant const& var) noexcept
{
    switch (var.index())
    {
    case tr_variant::MapIndex:
        return std::size(*var.get_if<tr_variant::Map>());

    case tr_variant::VectorIndex:
        return std::size(*var.get_if<tr_variant::Vector>());

    default:
        return {};
    }
}

namespace parse_helpers
{
/* arbitrary value... this is much deeper than our code goes */
auto constexpr MaxDepth = size_t{ 64 };

struct json_wrapper_data
{
    bool has_content;
    size_t size;
    std::string_view key;
    std::string keybuf;
    std::string strbuf;
    tr_error* error;
    std::deque<tr_variant*> stack;
    tr_variant* top;
    bool inplace = false;

    /* A very common pattern is for a container's children to be similar,
     * e.g. they may all be objects with the same set of keys. So when
     * a container is popped off the stack, remember its size to use as
     * a preallocation heuristic for the next container at that depth. */
    std::array<size_t, MaxDepth> preallocGuess;
};

tr_variant* get_node(struct jsonsl_st* jsn)
{
    auto* data = static_cast<struct json_wrapper_data*>(jsn->data);

    auto* parent = std::empty(data->stack) ? nullptr : data->stack.back();

    tr_variant* node = nullptr;
    if (parent == nullptr)
    {
        node = data->top;
    }
    else if (parent->holds_alternative<tr_variant::Vector>())
    {
        node = tr_variantListAdd(parent);
    }
    else if (parent->holds_alternative<tr_variant::Map>() && !std::empty(data->key))
    {
        node = tr_variantDictAdd(parent, tr_quark_new(data->key));
        data->key = ""sv;
    }

    return node;
}

void error_handler(jsonsl_t jsn, jsonsl_error_t error, jsonsl_state_st* /*state*/, jsonsl_char_t const* buf)
{
    auto* data = static_cast<struct json_wrapper_data*>(jsn->data);

    tr_error_set(
        &data->error,
        EILSEQ,
        fmt::format(
            _("Couldn't parse JSON at position {position} '{text}': {error} ({error_code})"),
            fmt::arg("position", jsn->pos),
            fmt::arg("text", std::string_view{ buf, std::min(size_t{ 16U }, data->size - jsn->pos) }),
            fmt::arg("error", jsonsl_strerror(error)),
            fmt::arg("error_code", static_cast<int>(error))));
}

int error_callback(jsonsl_t jsn, jsonsl_error_t error, struct jsonsl_state_st* state, jsonsl_char_t* at)
{
    error_handler(jsn, error, state, at);
    return 0; /* bail */
}

void action_callback_PUSH(jsonsl_t jsn, jsonsl_action_t /*action*/, struct jsonsl_state_st* state, jsonsl_char_t const* /*buf*/)
{
    auto* const data = static_cast<json_wrapper_data*>(jsn->data);

    if ((state->type == JSONSL_T_LIST) || (state->type == JSONSL_T_OBJECT))
    {
        data->has_content = true;
        tr_variant* node = get_node(jsn);
        data->stack.push_back(node);

        size_t const depth = std::size(data->stack);
        size_t const n = depth < MaxDepth ? data->preallocGuess[depth] : 0;
        if (state->type == JSONSL_T_LIST)
        {
            tr_variantInitList(node, n);
        }
        else
        {
            tr_variantInitDict(node, n);
        }
    }
}

/* like sscanf(in+2, "%4x", &val) but less slow */
[[nodiscard]] constexpr bool decode_hex_string(char const* in, std::uint16_t& setme)
{
    TR_ASSERT(in != nullptr);

    unsigned int val = 0;
    char const* const end = in + 6;

    TR_ASSERT(in[0] == '\\');
    TR_ASSERT(in[1] == 'u');
    in += 2;

    do
    {
        val <<= 4;

        if ('0' <= *in && *in <= '9')
        {
            val += *in - '0';
        }
        else if ('a' <= *in && *in <= 'f')
        {
            val += *in - 'a' + 10U;
        }
        else if ('A' <= *in && *in <= 'F')
        {
            val += *in - 'A' + 10U;
        }
        else
        {
            return false;
        }
    } while (++in != end);

    setme = val;
    return true;
}

template<typename Iter>
void decode_single_uchar(char const*& in, char const* const in_end, Iter& buf16_out_it)
{
    static auto constexpr EscapedUcharLength = 6U;
    if (in_end - in >= EscapedUcharLength && decode_hex_string(in, *buf16_out_it))
    {
        in += EscapedUcharLength;
        ++buf16_out_it;
    }
}

[[nodiscard]] bool decode_escaped_uchar_sequence(char const*& in, char const* const in_end, std::string& buf)
{
    auto buf16 = std::array<std::uint16_t, 2>{};
    auto buf16_out_it = std::begin(buf16);

    decode_single_uchar(in, in_end, buf16_out_it);
    if (in[0] == '\\' && in[1] == 'u')
    {
        decode_single_uchar(in, in_end, buf16_out_it);
    }

    if (buf16_out_it == std::begin(buf16))
    {
        return false;
    }

    try
    {
        utf8::utf16to8(std::begin(buf16), buf16_out_it, std::back_inserter(buf));
    }
    catch (utf8::exception const&) // invalid codepoint
    {
        buf.push_back('?');
    }

    return true;
}

[[nodiscard]] std::string_view extract_escaped_string(char const* in, size_t in_len, std::string& buf)
{
    char const* const in_end = in + in_len;

    buf.clear();

    while (in < in_end)
    {
        bool unescaped = false;

        if (*in == '\\' && in_end - in >= 2)
        {
            switch (in[1])
            {
            case 'b':
                buf.push_back('\b');
                in += 2;
                unescaped = true;
                break;

            case 'f':
                buf.push_back('\f');
                in += 2;
                unescaped = true;
                break;

            case 'n':
                buf.push_back('\n');
                in += 2;
                unescaped = true;
                break;

            case 'r':
                buf.push_back('\r');
                in += 2;
                unescaped = true;
                break;

            case 't':
                buf.push_back('\t');
                in += 2;
                unescaped = true;
                break;

            case '/':
                buf.push_back('/');
                in += 2;
                unescaped = true;
                break;

            case '"':
                buf.push_back('"');
                in += 2;
                unescaped = true;
                break;

            case '\\':
                buf.push_back('\\');
                in += 2;
                unescaped = true;
                break;

            case 'u':
                if (decode_escaped_uchar_sequence(in, in_end, buf))
                {
                    unescaped = true;
                    break;
                }
            }
        }

        if (!unescaped)
        {
            buf.push_back(*in);
            ++in;
        }
    }

    return buf;
}

[[nodiscard]] std::pair<std::string_view, bool> extract_string(jsonsl_t jsn, struct jsonsl_state_st* state, std::string& buf)
{
    // figure out where the string is
    char const* in_begin = jsn->base + state->pos_begin;
    if (*in_begin == '"')
    {
        in_begin++;
    }

    char const* const in_end = jsn->base + state->pos_cur;
    size_t const in_len = in_end - in_begin;
    if (memchr(in_begin, '\\', in_len) == nullptr)
    {
        /* it's not escaped */
        return std::make_pair(std::string_view{ in_begin, in_len }, true);
    }

    return std::make_pair(extract_escaped_string(in_begin, in_len, buf), false);
}

void action_callback_POP(jsonsl_t jsn, jsonsl_action_t /*action*/, struct jsonsl_state_st* state, jsonsl_char_t const* /*buf*/)
{
    auto* data = static_cast<struct json_wrapper_data*>(jsn->data);

    if (state->type == JSONSL_T_STRING)
    {
        auto const [str, inplace] = extract_string(jsn, state, data->strbuf);
        if (inplace && data->inplace)
        {
            tr_variantInitStrView(get_node(jsn), str);
        }
        else
        {
            tr_variantInitStr(get_node(jsn), str);
        }
        data->has_content = true;
    }
    else if (state->type == JSONSL_T_HKEY)
    {
        data->has_content = true;
        auto const [key, inplace] = extract_string(jsn, state, data->keybuf);
        data->key = key;
    }
    else if (state->type == JSONSL_T_LIST || state->type == JSONSL_T_OBJECT)
    {
        auto const depth = std::size(data->stack);
        auto const* const v = data->stack.back();
        data->stack.pop_back();
        if (depth < MaxDepth)
        {
            data->preallocGuess[depth] = variant_size(*v);
        }
    }
    else if (state->type == JSONSL_T_SPECIAL)
    {
        if ((state->special_flags & JSONSL_SPECIALf_NUMNOINT) != 0)
        {
            auto sv = std::string_view{ jsn->base + state->pos_begin, jsn->pos - state->pos_begin };
            tr_variantInitReal(get_node(jsn), tr_num_parse<double>(sv).value_or(0.0));
        }
        else if ((state->special_flags & JSONSL_SPECIALf_NUMERIC) != 0)
        {
            char const* begin = jsn->base + state->pos_begin;
            data->has_content = true;
            tr_variantInitInt(get_node(jsn), std::strtoll(begin, nullptr, 10));
        }
        else if ((state->special_flags & JSONSL_SPECIALf_BOOLEAN) != 0)
        {
            bool const b = (state->special_flags & JSONSL_SPECIALf_TRUE) != 0;
            data->has_content = true;
            tr_variantInitBool(get_node(jsn), b);
        }
        else if ((state->special_flags & JSONSL_SPECIALf_NULL) != 0)
        {
            data->has_content = true;
            tr_variantInitQuark(get_node(jsn), TR_KEY_NONE);
        }
    }
}

} // namespace parse_helpers
} // namespace

std::optional<tr_variant> tr_variant_serde::parse_json(std::string_view input)
{
    using namespace parse_helpers;

    auto top = tr_variant{};

    auto data = json_wrapper_data{};
    data.error = nullptr;
    data.size = std::size(input);
    data.has_content = false;
    data.key = ""sv;
    data.inplace = parse_inplace_;
    data.preallocGuess = {};
    data.stack = {};
    data.top = &top;

    auto jsn = jsonsl_new(MaxDepth);
    jsn->action_callback_PUSH = action_callback_PUSH;
    jsn->action_callback_POP = action_callback_POP;
    jsn->error_callback = error_callback;
    jsn->data = &data;
    jsonsl_enable_all_callbacks(jsn);

    // parse it
    jsonsl_feed(jsn, static_cast<jsonsl_char_t const*>(std::data(input)), std::size(input));

    // EINVAL if there was no content
    if (data.error == nullptr && !data.has_content)
    {
        tr_error_set(&data.error, EINVAL, "No content");
    }

    end_ = std::data(input) + jsn->pos;

    if (data.error != nullptr)
    {
        tr_error_propagate(&error_, &data.error);
    }

    // cleanup
    jsonsl_destroy(jsn);

    if (error_ == nullptr)
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
struct JsonWalk
{
    JsonWalk(rapidjson::StringBuffer& buf, bool is_compact)
    {
        if (is_compact)
        {
            writer.emplace<0>(buf);
        }
        else
        {
            writer.emplace<1>(buf);
        }
    }

    std::variant<rapidjson::Writer<rapidjson::StringBuffer>, rapidjson::PrettyWriter<rapidjson::StringBuffer>> writer;
};

void jsonIntFunc(tr_variant const& /*var*/, int64_t const val, void* vdata)
{
    std::visit([val](auto&& writer) { writer.Int64(val); }, static_cast<JsonWalk*>(vdata)->writer);
}

void jsonBoolFunc(tr_variant const& /*var*/, bool const val, void* vdata)
{
    std::visit([val](auto&& writer) { writer.Bool(val); }, static_cast<struct JsonWalk*>(vdata)->writer);
}

void jsonRealFunc(tr_variant const& /*var*/, double const val, void* vdata)
{
    std::visit([val](auto&& writer) { writer.Double(val); }, static_cast<struct JsonWalk*>(vdata)->writer);
}

void jsonStringFunc(tr_variant const& /*var*/, std::string_view sv, void* vdata)
{
    std::visit(
        [sv](auto&& writer) { writer.String(std::data(sv), std::size(sv)); },
        static_cast<struct JsonWalk*>(vdata)->writer);
}

void jsonDictBeginFunc(tr_variant const& /*var*/, void* vdata)
{
    std::visit([](auto&& writer) { writer.StartObject(); }, static_cast<struct JsonWalk*>(vdata)->writer);
}

void jsonListBeginFunc(tr_variant const& /*var*/, void* vdata)
{
    std::visit([](auto&& writer) { writer.StartArray(); }, static_cast<struct JsonWalk*>(vdata)->writer);
}

void jsonContainerEndFunc(tr_variant const& var, void* vdata)
{
    auto& writer_var = static_cast<struct JsonWalk*>(vdata)->writer;

    if (var.holds_alternative<tr_variant::Map>())
    {
        std::visit([](auto&& writer) { writer.EndObject(); }, writer_var);
    }
    else /* list */
    {
        std::visit([](auto&& writer) { writer.EndArray(); }, writer_var);
    }
}

} // namespace to_string_helpers
} // namespace

std::string tr_variant_serde::to_json_string(tr_variant const& var) const
{
    using namespace to_string_helpers;

    static auto constexpr Funcs = WalkFuncs{
        jsonIntFunc, //
        jsonBoolFunc, //
        jsonRealFunc, //
        jsonStringFunc, //
        jsonDictBeginFunc, //
        jsonListBeginFunc, //
        jsonContainerEndFunc, //
    };

    auto buf = rapidjson::StringBuffer{};
    auto data = JsonWalk{ buf, compact_ };
    walk(var, Funcs, &data, true);

    return { buf.GetString(), buf.GetLength() };
}
