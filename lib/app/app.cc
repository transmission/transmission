// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <lib/transmission/utils.h> // tr_lib_init()

#include "lib/app/app.h"
#include "lib/app/converters.h"

namespace tr::app
{
void init()
{
    tr_lib_init();
    tr_locale_set_global("");
    detail::register_app_converters();
}
} // namespace tr::app
