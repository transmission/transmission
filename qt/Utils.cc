/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifdef _WIN32
 #include <windows.h>
 #include <shellapi.h>
#endif

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QDataStream>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
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

#include "Utils.h"

/***
****
***/

#if defined(_WIN32) && QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
// Should be in QtWinExtras soon, but for now let's import it manually
extern QPixmap qt_pixmapFromWinHICON(HICON icon);
#endif

#ifdef _WIN32
namespace
{
  void
  addAssociatedFileIcon (const QFileInfo& fileInfo, UINT iconSize, QIcon& icon)
  {
    QString const pixmapCacheKey = QLatin1String ("tr_file_ext_")
                                 + QString::number (iconSize)
                                 + QLatin1Char ('_')
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
      for (const char * t: doc_types)
        suffixes[DOCUMENT] << QString::fromLatin1(t);
      fileIcons[DOCUMENT] = QIcon::fromTheme (QString::fromLatin1("text-x-generic"), fallback);

      const char * pic_types[] = {
        "bmp", "gif", "jpg", "jpeg", "pcx", "png", "psd", "ras", "tga", "tiff" };
      for (const char * t: pic_types)
        suffixes[PICTURE] << QString::fromLatin1(t);
      fileIcons[PICTURE]  = QIcon::fromTheme (QString::fromLatin1("image-x-generic"), fallback);

      const char * vid_types[] = {
        "3gp", "asf", "avi", "mkv", "mov", "mpeg", "mpg", "mp4",
        "ogm", "ogv", "qt", "rm", "wmv" };
      for (const char * t: vid_types)
        suffixes[VIDEO] << QString::fromLatin1(t);
      fileIcons[VIDEO] = QIcon::fromTheme (QString::fromLatin1("video-x-generic"), fallback);

      const char * arc_types[] = {
        "7z", "ace", "bz2", "cbz", "gz", "gzip", "lzma", "rar", "sft", "tar", "zip" };
      for (const char * t: arc_types)
        suffixes[ARCHIVE] << QString::fromLatin1(t);
      fileIcons[ARCHIVE]  = QIcon::fromTheme (QString::fromLatin1("package-x-generic"), fallback);

      const char * aud_types[] = {
        "aac", "ac3", "aiff", "ape", "au", "flac", "m3u", "m4a", "mid", "midi", "mp2",
        "mp3", "mpc", "nsf", "oga", "ogg", "ra", "ram", "shn", "voc", "wav", "wma" };
      for (const char * t: aud_types)
        suffixes[AUDIO] << QString::fromLatin1(t);
      fileIcons[AUDIO] = QIcon::fromTheme (QString::fromLatin1("audio-x-generic"), fallback);

      const char * exe_types[] = { "bat", "cmd", "com", "exe" };
      for (const char * t: exe_types)
        suffixes[APP] << QString::fromLatin1(t);
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

int
Utils::measureViewItem (QAbstractItemView * view, const QString& text)
{
  QStyleOptionViewItemV4 option;
  option.initFrom (view);
  option.features = QStyleOptionViewItemV2::HasDisplay;
  option.text = text;
  option.textElideMode = Qt::ElideNone;
  option.font = view->font ();

  return view->style ()->sizeFromContents (QStyle::CT_ItemViewItem, &option,
    QSize (QWIDGETSIZE_MAX, QWIDGETSIZE_MAX), view).width ();
}

int
Utils::measureHeaderItem (QHeaderView * view, const QString& text)
{
  QStyleOptionHeader option;
  option.initFrom (view);
  option.text = text;
  option.sortIndicator = view->isSortIndicatorShown () ? QStyleOptionHeader::SortDown :
    QStyleOptionHeader::None;

  return view->style ()->sizeFromContents (QStyle::CT_HeaderSection, &option, QSize (), view).width ();
}

QColor
Utils::getFadedColor (const QColor& color)
{
  QColor fadedColor (color);
  fadedColor.setAlpha (128);
  return fadedColor;
}
