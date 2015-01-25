/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <iostream>

#ifdef _WIN32
 #include <windows.h>
 #include <shellapi.h>
#endif

#include <QApplication>
#include <QDataStream>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QObject>
#include <QPixmapCache>
#include <QSet>
#include <QStyle>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QMimeDatabase>
#include <QMimeType>
#endif

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_formatter

#include "utils.h"

/***
****
***/

#if defined(_WIN32) && QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
// Should be in QtWinExtras soon, but for now let's import it manually
extern QPixmap qt_pixmapFromWinHICON(HICON icon);
#endif

void
Utils::toStderr (const QString& str)
{
  std::cerr << qPrintable(str) << std::endl;
}

#ifdef _WIN32
namespace
{
  void
  addAssociatedFileIcon (const QFileInfo& fileInfo, UINT iconSize, QIcon& icon)
  {
    QString const pixmapCacheKey = QLatin1String ("tr_file_ext_")
                                 + QString::number (iconSize)
                                 + "_"
                                 + fileInfo.suffix ();

    QPixmap pixmap;
    if (!QPixmapCache::find (pixmapCacheKey, &pixmap))
      {
        const QString filename = fileInfo.fileName ();

        SHFILEINFO shellFileInfo;
        if (::SHGetFileInfoW (reinterpret_cast<const wchar_t*> (filename.utf16 ()), FILE_ATTRIBUTE_NORMAL,
                              &shellFileInfo, sizeof(shellFileInfo),
                              SHGFI_ICON | iconSize | SHGFI_USEFILEATTRIBUTES) != 0)
          {
            if (shellFileInfo.hIcon != NULL)
              {
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
                pixmap = qt_pixmapFromWinHICON (shellFileInfo.hIcon);
#else
                pixmap = QPixmap::fromWinHICON (shellFileInfo.hIcon);
#endif
                ::DestroyIcon (shellFileInfo.hIcon);
              }
          }

        QPixmapCache::insert (pixmapCacheKey, pixmap);
      }

    if (!pixmap.isNull ())
      icon.addPixmap (pixmap);
  }
} // namespace
#endif

#include <QDebug>
QIcon
Utils::guessMimeIcon (const QString& filename)
{
  static const QIcon fallback = qApp->style ()->standardIcon (QStyle::SP_FileIcon);

#ifdef _WIN32

  QIcon icon;

  if (!filename.isEmpty ())
    {
      const QFileInfo fileInfo (filename);

      addAssociatedFileIcon (fileInfo, SHGFI_SMALLICON, icon);
      addAssociatedFileIcon (fileInfo, 0, icon);
      addAssociatedFileIcon (fileInfo, SHGFI_LARGEICON, icon);
    }

  if (!icon.isNull ())
    return icon;

#elif QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

  QMimeDatabase mimeDb;
  QMimeType mimeType = mimeDb.mimeTypeForFile (filename, QMimeDatabase::MatchExtension);
  if (mimeType.isValid ())
    return QIcon::fromTheme (mimeType.iconName (), QIcon::fromTheme (mimeType.genericIconName (), fallback));

#else

  enum { DISK, DOCUMENT, PICTURE, VIDEO, ARCHIVE, AUDIO, APP, TYPE_COUNT };
  static QIcon fileIcons[TYPE_COUNT];
  static QSet<QString> suffixes[TYPE_COUNT];

  if (fileIcons[0].isNull ())
    {
      suffixes[DISK] << QString::fromLatin1("iso");
      fileIcons[DISK]= QIcon::fromTheme (QString::fromLatin1("media-optical"), fallback);

      const char * doc_types[] = {
        "abw", "csv", "doc", "dvi", "htm", "html", "ini", "log", "odp",
        "ods", "odt", "pdf", "ppt", "ps",  "rtf", "tex", "txt", "xml" };
      for (int i=0, n=sizeof(doc_types)/sizeof(doc_types[0]); i<n; ++i)
        suffixes[DOCUMENT] << QString::fromLatin1(doc_types[i]);
      fileIcons[DOCUMENT] = QIcon::fromTheme (QString::fromLatin1("text-x-generic"), fallback);

      const char * pic_types[] = {
        "bmp", "gif", "jpg", "jpeg", "pcx", "png", "psd", "ras", "tga", "tiff" };
      for (int i=0, n=sizeof(pic_types)/sizeof(pic_types[0]); i<n; ++i)
        suffixes[PICTURE] << QString::fromLatin1(pic_types[i]);
      fileIcons[PICTURE]  = QIcon::fromTheme (QString::fromLatin1("image-x-generic"), fallback);

      const char * vid_types[] = {
        "3gp", "asf", "avi", "mkv", "mov", "mpeg", "mpg", "mp4",
        "ogm", "ogv", "qt", "rm", "wmv" };
      for (int i=0, n=sizeof(vid_types)/sizeof(vid_types[0]); i<n; ++i)
        suffixes[VIDEO] << QString::fromLatin1(vid_types[i]);
      fileIcons[VIDEO] = QIcon::fromTheme (QString::fromLatin1("video-x-generic"), fallback);

      const char * arc_types[] = {
        "7z", "ace", "bz2", "cbz", "gz", "gzip", "lzma", "rar", "sft", "tar", "zip" };
      for (int i=0, n=sizeof(arc_types)/sizeof(arc_types[0]); i<n; ++i)
        suffixes[ARCHIVE] << QString::fromLatin1(arc_types[i]);
      fileIcons[ARCHIVE]  = QIcon::fromTheme (QString::fromLatin1("package-x-generic"), fallback);

      const char * aud_types[] = {
        "aac", "ac3", "aiff", "ape", "au", "flac", "m3u", "m4a", "mid", "midi", "mp2",
        "mp3", "mpc", "nsf", "oga", "ogg", "ra", "ram", "shn", "voc", "wav", "wma" };
      for (int i=0, n=sizeof(aud_types)/sizeof(aud_types[0]); i<n; ++i)
        suffixes[AUDIO] << QString::fromLatin1(aud_types[i]);
      fileIcons[AUDIO] = QIcon::fromTheme (QString::fromLatin1("audio-x-generic"), fallback);

      const char * exe_types[] = { "bat", "cmd", "com", "exe" };
      for (int i=0, n=sizeof(exe_types)/sizeof(exe_types[0]); i<n; ++i)
        suffixes[APP] << QString::fromLatin1(exe_types[i]);
      fileIcons[APP] = QIcon::fromTheme (QString::fromLatin1("application-x-executable"), fallback);
    }

  QString suffix (QFileInfo (filename).suffix ().toLower ());

  for (int i=0; i<TYPE_COUNT; ++i)
    if (suffixes[i].contains (suffix))
      return fileIcons[i];

#endif

  return fallback;
}

bool
Utils::isValidUtf8 (const char * s)
{
  int n;  // number of bytes in a UTF-8 sequence

  for (const char *c = s;  *c;  c += n)
    {
      if  ((*c & 0x80) == 0x00)    n = 1;        // ASCII
      else if ((*c & 0xc0) == 0x80) return false; // not valid
      else if ((*c & 0xe0) == 0xc0) n = 2;
      else if ((*c & 0xf0) == 0xe0) n = 3;
      else if ((*c & 0xf8) == 0xf0) n = 4;
      else if ((*c & 0xfc) == 0xf8) n = 5;
      else if ((*c & 0xfe) == 0xfc) n = 6;
      else return false;
      for  (int m = 1; m < n; m++)
        if  ((c[m] & 0xc0) != 0x80)
          return false;
    }

  return true;
}

QString
Utils::removeTrailingDirSeparator (const QString& path)
{
  const QFileInfo pathInfo (path);
  return pathInfo.fileName ().isEmpty () ? pathInfo.absolutePath () : pathInfo.absoluteFilePath ();
}
