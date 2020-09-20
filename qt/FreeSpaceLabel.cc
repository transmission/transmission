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
#include "VariantHelpers.h"

using ::trqt::variant_helpers::dictAdd;
using ::trqt::variant_helpers::dictFind;

namespace
{

int const IntervalMSec = 15000;

} // namespace

FreeSpaceLabel::FreeSpaceLabel(QWidget* parent) :
    QLabel(parent),
    timer_(this)
{
    timer_.setSingleShot(true);
    timer_.setInterval(IntervalMSec);

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
    dictAdd(&args, TR_KEY_path, path_);

    auto* q = new RpcQueue();

    q->add([this, &args]()
        {
            return session_->exec("free-space", &args);
        });

    q->add([this](RpcResponse const& r)
        {
            // update the label
            auto const bytes = dictFind<int64_t>(r.args.get(), TR_KEY_size_bytes);
            if (bytes && *bytes > 1)
            {
                setText(tr("%1 free").arg(Formatter::get().sizeToString(*bytes)));
            }
            else
            {
                setText(QString());
            }

            // update the tooltip
            auto const path = dictFind<QString>(r.args.get(), TR_KEY_path);
            setToolTip(QDir::toNativeSeparators(path ? *path : QString()));

            timer_.start();
        });

    q->run();
}
