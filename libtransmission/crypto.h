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

#include <cstddef> // size_t, std::byte
#include <optional>
#include <memory>
#include <vector>

#include "tr-macros.h"

/** @brief Holds state information for encrypted peer communications */
struct tr_crypto
{
    static auto constexpr PrivateKeySize = size_t{ 20 };
    using private_key_bigend_t = std::array<std::byte, PrivateKeySize>;

    static auto constexpr KeySize = size_t{ 96 };
    using key_bigend_t = std::array<std::byte, KeySize>;

    tr_crypto(tr_sha1_digest_t const* torrent_hash = nullptr, bool is_incoming = true);

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

    [[nodiscard]] auto publicKey()
    {
        ensureKeyExists();
        return public_key_;
    }

    void setPeerPublicKey(key_bigend_t const& peer_public_key);

    [[nodiscard]] constexpr auto secret() const noexcept
    {
        return secret_;
    }

    [[nodiscard]] constexpr auto privateKey() const noexcept
    {
        return private_key_;
    }

    [[nodiscard]] std::optional<tr_sha1_digest_t> secretKeySha1(
        void const* prepend,
        size_t prepend_len,
        void const* append,
        size_t append_len) const;

    [[nodiscard]] constexpr auto isIncoming() const noexcept
    {
        return is_incoming_;
    }

    [[nodiscard]] virtual std::vector<std::byte> pad(size_t maxlen) const;

    void decryptInit();
    void decrypt(size_t buflen, void const* buf_in, void* buf_out);
    void encryptInit();
    void encrypt(size_t buflen, void const* buf_in, void* buf_out);

private:
    void ensureKeyExists();

    std::optional<tr_sha1_digest_t> torrent_hash_;
    std::shared_ptr<struct arc4_context> dec_key_;
    std::shared_ptr<struct arc4_context> enc_key_;
    bool const is_incoming_;

    private_key_bigend_t private_key_ = {};
    key_bigend_t public_key_ = {};
    key_bigend_t secret_ = {};
};

#endif // TR_ENCRYPTION_H
