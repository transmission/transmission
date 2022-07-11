// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring> /* memcpy(), memmove(), memset() */

#include <arc4.h>

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
    tr_dh_secret_free(my_secret_);
    tr_dh_free(dh_);
    tr_free(enc_key_);
    tr_free(dec_key_);
}

///

namespace
{

auto constexpr PrimeLen = size_t{ 96 };
auto constexpr DhPrivkeyLen = size_t{ 20 };

uint8_t constexpr dh_P[PrimeLen] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2, //
    0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1, //
    0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74, 0x02, 0x0B, 0xBE, 0xA6, //
    0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD, //
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D, //
    0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45, //
    0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9, //
    0xA6, 0x3A, 0x36, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x05, 0x63, //
};

uint8_t constexpr dh_G[] = { 2 };

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

bool tr_crypto::computeSecret(void const* peer_public_key, size_t len)
{
    ensureKeyExists();
    my_secret_ = tr_dh_agree(dh_, static_cast<uint8_t const*>(peer_public_key), len);
    return my_secret_ != nullptr;
}

void tr_crypto::ensureKeyExists()
{
    if (dh_ == nullptr)
    {
        size_t public_key_length = KEY_LEN;
        dh_ = tr_dh_new(dh_P, sizeof(dh_P), dh_G, sizeof(dh_G));
        tr_dh_make_key(dh_, DhPrivkeyLen, my_public_key_, &public_key_length);

        TR_ASSERT(public_key_length == KEY_LEN);
    }
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
    return tr_dh_secret_derive(my_secret_, prepend, prepend_len, append, append_len);
}

std::vector<uint8_t> tr_crypto::pad(size_t maxlen) const
{
    auto const len = tr_rand_int(maxlen);
    auto ret = std::vector<uint8_t>{};
    ret.resize(len);
    tr_rand_buffer(std::data(ret), len);
    return ret;
}
