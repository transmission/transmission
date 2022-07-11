// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// NB: crypto-test-ref.h needs this, so use it instead of #pragma once
#ifndef TR_ENCRYPTION_H
#define TR_ENCRYPTION_H

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint> // uint8_t
#include <optional>
#include <string_view>
#include <vector>

#include "crypto-utils.h"
#include "tr-macros.h"

enum
{
    KEY_LEN = 96
};

/** @brief Holds state information for encrypted peer communications */
struct tr_crypto
{
    tr_crypto(tr_sha1_digest_t const* torrent_hash = nullptr, bool is_incoming = true);
    ~tr_crypto();

    tr_crypto& operator=(tr_crypto const&) = delete;
    tr_crypto& operator=(tr_crypto&&) = delete;
    tr_crypto(tr_crypto const&) = delete;
    tr_crypto(tr_crypto&&) = delete;

    void setTorrentHash(tr_sha1_digest_t hash) noexcept
    {
        torrent_hash_ = hash;
    }

    [[nodiscard]] constexpr auto const& torrentHash() const noexcept
    {
        return torrent_hash_;
    }

    [[nodiscard]] std::string_view myPublicKey()
    {
        ensureKeyExists();
        return { reinterpret_cast<char const*>(my_public_key_), KEY_LEN };
    }

    [[nodiscard]] bool computeSecret(void const* peer_public_key, size_t len);

    [[nodiscard]] std::optional<tr_sha1_digest_t> secretKeySha1(
        void const* prepend,
        size_t prepend_len,
        void const* append,
        size_t append_len) const;

    [[nodiscard]] constexpr auto isIncoming() const noexcept
    {
        return is_incoming_;
    }

    [[nodiscard]] virtual std::vector<uint8_t> pad(size_t maxlen) const;

    void decryptInit();
    void decrypt(size_t buflen, void const* buf_in, void* buf_out);
    void encryptInit();
    void encrypt(size_t buflen, void const* buf_in, void* buf_out);

private:
    void ensureKeyExists();

    std::optional<tr_sha1_digest_t> torrent_hash_;
    struct arc4_context* dec_key_ = nullptr;
    struct arc4_context* enc_key_ = nullptr;
    tr_dh_ctx_t dh_ = {};
    tr_dh_secret_t my_secret_ = {};
    uint8_t my_public_key_[KEY_LEN] = {};
    bool const is_incoming_;
};

#endif // TR_ENCRYPTION_H
