/****************************************************************************
**
** Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
** Contact: Qt Software Information (qt-info@nokia.com)
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** Commercial Usage
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain
** additional rights. These rights are described in the Nokia Qt LGPL
** Exception version 1.0, included in the file LGPL_EXCEPTION.txt in this
** package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at qt-sales@nokia.com.
**
****************************************************************************/


#include "qticonloader.h"
#include <QtGui/QPixmapCache>

#include <QtCore/QList>
#include <QtCore/QHash>
#include <QtCore/QDir>
#include <QtCore/QString>
#include <QtCore/QLibrary>
#include <QtCore/QSettings>
#include <QtCore/QTextStream>

#ifdef Q_WS_X11

class QIconTheme
{
public:
    QIconTheme(QHash <int, QString> dirList, QStringList parents) :
            _dirList(dirList), _parents(parents), _valid(true){ }
    QIconTheme() : _valid(false){ }
    QHash <int, QString> dirList() {return _dirList;}
    QStringList parents() {return _parents;}
    bool isValid() {return _valid;}

private:
    QHash <int, QString> _dirList;
    QStringList _parents;
    bool _valid;
};

class QtIconLoaderImplementation
{
public:
    QtIconLoaderImplementation();
    QPixmap findIcon(int size, const QString &name) const;

private:
    QIconTheme parseIndexFile(const QString &themeName) const;
    void lookupIconTheme() const;
    QPixmap findIconHelper(int size,
                           const QString &themeName,
                           const QString &iconName,
                           QStringList &visited) const;
    mutable QString themeName;
    mutable QStringList iconDirs;
    mutable QHash <QString, QIconTheme> themeList;
};

Q_GLOBAL_STATIC(QtIconLoaderImplementation, iconLoaderInstance)
#endif

/*!

    Returns the standard icon for the given icon /a name
    as specified in the freedesktop icon spec
    http://standards.freedesktop.org/icon-naming-spec/icon-naming-spec-latest.html

    /a fallback is an optional argument to specify the icon to be used if
    no icon is found on the platform. This is particularily useful for
    crossplatform code.

*/
QIcon QtIconLoader::icon(const QString &name, const QIcon &fallback)
{
    QIcon icon;
#ifdef Q_WS_X11
    QString pngExtension(QLatin1String(".png"));
    QList<int> iconSizes;
    iconSizes << 16 << 24 << 32 << 48 << 64;
    Q_FOREACH (int size, iconSizes) {
        icon.addPixmap(iconLoaderInstance()->findIcon(size, name + pngExtension));
    }
#endif
    if (icon.isNull())
        icon = fallback;
    Q_UNUSED(name);
    return icon;
}

#ifdef Q_WS_X11

QtIconLoaderImplementation::QtIconLoaderImplementation()
{
    lookupIconTheme();
}

extern "C" {
    struct GConfClient;
    struct GError;
    typedef void (*Ptr_g_type_init)();
    typedef GConfClient* (*Ptr_gconf_client_get_default)();
    typedef char* (*Ptr_gconf_client_get_string)(GConfClient*, const char*, GError **);
    typedef void (*Ptr_g_object_unref)(void *);
    typedef void (*Ptr_g_error_free)(GError *);
    typedef void (*Ptr_g_free)(void*);
    static Ptr_g_type_init p_g_type_init = 0;
    static Ptr_gconf_client_get_default p_gconf_client_get_default = 0;
    static Ptr_gconf_client_get_string p_gconf_client_get_string = 0;
    static Ptr_g_object_unref p_g_object_unref = 0;
    static Ptr_g_error_free p_g_error_free = 0;
    static Ptr_g_free p_g_free = 0;
}


static int kdeVersion()
{
    static int version = qgetenv("KDE_SESSION_VERSION").toInt();
    return version;
}

static QString kdeHome()
{
    static QString kdeHomePath;
    if (kdeHomePath.isEmpty()) {
        kdeHomePath = QFile::decodeName(qgetenv("KDEHOME"));
        if (kdeHomePath.isEmpty()) {
            int kdeSessionVersion = kdeVersion();
            QDir homeDir(QDir::homePath());
            QString kdeConfDir(QLatin1String("/.kde"));
            if (4 == kdeSessionVersion && homeDir.exists(QLatin1String(".kde4")))
                kdeConfDir = QLatin1String("/.kde4");
            kdeHomePath = QDir::homePath() + kdeConfDir;
        }
    }
    return kdeHomePath;
}

void QtIconLoaderImplementation::lookupIconTheme() const
{
    
#ifdef Q_WS_X11
    QString dataDirs = QFile::decodeName(getenv("XDG_DATA_DIRS"));
    if (dataDirs.isEmpty())
        dataDirs = QLatin1String("/usr/local/share/:/usr/share/");
    
    dataDirs.prepend(QDir::homePath() + QLatin1String("/:"));
    iconDirs = dataDirs.split(QLatin1Char(':'));
    
    // If we are running GNOME we resolve and use GConf. In all other
    // cases we currently use the KDE icon theme
    
    if (qgetenv("DESKTOP_SESSION") == "gnome" ||
        !qgetenv("GNOME_DESKTOP_SESSION_ID").isEmpty()) {
        
        if (themeName.isEmpty()) {
            // Resolve glib and gconf
            
            p_g_type_init =              (Ptr_g_type_init)QLibrary::resolve(QLatin1String("gobject-2.0"), 0, "g_type_init");
            p_gconf_client_get_default = (Ptr_gconf_client_get_default)QLibrary::resolve(QLatin1String("gconf-2"), 4, "gconf_client_get_default");
            p_gconf_client_get_string =  (Ptr_gconf_client_get_string)QLibrary::resolve(QLatin1String("gconf-2"), 4, "gconf_client_get_string");
            p_g_object_unref =           (Ptr_g_object_unref)QLibrary::resolve(QLatin1String("gobject-2.0"), 0, "g_object_unref");
            p_g_error_free =             (Ptr_g_error_free)QLibrary::resolve(QLatin1String("glib-2.0"), 0, "g_error_free");
            p_g_free =                   (Ptr_g_free)QLibrary::resolve(QLatin1String("glib-2.0"), 0, "g_free");
            
            if (p_g_type_init && p_gconf_client_get_default &&
                p_gconf_client_get_string && p_g_object_unref &&
                p_g_error_free && p_g_free) {
                
                p_g_type_init();
                GConfClient* client = p_gconf_client_get_default();
                GError *err = 0;
                
                char *str = p_gconf_client_get_string(client, "/desktop/gnome/interface/icon_theme", &err);
                if (!err) {
                    themeName = QString::fromUtf8(str);
                    p_g_free(str);
                }
                
                p_g_object_unref(client);
                if (err)
                    p_g_error_free (err);
            }
            if (themeName.isEmpty())
                themeName = QLatin1String("gnome");
        }
        
        if (!themeName.isEmpty())
            return;
    }
    
    // KDE (and others)
    if (dataDirs.isEmpty())
        dataDirs = QLatin1String("/usr/local/share/:/usr/share/");
    
    dataDirs += QLatin1Char(':') + kdeHome() + QLatin1String("/share");
    dataDirs.prepend(QDir::homePath() + QLatin1String("/:"));
    QStringList kdeDirs = QFile::decodeName(getenv("KDEDIRS")).split(QLatin1Char(':'));
    Q_FOREACH (const QString dirName, kdeDirs)
        dataDirs.append(QLatin1Char(':') + dirName + QLatin1String("/share"));
    iconDirs = dataDirs.split(QLatin1Char(':'));
    
    QFileInfo fileInfo(QLatin1String("/usr/share/icons/default.kde"));
    QDir dir(fileInfo.canonicalFilePath());
    QString kdeDefault = kdeVersion() >= 4 ? QString::fromLatin1("oxygen") : QString::fromLatin1("crystalsvg");
    QString defaultTheme = fileInfo.exists() ? dir.dirName() : kdeDefault;
    QSettings settings(kdeHome() + QLatin1String("/share/config/kdeglobals"), QSettings::IniFormat);
    settings.beginGroup(QLatin1String("Icons"));
    themeName = settings.value(QLatin1String("Theme"), defaultTheme).toString();
#endif
}

QIconTheme QtIconLoaderImplementation::parseIndexFile(const QString &themeName) const
{
    QIconTheme theme;
    QFile themeIndex;
    QStringList parents;
    QHash <int, QString> dirList;

    for ( int i = 0 ; i < iconDirs.size() && !themeIndex.exists() ; ++i) {
        const QString &contentDir = QLatin1String(iconDirs[i].startsWith(QDir::homePath()) ? "/.icons/" : "/icons/");
        themeIndex.setFileName(iconDirs[i] + contentDir + themeName + QLatin1String("/index.theme"));
    }

    if (themeIndex.exists()) {
        QSettings indexReader(themeIndex.fileName(), QSettings::IniFormat);
        Q_FOREACH (const QString &key, indexReader.allKeys()) {
            if (key.endsWith("/Size")) {
                if (int size = indexReader.value(key).toInt())
                    dirList.insertMulti(size, key.left(key.size() - 5));
            }
        }

        // Parent themes provide fallbacks for missing icons
        parents = indexReader.value(QLatin1String("Icon Theme/Inherits")).toStringList();
    }

    if (kdeVersion() >= 3) {
        QFileInfo fileInfo(QLatin1String("/usr/share/icons/default.kde"));
        QDir dir(fileInfo.canonicalFilePath());
        QString defaultKDETheme = dir.exists() ? dir.dirName() : kdeVersion() == 3 ?
                                  QString::fromLatin1("crystalsvg") : QString::fromLatin1("oxygen");
        if (!parents.contains(defaultKDETheme) && themeName != defaultKDETheme)
            parents.append(defaultKDETheme);
    } else if (parents.isEmpty() && themeName != QLatin1String("hicolor")) {
        parents.append(QLatin1String("hicolor"));
    }
    
    theme = QIconTheme(dirList, parents);
    return theme;
}

QPixmap QtIconLoaderImplementation::findIconHelper(int size, const QString &themeName,
                                                   const QString &iconName, QStringList &visited) const
{
    QPixmap pixmap;
    
    if (!themeName.isEmpty()) {
        visited << themeName;
        QIconTheme theme = themeList.value(themeName);
        
        if (!theme.isValid()) {
            theme = parseIndexFile(themeName);
            themeList.insert(themeName, theme);
        }
        
        if (!theme.isValid())
            return QPixmap();
        
        QList <QString> subDirs = theme.dirList().values(size);
        
        for ( int i = 0 ; i < iconDirs.size() ; ++i) {
            for ( int j = 0 ; j < subDirs.size() ; ++j) {
                QString contentDir = (iconDirs[i].startsWith(QDir::homePath())) ?
                                     QLatin1String("/.icons/") : QLatin1String("/icons/");
                QString fileName = iconDirs[i] + contentDir + themeName + QLatin1Char('/') + subDirs[j] + QLatin1Char('/') + iconName;
                QFile file(fileName);
                if (file.exists())
                    pixmap.load(fileName);
                if (!pixmap.isNull())
                    break;
            }
        }
        
        if (pixmap.isNull()) {
            QStringList parents = theme.parents();
            //search recursively through inherited themes
            for (int i = 0 ; pixmap.isNull() && i < parents.size() ; ++i) {
                QString parentTheme = parents[i].trimmed();
                if (!visited.contains(parentTheme)) //guard against endless recursion
                    pixmap = findIconHelper(size, parentTheme, iconName, visited);
            }
        }
    }
    return pixmap;
}

QPixmap QtIconLoaderImplementation::findIcon(int size, const QString &name) const
{
    QPixmap pixmap;
    QString pixmapName = QLatin1String("$qt") + name + QString::number(size);
    if (QPixmapCache::find(pixmapName, pixmap))
        return pixmap;
    
    if (!themeName.isEmpty()) {
        QStringList visited;
        pixmap = findIconHelper(size, themeName, name, visited);
    }
    QPixmapCache::insert(pixmapName, pixmap);
    return pixmap;
}
#endif //Q_WS_X11
