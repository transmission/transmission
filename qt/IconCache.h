/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#if !defined(_WIN32)
#include <unordered_set>
#endif

#include <unordered_map>

#include <QFileIconProvider>
#include <QIcon>
#include <QString>

#include "Utils.h" // std::hash<QString>()

#if defined(_WIN32)
class QFileInfo;
#endif

class QModelIndex;

class IconCache
{
public:
    static IconCache& get();

    QIcon folderIcon() const { return folder_icon_; }
    QIcon fileIcon() const { return file_icon_; }
    QIcon guessMimeIcon(QString const& filename, QIcon fallback = {}) const;
    QIcon getMimeTypeIcon(QString const& mime_type, bool multifile) const;

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
};
