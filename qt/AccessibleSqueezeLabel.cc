// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <qtguiglobal.h>

#if QT_CONFIG(accessibility)

#include <QMetaProperty>

#include "AccessibleSqueezeLabel.h"
#include "SqueezeLabel.h"

AccessibleSqueezeLabel::AccessibleSqueezeLabel(QWidget* widget)
    : QAccessibleWidget(widget, QAccessible::EditableText)
{
}

QString AccessibleSqueezeLabel::text(QAccessible::Text kind) const
{
    switch (kind)
    {
    case QAccessible::Value:
        return label()->text();

    case QAccessible::Description:
        return !label()->accessibleDescription().isEmpty() || label()->toolTip() != label()->text() ?
            QAccessibleWidget::text(kind) :
            QString{};

    default:
        return QAccessibleWidget::text(kind);
    }
}

QAccessible::State AccessibleSqueezeLabel::state() const
{
    auto result = QAccessibleWidget::state();
    result.readOnly = true;
    result.selectableText = true;
    return result;
}

void* AccessibleSqueezeLabel::interface_cast(QAccessible::InterfaceType ifaceType)
{
    if (ifaceType == QAccessible::TextInterface)
    {
        return static_cast<QAccessibleTextInterface*>(this);
    }

    return QAccessibleWidget::interface_cast(ifaceType);
}

void AccessibleSqueezeLabel::selection(int selectionIndex, int* startOffset, int* endOffset) const
{
    if (selectionIndex != 0)
    {
        *startOffset = 0;
        *endOffset = 0;
        return;
    }

    *startOffset = label()->selectionStart();
    *endOffset = *startOffset + label()->selectedText().size();
}

int AccessibleSqueezeLabel::selectionCount() const
{
    return label()->hasSelectedText() ? 1 : 0;
}

void AccessibleSqueezeLabel::addSelection(int startOffset, int endOffset)
{
    setSelection(0, startOffset, endOffset);
}

void AccessibleSqueezeLabel::removeSelection(int selectionIndex)
{
    setSelection(selectionIndex, 0, 0);
}

void AccessibleSqueezeLabel::setSelection(int selectionIndex, int startOffset, int endOffset)
{
    if (selectionIndex != 0 || startOffset > endOffset)
    {
        return;
    }

    label()->setSelection(startOffset, endOffset - startOffset);
}

int AccessibleSqueezeLabel::cursorPosition() const
{
    // NOTE: Due to QLabel implementation specifics, this will return -1 unless some part of text is selected :(
    return label()->selectionStart();
}

void AccessibleSqueezeLabel::setCursorPosition(int position)
{
    setSelection(0, position, position);
}

QString AccessibleSqueezeLabel::text(int startOffset, int endOffset) const
{
    return startOffset > endOffset ? QString{} : label()->text().mid(startOffset, endOffset - startOffset);
}

int AccessibleSqueezeLabel::characterCount() const
{
    return label()->text().size();
}

QRect AccessibleSqueezeLabel::characterRect(int /*offset*/) const
{
    // NOTE: Can't be easily implemented as needed info is internal to QLabel :(
    return {};
}

int AccessibleSqueezeLabel::offsetAtPoint(QPoint const& /*point*/) const
{
    // NOTE: Can't be easily implemented as needed info is internal to QLabel :(
    return -1;
}

void AccessibleSqueezeLabel::scrollToSubstring(int startIndex, int endIndex)
{
    setCursorPosition(endIndex);
    setCursorPosition(startIndex);
}

QString AccessibleSqueezeLabel::attributes(int offset, int* startOffset, int* endOffset) const
{
    *startOffset = offset;
    *endOffset = offset;
    return {};
}

SqueezeLabel* AccessibleSqueezeLabel::label() const
{
    return qobject_cast<SqueezeLabel*>(object());
}

#endif // QT_CONFIG(accessibility)
