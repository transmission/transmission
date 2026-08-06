// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>

#include <QString>

#include "Transports.h"

namespace tr::interop
{

std::unique_ptr<Transport> make_transport([[maybe_unused]] QString const& config_dir)
{
#if defined(ENABLE_DBUS_INTEROP)
    return detail::make_dbus_transport(config_dir);
#elif defined(ENABLE_COM_INTEROP)
    return detail::make_com_transport(config_dir);
#else
#error "the Qt client needs a transport: see ENABLE_QT_DBUS_INTEROP and ENABLE_QT_COM_INTEROP"
#endif
}

} // namespace tr::interop
