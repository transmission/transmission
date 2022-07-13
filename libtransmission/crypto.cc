// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring> /* memcpy(), memmove(), memset() */

#include <arc4.h>

#include <iostream>

#include <math/wide_integer/uintwide_t.h>

#include <arpa/inet.h> // htonl

#include "transmission.h"
#include "crypto.h"
#include "crypto-utils.h"
#include "tr-assert.h"
#include "utils.h"

///

tr_crypto::tr_crypto(tr_sha1_digest_t const* torrent_hash, bool is_incoming)
    : is_incoming_{ is_incoming }
{
    if (torrent_hash != nullptr)
    {
        this->torrent_hash_ = *torrent_hash;
    }
}

tr_crypto::~tr_crypto()
{
    tr_free(enc_key_);
    tr_free(dec_key_);
}

///

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

void init_rc4(tr_crypto const* crypto, struct arc4_context** setme, std::string_view key)
{
    TR_ASSERT(crypto->torrentHash());

    if (*setme == nullptr)
    {
        *setme = tr_new0(struct arc4_context, 1);
    }

    auto const hash = crypto->torrentHash();
    auto const buf = hash ? crypto->secretKeySha1(std::data(key), std::size(key), std::data(*hash), std::size(*hash)) :
                            std::nullopt;
    if (buf)
    {
        arc4_init(*setme, std::data(*buf), std::size(*buf));
        arc4_discard(*setme, 1024);
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
        std::cerr << __FILE__ << ':' << __LINE__ << " private_key " << private_key << std::endl;
        // yay this is correct ^

        // use wide-integer to calculate the public key
        std::cerr << __FILE__ << ':' << __LINE__ << " P " << wi::P << std::endl;
        std::cerr << __FILE__ << ':' << __LINE__ << " G " << wi::G << std::endl;
        auto const public_key = math::wide_integer::powm(wi::G, private_key, wi::P);
        std::cerr << __FILE__ << ':' << __LINE__ << " public_key " << public_key << std::endl;
        // yay this is correct ^

        public_key_ = wi::export_bits(public_key);
        // TR_ASSERT(openssl_public_key_ == public_key_);
#if 0
        std::cerr << __FILE__ << ':' << __LINE__ << " my_public_key_ from openssl ";
        for (auto* walk = my_public_key_, *end = walk + 96; walk != end; ++walk)
        {
            std::cerr << static_cast<unsigned>(*walk) << ' ';
        }
        std::cerr << std::endl;

        auto tmp = public_key;
        for (auto walk = std::rbegin(public_key_), end = std::rend(public_key_); walk != end; ++walk)
        {
            *walk = std::byte(static_cast<uint8_t>(tmp & 0xFF));
            tmp >>= 8;
        }

        std::cerr << __FILE__ << ':' << __LINE__ << ' ';
        for (auto walk : public_key_)
        {
            std::cerr << static_cast<unsigned>(walk) << ' ';
        }
        std::cerr << std::endl;
#endif

        // auto const public_key_v = std::vector<std::byte>(std::begin(public_key_), std::end(public_key_));
        // TR_ASSERT(publicKey() == public_key_v);
        // std::cerr << __FILE__ << ':' << __LINE__ << " yep this is ok" << std::endl;
    }

#if 0
    auto const
        static auto constexpr PrivateKeySize = size_t{ 20 };
    using private_key_t = math::wide_integer::uintwide_t<PrivateKeySize * std::numeric_limits<unsigned char>::digits>;
    using private_key_bytes_t = std::array<char, PrivateKeySize>;

    static auto constexpr PublicKeySize = size_t{ 96 };
    using public_key_t = math::wide_integer::uintwide_t<PublicKeySize * std::numeric_limits<unsigned char>::digits>;
    using public_key_bytes_t = std::array<char, PublicKeySize>;

    private_key_bytes_t private_key_;
    public_key_bytes_t public_key_;
    public_key_bytes_t secret_;
#endif
}
void tr_crypto::decryptInit()
{
    init_rc4(this, &dec_key_, is_incoming_ ? "keyA" : "keyB"); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_crypto::decrypt(size_t buf_len, void const* buf_in, void* buf_out)
{
    crypt_rc4(dec_key_, buf_len, buf_in, buf_out); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_crypto::encryptInit()
{
    init_rc4(this, &enc_key_, is_incoming_ ? "keyB" : "keyA"); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_crypto::encrypt(size_t buf_len, void const* buf_in, void* buf_out)
{
    crypt_rc4(enc_key_, buf_len, buf_in, buf_out); // lgtm[cpp/weak-cryptographic-algorithm]
}

std::optional<tr_sha1_digest_t> tr_crypto::secretKeySha1(
    void const* prepend,
    size_t prepend_len,
    void const* append,
    size_t append_len) const
{
    return tr_sha1(
        std::string_view{ static_cast<char const*>(prepend), prepend_len },
        secret_,
        std::string_view{ static_cast<char const*>(append), append_len });
}

std::vector<std::byte> tr_crypto::pad(size_t maxlen) const
{
    auto const len = tr_rand_int(maxlen);
    auto ret = std::vector<std::byte>{};
    ret.resize(len);
    tr_rand_buffer(std::data(ret), len);
    return ret;
}
