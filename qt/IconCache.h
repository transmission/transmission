// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#if !defined(_WIN32)
#include <unordered_set>
#endif

#include <optional>
#include <unordered_map>

#include <QFileIconProvider>
#include <QIcon>
#include <QString>
#include <QStyle>

#include "Utils.h" // std::hash<QString>()

#if defined(_WIN32)
class QFileInfo;
#endif

class QModelIndex;

class IconCache
{
public:
    static IconCache& get();

    [[nodiscard]] constexpr auto const& folderIcon() const noexcept
    {
        return folder_icon_;
    }

    [[nodiscard]] constexpr auto const& fileIcon() const noexcept
    {
        return file_icon_;
    }

    QIcon guessMimeIcon(QString const& filename, QIcon fallback = {}) const;
    QIcon getMimeTypeIcon(QString const& mime_type, bool multifile) const;

    QIcon getThemeIcon(QString const& name, std::optional<QStyle::StandardPixmap> const& fallback = {}) const;

protected:
    IconCache() = default;

private:
    QIcon const folder_icon_ = QFileIconProvider().icon(QFileIconProvider::Folder);
    QIcon const file_icon_ = QFileIconProvider().icon(QFileIconProvider::File);

    mutable std::unordered_map<QString, QIcon> name_to_icon_;
    mutable std::unordered_map<QString, QIcon> name_to_emblem_icon_;

#if defined(_WIN32)
    void addAssociatedFileIcon(QFileInfo const& file_info, unsigned int icon_size, QIcon& icon) const;
#else
    mutable std::unordered_set<QString> suffixes_;
    mutable std::unordered_map<QString, QIcon> ext_to_icon_;
    QIcon getMimeIcon(QString const& filename) const;
#endif

    QIcon getThemeIcon(
        QString const& name,
        QString const& fallbackName,
        std::optional<QStyle::StandardPixmap> const& fallbackPixmap) const;
};
