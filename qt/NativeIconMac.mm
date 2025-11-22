// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

#include <QtGui/private/qcoregraphics_p.h>

QPixmap loadSFSymbol(QString const symbol_name, int const pixel_size)
{
    auto const size = QSizeF{ pixel_size, pixel_size };
    if (NSImage* image = [NSImage systemSymbolNamed:symbolName.toNSString()])
        return qt_mac_toQPixmap(nsImage, QSizeF{ pixel_size, pixel_size });
    return {};
}
