// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifndef TR_QTCOMPAT_H
#define TR_QTCOMPAT_H

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define IF_QT6(ThenValue, ElseValue) ThenValue
#else
#define IF_QT6(ThenValue, ElseValue) ElseValue
#endif

#endif //TR_QTCOMPAT_H
