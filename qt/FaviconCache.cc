
// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission-app/favicon-cache.h>

#include <QApplication>
#include <QPixmap>
#include <QStandardPaths>

using namespace tr::app;
using Icon = QPixmap;

template<>
Icon FaviconCache<Icon>::create_from_file(std::string_view filename) const
{
    auto icon = QPixmap{};
    if (!icon.load(QString::fromUtf8(std::data(filename), std::size(filename))))
    {
        return {};
    }

    return icon.scaled({ Width, Height }, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

template<>
Icon FaviconCache<Icon>::create_from_data(void const* data, size_t datalen) const
{
    auto icon = QPixmap{};
    if (!icon.loadFromData(static_cast<uchar const*>(data), datalen))
    {
        return {};
    }

    return icon.scaled({ Width, Height }, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

template<>
std::string FaviconCache<Icon>::app_cache_dir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation).toStdString();
}

template<>
void FaviconCache<Icon>::add_to_ui_thread(std::function<void()> idlefunc)
{
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    QMetaObject::invokeMethod(qApp, std::move(idlefunc), Qt::QueuedConnection);
}
