// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// NB: crypto-test-ref.h needs this, so use it instead of #pragma once
#ifndef TR_ENCRYPTION_H
#define TR_ENCRYPTION_H

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstddef> // size_t, std::byte
#include <memory>

#include "tr-macros.h" // tr_sha1_digest_t
#include "tr-arc4.h"

class tr_arc4;

// Spec: https://wiki.vuze.com/w/Message_Stream_Encryption
namespace tr_message_stream_encryption
{

/**
 * Holds state for the Diffie-Hellman key exchange that takes place
 * during encrypted peer handshakes
 */
class DH
{
public:
    // MSE spec: "Minimum length [for the private key] is 128 bit.
    // Anything beyond 180 bit is not believed to add any further
    // security and only increases the necessary calculation time.
    // You should use a length of 160bits whenever possible[.]
    static auto constexpr PrivateKeySize = size_t{ 20 };

    // MSE spec: "P, S [the shared secret], Ya and Yb
    // [the public keys] are 768bits long[.]"
    static auto constexpr KeySize = size_t{ 96 };

    // big-endian byte arrays holding the keys and shared secret.
    // MSE spec: "The entire handshake is in big-endian."
    using private_key_bigend_t = std::array<std::byte, PrivateKeySize>;
    using key_bigend_t = std::array<std::byte, KeySize>;

    // By default, a private key is randomly generated.
    // Providing a predefined one is useful for reproducible unit tests.
    constexpr DH(private_key_bigend_t const& private_key = randomPrivateKey()) noexcept
        : private_key_{ private_key }
    {
    }

    // Returns our own public key to be shared with a peer.
    [[nodiscard]] key_bigend_t publicKey() noexcept;

    // Compute the shared secret from our private key and the peer's public key.
    void setPeerPublicKey(key_bigend_t const& peer_public_key);

    // Returns the shared secret.
    [[nodiscard]] constexpr auto const& secret() const noexcept
    {
        return secret_;
    }

    [[nodiscard]] static private_key_bigend_t randomPrivateKey() noexcept;

private:
    private_key_bigend_t private_key_;
    key_bigend_t public_key_ = {};
    key_bigend_t secret_ = {};
};

// --- arc4 encryption for both incoming and outgoing stream
class Filter
{
public:
    void decryptInit(bool is_incoming, DH const&, tr_sha1_digest_t const& info_hash);

    template<typename T>
    constexpr void decrypt(size_t buf_len, T* buf)
    {
        if (dec_active_)
        {
            dec_key_.process(buf, buf, buf_len);
        }
    }

    void encryptInit(bool is_incoming, DH const&, tr_sha1_digest_t const& info_hash);

    template<typename T>
    constexpr void encrypt(size_t buf_len, T* buf)
    {
        if (enc_active_)
        {
            enc_key_.process(buf, buf, buf_len);
        }
    }

    [[nodiscard]] constexpr auto is_active() const noexcept
    {
        return dec_active_ || enc_active_;
    }

private:
    tr_arc4 dec_key_ = {};
    tr_arc4 enc_key_ = {};
    bool dec_active_ = false;
    bool enc_active_ = false;
};

} // namespace tr_message_stream_encryption

#endif // TR_ENCRYPTION_H
