// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <qtguiglobal.h>

#if QT_CONFIG(accessibility)

#include <QAccessibleWidget>

class SqueezeLabel;

class AccessibleSqueezeLabel
    : public QAccessibleWidget
    , public QAccessibleTextInterface
{
public:
    explicit AccessibleSqueezeLabel(QWidget* widget);

    // QAccessibleWidget
    [[nodiscard]] QString text(QAccessible::Text kind) const override;
    [[nodiscard]] QAccessible::State state() const override;
    void* interface_cast(QAccessible::InterfaceType ifaceType) override;

    // QAccessibleTextInterface
    void selection(int selectionIndex, int* startOffset, int* endOffset) const override;
    [[nodiscard]] int selectionCount() const override;
    void addSelection(int startOffset, int endOffset) override;
    void removeSelection(int selectionIndex) override;
    void setSelection(int selectionIndex, int startOffset, int endOffset) override;
    [[nodiscard]] int cursorPosition() const override;
    void setCursorPosition(int position) override;
    [[nodiscard]] QString text(int startOffset, int endOffset) const override;
    [[nodiscard]] int characterCount() const override;
    [[nodiscard]] QRect characterRect(int offset) const override;
    [[nodiscard]] int offsetAtPoint(QPoint const& point) const override;
    void scrollToSubstring(int startIndex, int endIndex) override;
    QString attributes(int offset, int* startOffset, int* endOffset) const override;

private:
    [[nodiscard]] SqueezeLabel* label() const;
};

#endif // QT_CONFIG(accessibility)
