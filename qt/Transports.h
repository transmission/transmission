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
// `config_dir` is where the D-Bus transport keeps its peer record.
// NOLINTNEXTLINE(readability-identifier-naming): spelled like the rest of tr::interop
[[nodiscard]] std::unique_ptr<Transport> make_transport(QString const& config_dir);

// Each concrete transport keeps its class to itself and offers only this factory.
// Declared here rather than beside make_transport(), so that the definition and the
// call are checked against one declaration instead of two.
namespace detail
{
#ifdef ENABLE_DBUS_INTEROP
// NOLINTNEXTLINE(readability-identifier-naming): spelled like the rest of tr::interop
[[nodiscard]] std::unique_ptr<Transport> make_dbus_transport(QString config_dir);
#endif
#ifdef ENABLE_COM_INTEROP
// NOLINTNEXTLINE(readability-identifier-naming): spelled like the rest of tr::interop
[[nodiscard]] std::unique_ptr<Transport> make_com_transport(QString const& config_dir);
#endif
} // namespace detail

} // namespace tr::interop
