// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <memory>

// NOMERGE
#include <iostream>

#include <arc4.h>

#include <math/wide_integer/uintwide_t.h>

#include "transmission.h"

#include "crypto-utils.h" // tr_sha1
#include "net.h" // includes the headers for htonl()
#include "peer-mse.h"

using namespace std::literals;

// source: https://stackoverflow.com/a/1001330/6568470
// nb: when we bump to std=C++20, use `std::endian`
static bool is_big_endian()
{
    return htonl(47) == 47;
}

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

    std::cerr << __FILE__ << ':' << __LINE__ << " import_bits, array in: "sv;
    for (auto u8 : bigend_bin)
    {
        std::cerr << static_cast<unsigned>(u8) << ' ';
    }
    std::cerr << std::endl;

    if (is_big_endian())
    {
        std::cerr << __FILE__ << ':' << __LINE__ << " is big endian" << std::endl;

        ret = *reinterpret_cast<UIntWide const*>(std::data(bigend_bin));
        std::cerr << __FILE__ << ':' << __LINE__ << " attempt 1: " << ret << std::endl;

        ret = {};
        for (auto walk = std::rbegin(bigend_bin), end = std::rend(bigend_bin); walk != end; ++walk)
        {
            ret <<= 8;
            ret += static_cast<uint8_t>(*walk);
        }
        std::cerr << __FILE__ << ':' << __LINE__ << " attempt 2: " << ret << std::endl;

        ret = {};
        for (auto walk = std::begin(bigend_bin), end = std::end(bigend_bin); walk != end; ++walk)
        {
            ret <<= 8;
            ret += static_cast<uint8_t>(*walk);
        }
        std::cerr << __FILE__ << ':' << __LINE__ << " attempt 3: " << ret << std::endl;
    }
    else
    {
        std::cerr << __FILE__ << ':' << __LINE__ << " is little endian" << std::endl;
        for (auto const walk : bigend_bin)
        {
            ret <<= 8;
            ret += static_cast<uint8_t>(walk);
        }
    }

    std::cerr << __FILE__ << ':' << __LINE__ << " import_bits, number out: "sv << ret << std::endl;
    return ret;
}

template<typename UIntWide>
auto export_bits(UIntWide i)
{
    std::cerr << __FILE__ << ':' << __LINE__ << " export_bits, num in: "sv << i << std::endl;

    using array_t = std::array<std::byte, UIntWide::my_width2 / std::numeric_limits<uint8_t>::digits>;
    auto ret = array_t{};

    if (is_big_endian())
    {
        std::cerr << __FILE__ << ':' << __LINE__ << " is big endian" << std::endl;

        ret = *reinterpret_cast<array_t const*>(&i);
        std::cerr << __FILE__ << ':' << __LINE__ << " attempt 1: ";
        for (auto u8 : ret)
        {
            std::cerr << static_cast<unsigned>(u8) << ' ';
        }
        std::cerr << std::endl;

        ret = {};
        for (auto walk = std::rbegin(ret), end = std::rend(ret); walk != end; ++walk)
        {
            *walk = std::byte(static_cast<uint8_t>(i & 0xFF));
            i >>= 8;
        }
        std::cerr << __FILE__ << ':' << __LINE__ << " attempt 2: ";
        for (auto u8 : ret)
        {
            std::cerr << static_cast<unsigned>(u8) << ' ';
        }
        std::cerr << std::endl;

        ret = {};
        for (auto& walk : ret)
        {
            walk = std::byte(static_cast<uint8_t>(i & 0xFF));
            i >>= 8;
        }
        std::cerr << __FILE__ << ':' << __LINE__ << " attempt 3: ";
        for (auto u8 : ret)
        {
            std::cerr << static_cast<unsigned>(u8) << ' ';
        }
        std::cerr << std::endl;
    }
    else
    {
        std::cerr << __FILE__ << ':' << __LINE__ << " is little endian" << std::endl;
        for (auto walk = std::rbegin(ret), end = std::rend(ret); walk != end; ++walk)
        {
            *walk = std::byte(static_cast<uint8_t>(i & 0xFF));
            i >>= 8;
        }
    }

    std::cerr << __FILE__ << ':' << __LINE__ << " export_bits, array out: "sv;
    for (auto u8 : ret)
    {
        std::cerr << static_cast<unsigned>(u8) << ' ';
    }
    std::cerr << std::endl;

    return ret;
}

auto WIDE_INTEGER_CONSTEXPR const G = wi::key_t{ "2" };
auto WIDE_INTEGER_CONSTEXPR const P = wi::key_t{
    "0xFFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A63A36210000000000090563"
};

} // namespace wi

namespace tr_message_stream_encryption
{

/// DH

[[nodiscard]] DH::private_key_bigend_t DH::randomPrivateKey() noexcept
{
    auto key = DH::private_key_bigend_t{};
    tr_rand_buffer(std::data(key), std::size(key));
    return key;
}

[[nodiscard]] auto generatePublicKey(DH::private_key_bigend_t const& private_key) noexcept
{
    auto const private_key_wi = wi::import_bits<wi::private_key_t>(private_key);
    auto const public_key_wi = math::wide_integer::powm(wi::G, private_key_wi, wi::P);
    return wi::export_bits(public_key_wi);
}

DH::DH(private_key_bigend_t const& private_key) noexcept
    : private_key_{ private_key }
{
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
        wi::P);
    secret_ = wi::export_bits(secret);
}

/// Filter

void Filter::decryptInit(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash)
{
    auto const key = is_incoming ? "keyA"sv : "keyB"sv;

    dec_key_ = std::make_shared<struct arc4_context>();
    auto const buf = tr_sha1(key, dh.secret(), info_hash);
    arc4_init(dec_key_.get(), std::data(*buf), std::size(*buf));
    arc4_discard(dec_key_.get(), 1024);
}

void Filter::decrypt(size_t buf_len, void* buf)
{
    if (dec_key_)
    {
        arc4_process(dec_key_.get(), buf, buf, buf_len);
    }
}

void Filter::encryptInit(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash)
{
    auto const key = is_incoming ? "keyB"sv : "keyA"sv;

    enc_key_ = std::make_shared<struct arc4_context>();
    auto const buf = tr_sha1(key, dh.secret(), info_hash);
    arc4_init(enc_key_.get(), std::data(*buf), std::size(*buf));
    arc4_discard(enc_key_.get(), 1024);
}

void Filter::encrypt(size_t buf_len, void* buf)
{
    if (enc_key_)
    {
        arc4_process(enc_key_.get(), buf, buf, buf_len);
    }
}

} // namespace tr_message_stream_encryption
