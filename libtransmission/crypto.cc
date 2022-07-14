// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring> /* memcpy(), memmove(), memset() */
#include <string_view>

#include <arc4.h>

#include <math/wide_integer/uintwide_t.h>

#include "transmission.h"

#include "crypto-utils.h" // tr_sha1
#include "crypto.h"
#include "net.h" // includes the headers for htonl()

using namespace std::literals;

namespace wi
{
using key_t = math::wide_integer::uintwide_t<
    tr_message_stream_encryption::KeySize * std::numeric_limits<unsigned char>::digits>;

using private_key_t = math::wide_integer::uintwide_t<
    tr_message_stream_encryption::PrivateKeySize * std::numeric_limits<unsigned char>::digits>;

// source: https://stackoverflow.com/a/1001330/6568470
// nb: when we bump to std=C++20, use `std::endian`
bool is_big_endian()
{
    return htonl(47) == 47;
}

template<typename UIntWide>
auto import_bits(std::array<std::byte, UIntWide::my_width2 / std::numeric_limits<uint8_t>::digits> const& bigend_bin)
{
    auto ret = UIntWide{};

    for (auto const walk : bigend_bin)
    {
        ret <<= 8;
        ret += static_cast<uint8_t>(walk);
    }

    return ret;
}

template<typename UIntWide>
auto export_bits(UIntWide i)
{
    auto ret = std::array<std::byte, UIntWide::my_width2 / std::numeric_limits<uint8_t>::digits>{};

    if (is_big_endian())
    {
        for (auto& walk : ret)
        {
            walk = std::byte(static_cast<uint8_t>(i & 0xFF));
            i >>= 8;
        }
    }
    else
    {
        for (auto walk = std::rbegin(ret), end = std::rend(ret); walk != end; ++walk)
        {
            *walk = std::byte(static_cast<uint8_t>(i & 0xFF));
            i >>= 8;
        }
    }

    return ret;
}

auto WIDE_INTEGER_CONSTEXPR const G = wi::key_t{ "2" };
auto WIDE_INTEGER_CONSTEXPR const P = wi::key_t{
    "0xFFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A63A36210000000000090563"
};

} // namespace wi

namespace
{

void crypt_arc4(struct arc4_context* key, size_t buf_len, void const* buf_in, void* buf_out)
{
    if (key != nullptr)
    {
        arc4_process(key, buf_in, buf_out, buf_len);
        return;
    }

    if (buf_in != buf_out)
    {
        memmove(buf_out, buf_in, buf_len);
    }
}

} // namespace

///

void tr_message_stream_encryption::setPeerPublicKey(key_bigend_t const& peer_public_key)
{
    ensureKeyExists();

    auto const secret = math::wide_integer::powm(
        wi::import_bits<wi::key_t>(peer_public_key),
        wi::import_bits<wi::private_key_t>(private_key_),
        wi::P);
    secret_ = wi::export_bits(secret);
}

void tr_message_stream_encryption::ensureKeyExists()
{
    if (private_key_ == private_key_bigend_t{})
    {
        tr_rand_buffer(std::data(private_key_), std::size(private_key_));
    }

    if (public_key_ == key_bigend_t{})
    {
        auto const private_key = wi::import_bits<wi::private_key_t>(private_key_);
        auto const public_key = math::wide_integer::powm(wi::G, private_key, wi::P);
        public_key_ = wi::export_bits(public_key);
    }
}
void tr_message_stream_encryption::decryptInit(tr_sha1_digest_t const& info_hash)
{
    auto const key = isIncoming() ? "keyA"sv : "keyB"sv;

    dec_key_ = std::make_shared<struct arc4_context>();
    auto const buf = tr_sha1(key, secret(), info_hash);
    arc4_init(dec_key_.get(), std::data(*buf), std::size(*buf));
    arc4_discard(dec_key_.get(), 1024);
}

void tr_message_stream_encryption::decrypt(size_t buf_len, void const* buf_in, void* buf_out)
{
    crypt_arc4(dec_key_.get(), buf_len, buf_in, buf_out);
}

void tr_message_stream_encryption::encryptInit(tr_sha1_digest_t const& info_hash)
{
    auto const key = isIncoming() ? "keyB"sv : "keyA"sv;

    enc_key_ = std::make_shared<struct arc4_context>();
    auto const buf = tr_sha1(key, secret(), info_hash);
    arc4_init(enc_key_.get(), std::data(*buf), std::size(*buf));
    arc4_discard(enc_key_.get(), 1024);
}

void tr_message_stream_encryption::encrypt(size_t buf_len, void const* buf_in, void* buf_out)
{
    crypt_arc4(enc_key_.get(), buf_len, buf_in, buf_out);
}

std::vector<std::byte> tr_message_stream_encryption::pad(size_t maxlen) const
{
    auto const len = tr_rand_int(maxlen);
    auto ret = std::vector<std::byte>{};
    ret.resize(len);
    tr_rand_buffer(std::data(ret), len);
    return ret;
}
