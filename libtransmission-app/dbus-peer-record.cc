// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <libtransmission/file.h> // tr_sys_path_get_info()
#include <libtransmission/log.h>
#include <libtransmission/tr-strbuf.h> // tr_pathbuf
#include <libtransmission/utils.h> // tr_file_read(), tr_file_save()

#include "libtransmission-app/dbus-peer-record.h"
#include "libtransmission-app/interop-names.h"

using namespace std::string_view_literals;

namespace tr::interop
{
namespace
{

// A bus name alone is capped at 255, so every record we write is far shorter than this.
// Anything bigger is not a record.
auto constexpr MaxRecordLen = size_t{ 4096U };

[[nodiscard]] tr_pathbuf record_path(std::string_view const config_dir)
{
    return tr_pathbuf{ config_dir, '/', PeerRecordFilename };
}

auto constexpr MaxNameLen = size_t{ 255U };

[[nodiscard]] constexpr bool is_name_char(char const ch, bool const allow_hyphen) noexcept
{
    return ('A' <= ch && ch <= 'Z') || ('a' <= ch && ch <= 'z') || ('0' <= ch && ch <= '9') || ch == '_' ||
        (allow_hyphen && ch == '-');
}

// Two or more dot-separated elements, none empty.
// The two flags cover the only difference between the bus-name grammar and the
// interface-name grammar: a hyphen, and a leading digit.
[[nodiscard]] constexpr bool is_dotted_name(
    std::string_view const name,
    bool const allow_hyphen,
    bool const allow_leading_digit) noexcept
{
    if (std::empty(name) || std::size(name) > MaxNameLen || name.front() == '.' || name.back() == '.')
    {
        return false;
    }

    auto n_elements = size_t{ 1U };
    auto starting_element = true;

    for (auto const ch : name)
    {
        if (ch == '.')
        {
            if (starting_element)
            {
                return false;
            }

            ++n_elements;
            starting_element = true;
            continue;
        }

        if (!is_name_char(ch, allow_hyphen) || (starting_element && !allow_leading_digit && ch >= '0' && ch <= '9'))
        {
            return false;
        }

        starting_element = false;
    }

    return n_elements >= 2U;
}

// A colon, then the elements of a bus name. Unlike an interface's, these may lead with a digit.
[[nodiscard]] constexpr bool is_unique_bus_name(std::string_view const name) noexcept
{
    return !std::empty(name) && name.front() == ':' && is_dotted_name(name.substr(1U), true, true);
}

// Slash-separated elements, none empty, no hyphens. The root alone also counts.
[[nodiscard]] constexpr bool is_object_path(std::string_view const path) noexcept
{
    if (std::empty(path) || path.front() != '/')
    {
        return false;
    }

    if (std::size(path) == 1U)
    {
        return true;
    }

    if (path.back() == '/')
    {
        return false;
    }

    auto starting_element = true;

    for (auto const ch : path.substr(1U))
    {
        if (ch == '/')
        {
            if (starting_element)
            {
                return false;
            }

            starting_element = true;
            continue;
        }

        if (!is_name_char(ch, false))
        {
            return false;
        }

        starting_element = false;
    }

    return true;
}

} // namespace

std::optional<DBusPeerRecord> read_dbus_peer_record(std::string_view const config_dir)
{
    auto const path = record_path(config_dir);

    // Most launches find no record, because nothing is running. Check quietly first.
    // tr_file_read() logs every failure, and a missing record is the normal case.
    if (!tr_sys_path_get_info(path))
    {
        return {};
    }

    auto contents = std::vector<char>{};
    if (!tr_file_read(path, contents) || std::size(contents) > MaxRecordLen)
    {
        return {};
    }

    auto record = DBusPeerRecord{};
    auto remainder = std::string_view{ std::data(contents), std::size(contents) };
    auto line = std::string_view{};
    while (tr_strv_sep(&remainder, &line, '\n'))
    {
        auto const sep = line.find('=');
        if (sep == std::string_view::npos)
        {
            continue;
        }

        auto const key = tr_strv_strip(line.substr(0U, sep));
        auto const value = tr_strv_strip(line.substr(sep + 1U));

        // Unknown keys are ignored, so a newer version can add one without breaking this reader.
        if (key == "bus-name"sv)
        {
            record.bus_name = value;
        }
        else if (key == "interface"sv)
        {
            record.interface = value;
        }
        else if (key == "path"sv)
        {
            record.path = value;
        }
    }

    // We only ever record a unique bus name, and a D-Bus library only accepts names
    // matching the grammars above.
    // We discard a partial record rather than fill it in from interop-names.h.
    // Half a record is no evidence that anyone is listening.
    auto const usable = is_unique_bus_name(record.bus_name) && is_dotted_name(record.interface, false, false) &&
        is_object_path(record.path);

    if (!usable)
    {
        return {};
    }

    return record;
}

std::optional<DBusPeerRecord> select_dbus_peer(
    std::string_view const config_dir,
    std::string_view const self,
    DBusNameOwner const& name_owner)
{
    if (auto const recorded = read_dbus_peer_record(config_dir);
        recorded && recorded->bus_name != self && !std::empty(name_owner(recorded->bus_name)))
    {
        return recorded;
    }

    if (auto const owner = name_owner(TR_INTEROP_DBUS_SERVICE_NAME); !std::empty(owner) && owner != self)
    {
        return DBusPeerRecord{ TR_INTEROP_DBUS_SERVICE_NAME, TR_INTEROP_DBUS_INTERFACE_NAME, TR_INTEROP_DBUS_OBJECT_PATH };
    }

    return {};
}

void publish_dbus_peer(
    std::string_view const config_dir,
    std::string_view const unique_name,
    std::function<bool()> const& register_object,
    std::function<bool()> const& claim_well_known_name)
{
    if (!register_object())
    {
        return;
    }

    if (!claim_well_known_name())
    {
        tr_logAddInfo(fmt::format("another session owns {:s}", TR_INTEROP_DBUS_SERVICE_NAME));
    }

    auto error = tr_error{};
    auto const record = DBusPeerRecord{ std::string{ unique_name },
                                        TR_INTEROP_DBUS_INTERFACE_NAME,
                                        TR_INTEROP_DBUS_OBJECT_PATH };
    if (!write_dbus_peer_record(config_dir, record, &error))
    {
        tr_logAddWarn(fmt::format("couldn't record this session in '{:s}': {:s}", config_dir, error.message()));
    }
}

bool write_dbus_peer_record(std::string_view const config_dir, DBusPeerRecord const& record, tr_error* const error)
{
    auto const contents = fmt::format(
        "bus-name={:s}\ninterface={:s}\npath={:s}\n",
        record.bus_name,
        record.interface,
        record.path);
    return tr_file_save(record_path(config_dir), contents, error);
}

} // namespace tr::interop
