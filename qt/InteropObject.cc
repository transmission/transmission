// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "InteropObject.h"

namespace
{

// Mutable by design. publish_instance() names the process's one Instance once the
// transports exist, and only ever on the GUI thread.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
tr::interop::Instance* published_instance = nullptr;

} // namespace

void InteropObject::publish_instance(tr::interop::Instance* const instance) noexcept
{
    published_instance = instance;
}

InteropObject::InteropObject(QObject* const parent)
    : QObject{ parent }
    , instance_{ published_instance }
{
}

InteropObject::InteropObject(tr::interop::Instance& instance, QObject* const parent)
    : QObject{ parent }
    , instance_{ &instance }
{
}

bool InteropObject::PresentWindow() const
{
    return instance_ != nullptr && instance_->present_window() == tr::interop::Reply::Yes;
}

bool InteropObject::AddMetainfo(QString const& metainfo) const
{
    return instance_ != nullptr && instance_->add_metainfo(metainfo.toStdString()) == tr::interop::Reply::Yes;
}

QString InteropObject::ConfigDir() const
{
    return instance_ != nullptr ? QString::fromStdString(instance_->config_dir()) : QString{};
}
