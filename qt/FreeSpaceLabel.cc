/*
 * This file Copyright (C) 2013-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QDir>

#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include "Formatter.h"
#include "FreeSpaceLabel.h"
#include "RpcQueue.h"
#include "Session.h"

namespace
{

static int const INTERVAL_MSEC = 15000;

} // namespace

FreeSpaceLabel::FreeSpaceLabel(QWidget* parent) :
    QLabel(parent),
    mySession(nullptr),
    myTimer(this)
{
    myTimer.setSingleShot(true);
    myTimer.setInterval(INTERVAL_MSEC);

    connect(&myTimer, SIGNAL(timeout()), this, SLOT(onTimer()));
}

void FreeSpaceLabel::setSession(Session& session)
{
    if (mySession == &session)
    {
        return;
    }

    mySession = &session;
    onTimer();
}

void FreeSpaceLabel::setPath(QString const& path)
{
    if (myPath != path)
    {
        setText(tr("<i>Calculating Free Space...</i>"));
        myPath = path;
        onTimer();
    }
}

void FreeSpaceLabel::onTimer()
{
    myTimer.stop();

    if (mySession == nullptr || myPath.isEmpty())
    {
        return;
    }

    tr_variant args;
    tr_variantInitDict(&args, 1);
    tr_variantDictAddStr(&args, TR_KEY_path, myPath.toUtf8().constData());

    RpcQueue* q = new RpcQueue();

    q->add([this, &args]()
        {
            return mySession->exec("free-space", &args);
        });

    q->add([this](RpcResponse const& r)
        {
            QString str;

            // update the label
            int64_t bytes = -1;

            if (tr_variantDictFindInt(r.args.get(), TR_KEY_size_bytes, &bytes) && bytes >= 0)
            {
                setText(tr("%1 free").arg(Formatter::sizeToString(bytes)));
            }
            else
            {
                setText(QString());
            }

            // update the tooltip
            size_t len = 0;
            char const* path = nullptr;
            tr_variantDictFindStr(r.args.get(), TR_KEY_path, &path, &len);
            str = QString::fromUtf8(path, len);
            setToolTip(QDir::toNativeSeparators(str));

            myTimer.start();
        });

    q->run();
}
