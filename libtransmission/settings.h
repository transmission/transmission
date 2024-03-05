// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <cstddef> // for size_t
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/log.h" // for tr_log_level
#include "libtransmission/net.h" // for tr_port, tr_tos_t
#include "libtransmission/open-files.h" // for tr_open_files::Preallocation
#include "libtransmission/peer-io.h" // tr_preferred_transport
#include "libtransmission/quark.h"
#include "libtransmission/variant.h"

namespace libtransmission
{

class Settings
{
public:
    void load(tr_variant const& src);

    [[nodiscard]] tr_variant save() const;

protected:
    using field_key_type = tr_quark;
    using field_mapped_type = std::variant<
        bool*,
        double*,
        size_t*,
        std::chrono::milliseconds*,
        std::string*,
        tr_encryption_mode*,
        tr_log_level*,
        tr_mode_t*,
        tr_open_files::Preallocation*,
        tr_port*,
        tr_preferred_transport*,
        tr_tos_t*,
        tr_verify_added_mode*>;
    using field_value_type = std::pair<field_key_type const, field_mapped_type>;
    using Fields = std::vector<field_value_type>;

    Settings() = default;

    [[nodiscard]] virtual Fields fields() = 0;
};
} // namespace libtransmission
