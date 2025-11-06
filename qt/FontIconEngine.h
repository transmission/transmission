#pragma once

#include <QFont>
#include <QIconEngine>
#include <QString>

class FontIconEngine : public QIconEngine
{
public:
    FontIconEngine(QFont const& font, QString const& glyph);

    static bool isValidTheme();
    static QString themeName();

    // QIconEngine
    void paint(QPainter* painter, QRect const& rect, QIcon::Mode mode, QIcon::State state) override;
    QPixmap pixmap(QSize const& size, QIcon::Mode mode, QIcon::State state) override;
    [[nodiscard]] QIconEngine* clone() const override;
    QString iconName() override;
    bool isNull() override;

private:
    QFont font_;
    QString glyph_;
};
