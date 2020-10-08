/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#if defined(_WIN32)
#include <windows.h> // UINT
class QFileInfo;
#else
#include <unordered_map>
#include <unordered_set>
#include "Utils.h" // std::hash<QString>()
#endif

#include <QIcon>
#include <QString>

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
    IconCache();

private:
    QIcon const folder_icon_;
    QIcon const file_icon_;

    mutable std::unordered_map<QString, QIcon> name_to_icon_;
    mutable std::unordered_map<QString, QIcon> name_to_emblem_icon_;

#if defined(_WIN32)
    void addAssociatedFileIcon(QFileInfo const& file_info, UINT icon_size, QIcon& icon) const;
#else
    mutable std::unordered_set<QString> suffixes_;
    mutable std::unordered_map<QString, QIcon> ext_to_icon_;
    QIcon getMimeIcon(QString const& filename) const;
#endif
};
