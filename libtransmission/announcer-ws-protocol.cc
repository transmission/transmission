// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// WebTorrent's tracker protocol represents 20-byte binary IDs as JSON strings.
// Do not send hex or URL-encoded info_hash/peer_id values here.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "libtransmission/announcer-ws-protocol.h"

namespace tr_ws_tracker
{
namespace
{
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

void binary_value(Writer& writer, std::string_view bytes)
{
    auto const encoded = encode_binary(bytes);
    writer.String(encoded.data(), static_cast<rapidjson::SizeType>(encoded.size()));
}

void string_value(Writer& writer, std::string_view value)
{
    writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
}

std::optional<std::string> field_binary(rapidjson::Value const& value, char const* field)
{
    if (!value.HasMember(field) || !value[field].IsString())
    {
        return std::nullopt;
    }
    return decode_binary({ value[field].GetString(), value[field].GetStringLength() });
}

std::optional<std::string> field_text(rapidjson::Value const& value, char const* field)
{
    if (!value.HasMember(field) || !value[field].IsString())
    {
        return std::nullopt;
    }
    return std::string{ value[field].GetString(), value[field].GetStringLength() };
}

std::optional<int64_t> field_count(rapidjson::Value const& value, char const* field)
{
    if (value.HasMember(field) && value[field].IsInt64() && value[field].GetInt64() >= 0)
    {
        return value[field].GetInt64();
    }
    return std::nullopt;
}
} // namespace

std::string encode_binary(std::string_view bytes)
{
    auto text = std::string{};
    text.reserve(bytes.size() * 2U);
    for (unsigned char const byte : bytes)
    {
        if (byte < 0x80)
        {
            text.push_back(static_cast<char>(byte));
        }
        else
        {
            text.push_back(static_cast<char>(0xC0 | (byte >> 6U)));
            text.push_back(static_cast<char>(0x80 | (byte & 0x3FU)));
        }
    }
    return text;
}

std::optional<std::string> decode_binary(std::string_view text)
{
    auto binary = std::string{};
    binary.reserve(text.size());

    for (size_t i = 0; i < text.size();)
    {
        auto const first = static_cast<uint8_t>(text[i++]);
        if (first < 0x80)
        {
            binary.push_back(static_cast<char>(first));
            continue;
        }

        // Only 2-byte UTF-8 for U+0080..U+00FF is representable by a byte.
        if (first != 0xC2 && first != 0xC3)
        {
            return std::nullopt;
        }
        if (i >= text.size())
        {
            return std::nullopt;
        }
        auto const next = static_cast<uint8_t>(text[i++]);
        if ((next & 0xC0U) != 0x80U)
        {
            return std::nullopt;
        }
        auto const decoded = static_cast<uint16_t>((first & 0x1FU) << 6U) | (next & 0x3FU);
        binary.push_back(static_cast<char>(decoded));
    }

    return binary;
}

std::string encode_announce(Announce const& announce)
{
    auto buffer = rapidjson::StringBuffer{};
    auto writer = Writer{ buffer };
    writer.StartObject();
    writer.Key("action");
    writer.String("announce");
    writer.Key("info_hash");
    binary_value(writer, announce.info_hash);
    writer.Key("peer_id");
    binary_value(writer, announce.peer_id);
    writer.Key("uploaded");
    writer.Uint64(announce.uploaded);
    writer.Key("downloaded");
    writer.Uint64(announce.downloaded);
    writer.Key("left");
    writer.Uint64(announce.left);
    writer.Key("numwant");
    writer.Int64(announce.numwant);
    if (!announce.event.empty())
    {
        writer.Key("event");
        string_value(writer, announce.event);
    }
    writer.Key("offers");
    writer.StartArray();
    for (auto const& offer : announce.offers)
    {
        writer.StartObject();
        writer.Key("offer_id");
        binary_value(writer, offer.offer_id);
        writer.Key("offer");
        writer.StartObject();
        writer.Key("type");
        writer.String("offer");
        writer.Key("sdp");
        string_value(writer, offer.sdp);
        writer.EndObject();
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
    return { buffer.GetString(), buffer.GetSize() };
}

std::string encode_answer(
    std::string_view info_hash,
    std::string_view our_peer_id,
    std::string_view remote_peer_id,
    std::string_view offer_id,
    std::string_view sdp)
{
    auto buffer = rapidjson::StringBuffer{};
    auto writer = Writer{ buffer };
    writer.StartObject();
    writer.Key("action");
    writer.String("announce");
    writer.Key("info_hash");
    binary_value(writer, info_hash);
    writer.Key("peer_id");
    binary_value(writer, our_peer_id);
    writer.Key("to_peer_id");
    binary_value(writer, remote_peer_id);
    writer.Key("offer_id");
    binary_value(writer, offer_id);
    writer.Key("answer");
    writer.StartObject();
    writer.Key("type");
    writer.String("answer");
    writer.Key("sdp");
    string_value(writer, sdp);
    writer.EndObject();
    writer.EndObject();
    return { buffer.GetString(), buffer.GetSize() };
}

std::optional<Signal> parse_signal(std::string_view json)
{
    // Reject unbounded remote JSON before asking RapidJSON to allocate.
    if (json.empty() || json.size() > 1024U * 1024U)
    {
        return std::nullopt;
    }

    auto document = rapidjson::Document{};
    document.Parse(json.data(), json.size());
    if (document.HasParseError() || !document.IsObject())
    {
        return std::nullopt;
    }
    if (auto const action = field_text(document, "action"); !action || *action != "announce")
    {
        return std::nullopt;
    }
    auto const hash = field_binary(document, "info_hash");
    if (!hash || hash->size() != 20U)
    {
        return std::nullopt;
    }

    auto signal = Signal{};
    signal.info_hash = *hash;
    signal.complete = field_count(document, "complete");
    signal.incomplete = field_count(document, "incomplete");
    signal.interval = field_count(document, "interval").value_or(0);
    if (auto const failure = field_text(document, "failure reason"); failure)
    {
        signal.kind = Signal::Kind::Failure;
        signal.failure = *failure;
        return signal;
    }

    bool const is_offer = document.HasMember("offer");
    bool const is_answer = document.HasMember("answer");
    if (is_offer || is_answer)
    {
        if (is_offer == is_answer)
        {
            return std::nullopt;
        }
        auto const peer_id = field_binary(document, "peer_id");
        auto const offer_id = field_binary(document, "offer_id");
        if (!peer_id || peer_id->size() != 20U || !offer_id || offer_id->empty() || offer_id->size() > 64U)
        {
            return std::nullopt;
        }
        auto const& description = document[is_offer ? "offer" : "answer"];
        auto const type = is_offer ? "offer" : "answer";
        if (!description.IsObject() || field_text(description, "type") != type)
        {
            return std::nullopt;
        }
        auto const sdp = field_text(description, "sdp");
        if (!sdp || sdp->empty() || sdp->size() > 512U * 1024U)
        {
            return std::nullopt;
        }
        signal.kind = is_offer ? Signal::Kind::Offer : Signal::Kind::Answer;
        signal.peer_id = *peer_id;
        signal.offer_id = *offer_id;
        signal.sdp = *sdp;
    }
    return signal;
}

} // namespace tr_ws_tracker
