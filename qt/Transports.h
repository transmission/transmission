// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>

#include <QString>

#include <libtransmission-app/interop.h>

namespace tr::interop
{

// The transport this build reaches another instance over, eg COM or D-Bus.
// Defined by whichever transport file this build compiles. CMake enables exactly
// one of them (ENABLE_QT_COM_INTEROP on Windows, ENABLE_QT_DBUS_INTEROP elsewhere).
// `config_dir` is where the D-Bus transport keeps its peer record.
// NOLINTNEXTLINE(readability-identifier-naming): spelled like the rest of tr::interop
[[nodiscard]] std::unique_ptr<Transport> make_transport(QString const& config_dir);

} // namespace tr::interop
