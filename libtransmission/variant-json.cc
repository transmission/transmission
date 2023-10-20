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
#include <iterator> // std::back_inserter
#include <stack>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fmt/core.h>

// RapidJSON SIMD optimisations
#ifdef __SSE2__
#define RAPIDJSON_SSE2
#endif
#ifdef __SSE4_2__
#define RAPIDJSON_SSE42
#endif
#ifdef __ARM_NEON
#define RAPIDJSON_NEON
#endif
#include <rapidjson/encodedstream.h>
#include <rapidjson/error/en.h>
#include <rapidjson/memorystream.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/reader.h>
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
namespace parse_helpers
{
struct json_to_variant_handler : public rapidjson::BaseReaderHandler<>
{
    explicit json_to_variant_handler(tr_variant* const top)
    {
        stack_.emplace(top);
    }

    bool Null()
    {
        tr_variantInitQuark(get_leaf(), TR_KEY_NONE);
        return true;
    }

    bool Bool(bool const val)
    {
        tr_variantInitBool(get_leaf(), val);
        return true;
    }

    bool Int(int const val)
    {
        return Int64(val);
    }

    bool Uint(unsigned const val)
    {
        return Uint64(val);
    }

    bool Int64(int64_t const val)
    {
        tr_variantInitInt(get_leaf(), val);
        return true;
    }

    bool Uint64(uint64_t const val)
    {
        return Int64(val);
    }

    bool Double(double const val)
    {
        tr_variantInitReal(get_leaf(), val);
        return true;
    }

    bool String(Ch const* const str, rapidjson::SizeType const len, bool const copy)
    {
        if (copy)
        {
            tr_variantInitStr(get_leaf(), { str, len });
        }
        else
        {
            tr_variantInitStrView(get_leaf(), { str, len });
        }
        return true;
    }

    bool StartObject()
    {
        tr_variantInitDict(push_stack(), prealloc_guess());
        return true;
    }

    bool Key(Ch const* const str, rapidjson::SizeType const len, bool const copy)
    {
        if (copy)
        {
            key_buf_ = std::string{ str, len };
            cur_key_ = key_buf_;
        }
        else
        {
            cur_key_ = std::string_view{ str, len };
        }
        return true;
    }

    bool EndObject(rapidjson::SizeType const len)
    {
        pop_stack(len);
        return true;
    }

    bool StartArray()
    {
        tr_variantInitList(push_stack(), prealloc_guess());
        return true;
    }

    bool EndArray(rapidjson::SizeType const len)
    {
        pop_stack(len);
        return true;
    }

private:
    [[nodiscard]] size_t prealloc_guess() const noexcept
    {
        auto const depth = std::size(stack_);
        return depth < MaxDepth ? prealloc_guess_[depth] : 0;
    }

    tr_variant* push_stack() noexcept
    {
        return stack_.emplace(get_leaf());
    }

    void pop_stack(rapidjson::SizeType const len) noexcept
    {
#ifdef TR_ENABLE_ASSERTS
        if (auto* top = stack_.top(); top->holds_alternative<tr_variant::Vector>())
        {
            TR_ASSERT(std::size(*top->get_if<tr_variant::Vector>()) == len);
        }
        else if (top->holds_alternative<tr_variant::Map>())
        {
            TR_ASSERT(std::size(*top->get_if<tr_variant::Map>()) == len);
        }
#endif

        auto const depth = std::size(stack_);
        stack_.pop();
        TR_ASSERT(!std::empty(stack_));
        if (depth < MaxDepth)
        {
            prealloc_guess_[depth] = len;
        }
    }

    tr_variant* get_leaf()
    {
        auto* const parent = stack_.top();
        TR_ASSERT(parent != nullptr);

        if (parent->holds_alternative<tr_variant::Vector>())
        {
            return tr_variantListAdd(parent);
        }
        if (parent->holds_alternative<tr_variant::Map>())
        {
            TR_ASSERT(!std::empty(cur_key_));
            auto tmp = std::string_view{};
            std::swap(cur_key_, tmp);
            return tr_variantDictAdd(parent, tr_quark_new(tmp));
        }

        return parent;
    }

    /* arbitrary value... this is much deeper than our code goes */
    static auto constexpr MaxDepth = size_t{ 64 };

    /* A very common pattern is for a container's children to be similar,
     * e.g. they may all be objects with the same set of keys. So when
     * a container is popped off the stack, remember its size to use as
     * a preallocation heuristic for the next container at that depth. */
    std::array<size_t, MaxDepth> prealloc_guess_{};

    std::string key_buf_;
    std::string_view cur_key_;
    std::stack<tr_variant*> stack_;
};
} // namespace parse_helpers
} // namespace

std::optional<tr_variant> tr_variant_serde::parse_json(std::string_view input)
{
    auto top = tr_variant{};

    auto* const begin = std::data(input);
    auto const size = std::size(input);

    auto handler = parse_helpers::json_to_variant_handler{ &top };
    auto ms = rapidjson::MemoryStream{ begin, size };
    auto eis = rapidjson::AutoUTFInputStream<unsigned, rapidjson::MemoryStream>{ ms };
    auto reader = rapidjson::GenericReader<rapidjson::AutoUTF<unsigned>, rapidjson::UTF8<char>>{};
    reader.Parse(eis, handler);

    if (!reader.HasParseError())
    {
        return top;
    }

    if (auto err_code = reader.GetParseErrorCode(); err_code == rapidjson::kParseErrorDocumentEmpty)
    {
        tr_error_set(&error_, EINVAL, "No content");
    }
    else
    {
        auto const err_offset = reader.GetErrorOffset();
        tr_error_set(
            &error_,
            EILSEQ,
            fmt::format(
                _("Couldn't parse JSON at position {position} '{text}': {error} ({error_code})"),
                fmt::arg("position", err_offset),
                fmt::arg("text", std::string_view{ begin + err_offset, std::min(size_t{ 16U }, size - err_offset) }),
                fmt::arg("error", rapidjson::GetParseError_En(err_code)),
                fmt::arg("error_code", static_cast<std::underlying_type_t<decltype(err_code)>>(err_code))));
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
