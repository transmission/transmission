// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring> /* memcpy(), memmove(), memset() */

#include <arc4.h>

#include <math/wide_integer/uintwide_t.h>

#include <arpa/inet.h> // htonl

#include "transmission.h"
#include "crypto.h"
#include "crypto-utils.h"
#include "tr-assert.h"

namespace wi
{
using key_t = math::wide_integer::uintwide_t<tr_crypto::KeySize * std::numeric_limits<unsigned char>::digits>;

using private_key_t = math::wide_integer::uintwide_t<tr_crypto::PrivateKeySize * std::numeric_limits<unsigned char>::digits>;

template<typename Integral>
auto import_bits(std::array<std::byte, Integral::my_width2 / std::numeric_limits<uint8_t>::digits> const& bigend_bin)
{
    auto ret = Integral{};

    for (auto const walk : bigend_bin)
    {
        ret <<= 8;
        ret += static_cast<uint8_t>(walk);
    }

    return ret;
}

template<typename Integral>
auto export_bits(Integral i)
{
    auto const is_big_endian = htonl(47) == 47;
    auto ret = std::array<std::byte, Integral::my_width2 / std::numeric_limits<uint8_t>::digits>{};

    if (is_big_endian)
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

void init_rc4(
    tr_crypto const* crypto,
    std::shared_ptr<struct arc4_context>& setme,
    std::string_view key,
    tr_sha1_digest_t const& info_hash)
{
    if (!setme)
    {
        setme = std::make_shared<struct arc4_context>();
    }

    if (auto const buf = tr_sha1(key, crypto->secret(), info_hash); buf)
    {
        arc4_init(setme.get(), std::data(*buf), std::size(*buf));
        arc4_discard(setme.get(), 1024);
    }
}

void crypt_rc4(struct arc4_context* key, size_t buf_len, void const* buf_in, void* buf_out)
{
    if (key == nullptr)
    {
        if (buf_in != buf_out)
        {
            memmove(buf_out, buf_in, buf_len);
        }

        return;
    }

    arc4_process(key, buf_in, buf_out, buf_len);
}

} // namespace

///

void tr_crypto::setPeerPublicKey(key_bigend_t const& peer_public_key)
{
    ensureKeyExists();

    auto const secret = math::wide_integer::powm(
        wi::import_bits<wi::key_t>(peer_public_key),
        wi::import_bits<wi::private_key_t>(private_key_),
        wi::P);
    secret_ = wi::export_bits(secret);
}

void tr_crypto::ensureKeyExists()
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
void tr_crypto::decryptInit(tr_sha1_digest_t const& info_hash)
{
    init_rc4(this, dec_key_, is_incoming_ ? "keyA" : "keyB", info_hash); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_crypto::decrypt(size_t buf_len, void const* buf_in, void* buf_out)
{
    crypt_rc4(dec_key_.get(), buf_len, buf_in, buf_out); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_crypto::encryptInit(tr_sha1_digest_t const& info_hash)
{
    init_rc4(this, enc_key_, is_incoming_ ? "keyB" : "keyA", info_hash); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_crypto::encrypt(size_t buf_len, void const* buf_in, void* buf_out)
{
    crypt_rc4(enc_key_.get(), buf_len, buf_in, buf_out); // lgtm[cpp/weak-cryptographic-algorithm]
}

std::vector<std::byte> tr_crypto::pad(size_t maxlen) const
{
    auto const len = tr_rand_int(maxlen);
    auto ret = std::vector<std::byte>{};
    ret.resize(len);
    tr_rand_buffer(std::data(ret), len);
    return ret;
}
