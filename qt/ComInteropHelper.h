/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>

class QAxObject;
class QObject;
class QString;
class QVariant;

class ComInteropHelper
{
public:
    ComInteropHelper();
    ~ComInteropHelper();

    bool isConnected() const;

    QVariant addMetainfo(QString const& metainfo);

    static void initialize();
    static void registerObject(QObject* parent);

private:
    std::unique_ptr<QAxObject> m_client;
};
