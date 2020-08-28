/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "libtransmission/transmission.h"
#include "libtransmission/utils.h"

#include "qt/Application.h"
#include "qt/InteropHelper.h"

#include <QtGlobal>

int tr_main(int argc, char** argv)
{
    InteropHelper::initialize();

#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    Application::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    Application::setAttribute(Qt::AA_UseHighDpiPixmaps);

    Application app(argc, argv);
    return app.exec();
}
