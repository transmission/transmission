/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <string_view>
#include <optional>

#include <QString>
#include <QVector>

#include <libtransmission/variant.h>

class QByteArray;

class Speed;
struct Peer;
struct TorrentFile;
struct TrackerStat;

namespace trqt
{

namespace variant_helpers
{

template<typename T,
    typename std::enable_if<std::is_same_v<T, bool>>::type* = nullptr>
auto getValue(tr_variant const* variant)
{
    std::optional<T> ret;
    auto value = T {};
    if (tr_variantGetBool(variant, &value))
    {
        ret = value;
    }

    return ret;
}

template<typename T,
    typename std::enable_if<std::is_same_v<T, int64_t>||
    std::is_same_v<T, uint64_t>||
    std::is_same_v<T, int>||
    std::is_same_v<T, time_t>>::type* = nullptr>
auto getValue(tr_variant const* variant)
{
    std::optional<T> ret;
    auto value = int64_t {};
    if (tr_variantGetInt(variant, &value))
    {
        ret = value;
    }

    return ret;
}

template<typename T,
    typename std::enable_if<std::is_same_v<T, double>>::type* = nullptr>
auto getValue(tr_variant const* variant)
{
    std::optional<T> ret;
    auto value = T {};
    if (tr_variantGetReal(variant, &value))
    {
        ret = value;
    }

    return ret;
}

template<typename T, typename std::enable_if<std::is_same_v<T, QString>>::type* = nullptr>
auto getValue(tr_variant const* variant)
{
    std::optional<T> ret;
    char const* str;
    size_t len;
    if (tr_variantGetStr(variant, &str, &len))
    {
        ret = QString::fromUtf8(str, len);
    }

    return ret;
}

template<typename T>
bool change(T& setme, T const& value)
{
    bool const changed = setme != value;

    if (changed)
    {
        setme = value;
    }

    return changed;
}

bool change(double& setme, double const& value);
bool change(Speed& setme, tr_variant const* value);
bool change(Peer& setme, tr_variant const* value);
bool change(TorrentFile& setme, tr_variant const* value);
bool change(TrackerStat& setme, tr_variant const* value);

template<typename T>
bool change(T& setme, tr_variant const* variant)
{
    auto const value = getValue<T>(variant);
    return value && change(setme, *value);
}

template<typename T>
bool change(QVector<T>& setme, tr_variant const* value)
{
    bool changed = false;

    int const n = tr_variantListSize(value);
    if (setme.size() != n)
    {
        setme.resize(n);
        changed = true;
    }

    for (int i = 0; i < n; ++i)
    {
        changed = change(setme[i], tr_variantListChild(const_cast<tr_variant*>(value), i)) || changed;
    }

    return changed;
}

///

template<typename T>
auto dictFind(tr_variant* dict, tr_quark key)
{
    std::optional<T> ret;
    auto const* child = tr_variantDictFind(dict, key);
    if (child != nullptr)
    {
        ret = getValue<T>(child);
    }

    return ret;
}

///

void variantInit(tr_variant* init_me, bool value);
void variantInit(tr_variant* init_me, int64_t value);
void variantInit(tr_variant* init_me, int value);
void variantInit(tr_variant* init_me, unsigned int value);
void variantInit(tr_variant* init_me, double value);
void variantInit(tr_variant* init_me, QByteArray const& value);
void variantInit(tr_variant* init_me, QString const& value);
void variantInit(tr_variant* init_me, std::string_view value);
void variantInit(tr_variant* init_me, char const* value) = delete; // use string_view

template<typename C, typename T = typename C::value_type>
void variantInit(tr_variant* init_me, C const& value)
{
    tr_variantInitList(init_me, std::size(value));
    for (auto const& item : value)
    {
        variantInit(tr_variantListAdd(init_me), item);
    }
}

template<typename T>
void listAdd(tr_variant* list, T const& value)
{
    variantInit(tr_variantListAdd(list), value);
}

template<typename T>
void dictAdd(tr_variant* dict, tr_quark key, T const& value)
{
    variantInit(tr_variantDictAdd(dict, key), value);
}

} // namespace variant_helpers

} // trqt
