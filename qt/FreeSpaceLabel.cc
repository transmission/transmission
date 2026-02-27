// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>

#include <QDir>
#include <QLabel>
#include <QString>
#include <QWidget>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include "Formatter.h"
#include "FreeSpaceLabel.h"
#include "RpcQueue.h"
#include "Session.h"
#include "VariantHelpers.h"

using ::trqt::variant_helpers::dict_add;
using ::trqt::variant_helpers::dict_find;

namespace
{

int const IntervalMSec = 15000;

} // namespace

FreeSpaceLabel::FreeSpaceLabel(QWidget* parent)
    : QLabel{ parent }
    , timer_{ this }
{
    timer_.setSingleShot(true);
    timer_.setInterval(IntervalMSec);

    connect(&timer_, &QTimer::timeout, this, &FreeSpaceLabel::on_timer);
}

void FreeSpaceLabel::set_session(Session& session)
{
    if (session_ == &session)
    {
        return;
    }

    session_ = &session;
    on_timer();
}

void FreeSpaceLabel::set_path(QString const& path)
{
    if (path_ != path)
    {
        setText(tr("<i>Calculating Free Space…</i>"));
        path_ = path;
        on_timer();
    }
}

void FreeSpaceLabel::on_timer()
{
    timer_.stop();

    if (session_ == nullptr || path_.isEmpty())
    {
        return;
    }

    tr_variant args;
    tr_variantInitDict(&args, 1);
    dict_add(&args, TR_KEY_path, path_);

    auto* q = new RpcQueue{ this };

    q->add([this, &args]() { return session_->exec(TR_KEY_free_space, &args); });

    q->add(
        [this](RpcResponse const& r)
        {
            // update the label
            if (auto const bytes = dict_find<int64_t>(r.args.get(), TR_KEY_size_bytes); bytes && *bytes > 1)
            {
                setText(tr("%1 free").arg(Formatter::storage_to_string(*bytes)));
            }
            else
            {
                setText(QString{});
            }

            // update the tooltip
            auto const path = dict_find<QString>(r.args.get(), TR_KEY_path);
            setToolTip(QDir::toNativeSeparators(path.value_or(QString{})));

            timer_.start();
        });

    q->run();
}
