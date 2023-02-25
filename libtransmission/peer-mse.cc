// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <memory>

#include <math/wide_integer/uintwide_t.h>

#include "transmission.h"

#include "crypto-utils.h" // tr_sha1
#include "peer-mse.h"
#include "tr-arc4.h"

using namespace std::literals;

namespace
{
namespace wi
{
using key_t = math::wide_integer::uintwide_t<
    tr_message_stream_encryption::DH::KeySize * std::numeric_limits<unsigned char>::digits>;

using private_key_t = math::wide_integer::uintwide_t<
    tr_message_stream_encryption::DH::PrivateKeySize * std::numeric_limits<unsigned char>::digits>;

template<typename UIntWide>
auto import_bits(std::array<std::byte, UIntWide::my_width2 / std::numeric_limits<uint8_t>::digits> const& bigend_bin)
{
    auto ret = UIntWide{};
    static_assert(sizeof(UIntWide) == sizeof(bigend_bin));

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

    for (auto walk = std::rbegin(ret), end = std::rend(ret); walk != end; ++walk)
    {
        *walk = std::byte(static_cast<uint8_t>(i & 0xFF));
        i >>= 8;
    }

    return ret;
}

// NOLINTBEGIN(readability-identifier-naming)
auto WIDE_INTEGER_CONSTEXPR const generator = wi::key_t{ "2" };
auto WIDE_INTEGER_CONSTEXPR const prime = wi::key_t{
    "0xFFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A63A36210000000000090563"
};
// NOLINTEND(readability-identifier-naming)

} // namespace wi
} // namespace

namespace tr_message_stream_encryption
{

// --- DH

[[nodiscard]] DH::private_key_bigend_t DH::randomPrivateKey() noexcept
{
    return tr_rand_obj<DH::private_key_bigend_t>();
}

[[nodiscard]] auto generatePublicKey(DH::private_key_bigend_t const& private_key) noexcept
{
    auto const private_key_wi = wi::import_bits<wi::private_key_t>(private_key);
    auto const public_key_wi = math::wide_integer::powm(wi::generator, private_key_wi, wi::prime);
    return wi::export_bits(public_key_wi);
}

DH::key_bigend_t DH::publicKey() noexcept
{
    if (public_key_ == key_bigend_t{})
    {
        public_key_ = generatePublicKey(private_key_);
    }

    return public_key_;
}

void DH::setPeerPublicKey(key_bigend_t const& peer_public_key)
{
    auto const secret = math::wide_integer::powm(
        wi::import_bits<wi::key_t>(peer_public_key),
        wi::import_bits<wi::private_key_t>(private_key_),
        wi::prime);
    secret_ = wi::export_bits(secret);
}

// --- Filter

void Filter::decryptInit(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash)
{
    auto const key = is_incoming ? "keyA"sv : "keyB"sv;
    auto const buf = tr_sha1::digest(key, dh.secret(), info_hash);
    dec_active_ = true;
    dec_key_.init(std::data(buf), std::size(buf));
    dec_key_.discard(1024);
}

void Filter::encryptInit(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash)
{
    auto const key = is_incoming ? "keyB"sv : "keyA"sv;
    auto const buf = tr_sha1::digest(key, dh.secret(), info_hash);
    enc_active_ = true;
    enc_key_.init(std::data(buf), std::size(buf));
    enc_key_.discard(1024);
}

} // namespace tr_message_stream_encryption
