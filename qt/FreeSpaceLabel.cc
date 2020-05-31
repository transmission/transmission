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

int const INTERVAL_MSEC = 15000;

} // namespace

FreeSpaceLabel::FreeSpaceLabel(QWidget* parent) :
    QLabel(parent),
    session_(nullptr),
    timer_(this)
{
    timer_.setSingleShot(true);
    timer_.setInterval(INTERVAL_MSEC);

    connect(&timer_, SIGNAL(timeout()), this, SLOT(onTimer()));
}

void FreeSpaceLabel::setSession(Session& session)
{
    if (session_ == &session)
    {
        return;
    }

    session_ = &session;
    onTimer();
}

void FreeSpaceLabel::setPath(QString const& path)
{
    if (path_ != path)
    {
        setText(tr("<i>Calculating Free Space...</i>"));
        path_ = path;
        onTimer();
    }
}

void FreeSpaceLabel::onTimer()
{
    timer_.stop();

    if (session_ == nullptr || path_.isEmpty())
    {
        return;
    }

    tr_variant args;
    tr_variantInitDict(&args, 1);
    tr_variantDictAddStr(&args, TR_KEY_path, path_.toUtf8().constData());

    auto* q = new RpcQueue();

    q->add([this, &args]()
        {
            return session_->exec("free-space", &args);
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

            timer_.start();
        });

    q->run();
}
