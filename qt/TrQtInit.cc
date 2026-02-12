// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "TrQtInit.h"

#include <lib/app/app.h>

#include "VariantHelpers.h"

namespace trqt
{

void trqt_init()
{
    tr::app::init();
    trqt::variant_helpers::register_qt_converters();
}

} // namespace trqt
