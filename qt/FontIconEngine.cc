#define QT_PLUGIN
#define QT_STATICPLUGIN

#include <fmt/format.h>

#include <QIconEnginePlugin>
#include <QOperatingSystemVersion>
#include <QPainter>
#include <QPalette>

#include "FontIconEngine.h"

namespace
{

class FontIconPlugin : public QIconEnginePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QIconEngineFactoryInterface" FILE "FontIcons.json")

public:
    explicit FontIconPlugin(QObject* parent = nullptr)
        : QIconEnginePlugin(parent)
    {
#ifdef Q_OS_WIN
        if (auto const current = QOperatingSystemVersion::current(); current >= QOperatingSystemVersion::Windows10)
        {
            font_ = current >= QOperatingSystemVersion::Windows11 ? QStringLiteral("Segoe Fluent Icons") :
                                                                    QStringLiteral("Segoe MDL2 Assets");
            glyphs_ = {
                { QStringLiteral("application-exit"), QString(QChar::fromUcs4(0xf3b1)) },
                { QStringLiteral("dialog-ok"), QString(QChar::fromUcs4(0xe8fb)) },
                { QStringLiteral("document-new"), QString(QChar::fromUcs4(0xe8a5)) },
                { QStringLiteral("document-open"), QString(QChar::fromUcs4(0xe8e5)) },
                { QStringLiteral("document-properties"), QString(QChar::fromUcs4(0xf259)) }, // 0xec7a // 0xe70f
                { QStringLiteral("edit-clear"), QString(QChar::fromUcs4(0xe750)) },
                { QStringLiteral("edit-delete"), QString(QChar::fromUcs4(0xe74d)) },
                { QStringLiteral("edit-select-all"), QString(QChar::fromUcs4(0xe8b3)) },
                { QStringLiteral("emblem-important"), QString(QChar::fromUcs4(0xe814)) },
                { QStringLiteral("folder-open"), QString(QChar::fromUcs4(0xe838)) },
                { QStringLiteral("go-bottom"), QString(QChar::fromUcs4(0xf0ae)) },
                { QStringLiteral("go-down"), QString(QChar::fromUcs4(0xe74b)) },
                { QStringLiteral("go-top"), QString(QChar::fromUcs4(0xf0ad)) },
                { QStringLiteral("go-up"), QString(QChar::fromUcs4(0xe74a)) },
                { QStringLiteral("help-about"), QString(QChar::fromUcs4(0xe946)) },
                { QStringLiteral("help-contents"), QString(QChar::fromUcs4(0xe897)) },
                { QStringLiteral("insert-link"), QString(QChar::fromUcs4(0xe71b)) },
                { QStringLiteral("list-add"), QString(QChar::fromUcs4(0xe710)) },
                { QStringLiteral("list-remove"), QString(QChar::fromUcs4(0xe738)) },
                { QStringLiteral("media-playback-pause"), QString(QChar::fromUcs4(0xedb4)) },
                { QStringLiteral("media-playback-start"), QString(QChar::fromUcs4(0xedb5)) },
                { QStringLiteral("network-error"), QString() },
                { QStringLiteral("network-idle"), QString() },
                { QStringLiteral("network-receive"), QString(QChar::fromUcs4(0xe896)) },
                { QStringLiteral("network-transmit-receive"), QString(QChar::fromUcs4(0xe8ab)) },
                { QStringLiteral("network-transmit"), QString(QChar::fromUcs4(0xe898)) },
                { QStringLiteral("preferences-system"), QString(QChar::fromUcs4(0xe713)) },
                { QStringLiteral("process-stop"), QString(QChar::fromUcs4(0xe71a)) },
                { QStringLiteral("system-run"), QString(QChar::fromUcs4(0xe9f5)) },
                { QStringLiteral("view-refresh"), QString(QChar::fromUcs4(0xe72c)) },
                { QStringLiteral("view-sort-ascending"), QString(QChar::fromUcs4(0xe8cb)) },
            };
        }
#else
        font_ = QStringLiteral("Material Design Icons Desktop");
        glyphs_ = {
            { QStringLiteral("application-exit"), QString(QChar::fromUcs4(0xF0343)) },
            { QStringLiteral("dialog-ok"), QString(QChar::fromUcs4(0xF012C)) },
            { QStringLiteral("document-new"), QString(QChar::fromUcs4(0xF0224)) },
            { QStringLiteral("document-open"), QString(QChar::fromUcs4(0xF0EED)) },
            { QStringLiteral("document-properties"), QString(QChar::fromUcs4(0xF107C)) },
            { QStringLiteral("edit-clear"), QString(QChar::fromUcs4(0xF0B5C)) },
            { QStringLiteral("edit-delete"), QString(QChar::fromUcs4(0xF0A7A)) },
            { QStringLiteral("edit-select-all"), QString(QChar::fromUcs4(0xF0486)) },
            { QStringLiteral("emblem-important"), QString(QChar::fromUcs4(0xF0028)) },
            { QStringLiteral("folder-open"), QString(QChar::fromUcs4(0xF0DCF)) },
            { QStringLiteral("go-bottom"), QString(QChar::fromUcs4(0xF0792)) },
            { QStringLiteral("go-down"), QString(QChar::fromUcs4(0xF0045)) },
            { QStringLiteral("go-top"), QString(QChar::fromUcs4(0xF0795)) },
            { QStringLiteral("go-up"), QString(QChar::fromUcs4(0xF005D)) },
            { QStringLiteral("help-about"), QString(QChar::fromUcs4(0xF02FD)) },
            { QStringLiteral("help-contents"), QString(QChar::fromUcs4(0xF02D6)) },
            { QStringLiteral("insert-link"), QString(QChar::fromUcs4(0xF1100)) },
            { QStringLiteral("list-add"), QString(QChar::fromUcs4(0xF0415)) },
            { QStringLiteral("list-remove"), QString(QChar::fromUcs4(0xF0374)) },
            { QStringLiteral("media-playback-pause"), QString(QChar::fromUcs4(0xF03E4)) },
            { QStringLiteral("media-playback-start"), QString(QChar::fromUcs4(0xF0F1B)) },
            { QStringLiteral("network-error"), QString(QChar::fromUcs4(0xF0C9C)) },
            { QStringLiteral("network-idle"), QString(QChar::fromUcs4(0xF0C9D)) },
            { QStringLiteral("network-receive"), QString(QChar::fromUcs4(0xF0C66)) },
            { QStringLiteral("network-transmit-receive"), QString(QChar::fromUcs4(0xF04E1)) },
            { QStringLiteral("network-transmit"), QString(QChar::fromUcs4(0xF0CD8)) },
            { QStringLiteral("preferences-system"), QString(QChar::fromUcs4(0xF08BB)) },
            { QStringLiteral("process-stop"), QString(QChar::fromUcs4(0xF04DB)) },
            { QStringLiteral("system-run"), QString(QChar::fromUcs4(0xF08D6)) },
            { QStringLiteral("view-refresh"), QString(QChar::fromUcs4(0xF0450)) },
            { QStringLiteral("view-sort-ascending"), QString(QChar::fromUcs4(0xF04BA)) },
        };
#endif
    }

    QIconEngine* create(QString const& name) override
    {
        auto const glyph_it = glyphs_.find(name);
        return new FontIconEngine(font_, glyph_it != glyphs_.end() ? glyph_it->second : QString());
    }

private:
    QFont font_;
    std::map<QString, QString> glyphs_;
};

} // namespace

#include "FontIconEngine.moc"

Q_IMPORT_PLUGIN(FontIconPlugin)

FontIconEngine::FontIconEngine(QFont const& font, QString const& glyph)
    : font_(font)
    , glyph_(glyph)
{
}

bool FontIconEngine::isValidTheme()
{
#ifdef Q_OS_WIN
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows10;
#else
    return false;
#endif
}

QString FontIconEngine::themeName()
{
    return QStringLiteral("TrFontIcons");
}

void FontIconEngine::paint(QPainter* painter, QRect const& rect, QIcon::Mode mode, QIcon::State /*state*/)
{
    auto const palette = QPalette();
    auto color = QColor();
    switch (mode)
    {
    case QIcon::Normal:
        color = palette.color(QPalette::Inactive, QPalette::Text);
        break;
    case QIcon::Disabled:
        color = palette.color(QPalette::Disabled, QPalette::Text);
        break;
    case QIcon::Active:
        color = palette.color(QPalette::Active, QPalette::Text);
        break;
    case QIcon::Selected:
        color = palette.color(QPalette::Active, QPalette::HighlightedText);
        break;
    }

    auto font = font_;
    font.setPixelSize(rect.height());

    painter->save();

    painter->setPen(color);
    painter->setFont(font);
    painter->drawText(rect, Qt::AlignCenter, glyph_);

    painter->restore();
}

QPixmap FontIconEngine::pixmap(QSize const& size, QIcon::Mode mode, QIcon::State state)
{
    auto pixmap = QPixmap(size);
    pixmap.fill(Qt::transparent);

    auto painter = QPainter(&pixmap);
    paint(&painter, { {}, size }, mode, state);
    return pixmap;
}

QIconEngine* FontIconEngine::clone() const
{
    return new FontIconEngine(*this);
}

QString FontIconEngine::iconName()
{
    return glyph_;
}

bool FontIconEngine::isNull()
{
    return glyph_.isEmpty();
}
