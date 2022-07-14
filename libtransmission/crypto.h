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
#include <functional>
#include <memory>
#include <vector>

#include "tr-macros.h" // tr_sha1_digest_t
#include "tr-assert.h"

/**
 * Holds state information for message stream encryption.
 * Spec: https://wiki.vuze.com/w/Message_Stream_Encryption
 */
namespace tr_message_stream_encryption
{

class DH
{
public:
    // MSE spec: "Minimum length [for the private key] is 128 bit.
    // Anything beyond 180 bit is not believed to add any further
    // security and only increases the necessary calculation time.
    // You should use a length of 160bits whenever possible[.]
    static auto constexpr PrivateKeySize = size_t{ 20 };

    // MSE spec: "P, S [the shared secret], Ya and Yb
    // [the public keys] are 768bits long"
    static auto constexpr KeySize = size_t{ 96 };

    // big-endian byte arrays holding the keys and shared secret.
    // MSE spec: "The entire handshake is in big-endian."
    using private_key_bigend_t = std::array<std::byte, PrivateKeySize>;
    using key_bigend_t = std::array<std::byte, KeySize>;

    // Returns our own public key to be shared with a peer.
    // If one doesn't exist, it is created.
    [[nodiscard]] auto publicKey()
    {
        ensureKeyExists();
        return public_key_;
    }

    // Computes the shared secret from our own privateKey()
    // and the peer's publicKey().
    void setPeerPublicKey(key_bigend_t const& peer_public_key);

    // Returns the shared secret.
    [[nodiscard]] auto secret() const noexcept
    {
        TR_ASSERT(secret_ != key_bigend_t{});
        return secret_;
    }

    [[nodiscard]] auto privateKey() noexcept
    {
        ensureKeyExists();
        return private_key_;
    }

    // Unused in production. Exists to help make tests reproducible.
    // Note that the public key is derived from the private key, so
    // tests must call this *before* ensureKeyExists() is called.
    void setPrivateKey(private_key_bigend_t const& key)
    {
        private_key_ = key;
    }

private:
    void ensureKeyExists();

    private_key_bigend_t private_key_ = {};
    key_bigend_t public_key_ = {};
    key_bigend_t secret_ = {};
};

/// arc4 encryption for both incoming and outgoing stream
class Filter
{
public:
    void decryptInit(bool is_incoming, DH const&, tr_sha1_digest_t const& info_hash);
    void decrypt(size_t buflen, void* buf);
    void encryptInit(bool is_incoming, DH const&, tr_sha1_digest_t const& info_hash);
    void encrypt(size_t buflen, void* buf);

private:
    std::shared_ptr<struct arc4_context> dec_key_;
    std::shared_ptr<struct arc4_context> enc_key_;
};

} // namespace tr_message_stream_encryption

#endif // TR_ENCRYPTION_H
