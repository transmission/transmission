// This file Copyright (C) 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifndef TR_CRYPTO_TEST_REF_H
#define TR_CRYPTO_TEST_REF_H

/* #define CRYPTO_REFERENCE_CHECK */

#ifdef CRYPTO_REFERENCE_CHECK

#define tr_base64_decode tr_base64_decode_
#define tr_base64_decode_impl tr_base64_decode_impl_
#define tr_base64_encode tr_base64_encode_
#define tr_base64_encode_impl tr_base64_encode_impl_
#define tr_rand_buffer tr_rand_buffer_
#define tr_rand_buffer_crypto tr_rand_buffer_crypto_
#define tr_rand_buffer_std tr_rand_buffer_std_
#define tr_rand_int tr_rand_int_
#define tr_rand_obj tr_rand_obj_
#define tr_salt_shaker tr_salt_shaker_
#define tr_sha1 tr_sha1_
#define tr_sha1_from_string tr_sha1_from_string_
#define tr_sha1_to_string tr_sha1_to_string_
#define tr_sha256 tr_sha256_
#define tr_sha256_from_string tr_sha256_from_string_
#define tr_sha256_to_string tr_sha256_to_string_
#define tr_ssha1 tr_ssha1_
#define tr_ssha1_matches tr_ssha1_matches_
#define tr_ssha1_test tr_ssha1_test_
#define tr_ssl_ctx_t tr_ssl_ctx_t_
#define tr_ssl_get_x509_store tr_ssl_get_x509_store_
#define tr_urbg tr_urbg_
#define tr_x509_cert_free tr_x509_cert_free_
#define tr_x509_cert_new tr_x509_cert_new_
#define tr_x509_cert_t tr_x509_cert_t_
#define tr_x509_store_add tr_x509_store_add_
#define tr_x509_store_t tr_x509_store_t_

#undef TR_ENCRYPTION_H
#undef TR_CRYPTO_UTILS_H

#include <libtransmission/crypto-utils.h>
#include <libtransmission/crypto-utils.cc>
#include <libtransmission/crypto-utils-openssl.cc>

#undef tr_base64_decode
#undef tr_base64_decode_impl
#undef tr_base64_encode
#undef tr_base64_encode_impl
#undef tr_rand_buffer
#undef tr_rand_buffer_crypto
#undef tr_rand_buffer_std
#undef tr_rand_int
#undef tr_rand_obj
#undef tr_salt_shaker
#undef tr_sha1
#undef tr_sha1_from_string
#undef tr_sha1_to_string
#undef tr_sha256
#undef tr_sha256_from_string
#undef tr_sha256_to_string
#undef tr_ssha1
#undef tr_ssha1_matches
#undef tr_ssha1_test
#undef tr_ssl_ctx_t
#undef tr_ssl_get_x509_store
#undef tr_urbg
#undef tr_x509_cert_free
#undef tr_x509_cert_new
#undef tr_x509_cert_t
#undef tr_x509_store_add
#undef tr_x509_store_t

#else /* CRYPTO_REFERENCE_CHECK */

#define tr_base64_decode_ tr_base64_decode
#define tr_base64_decode_impl_ tr_base64_decode_impl
#define tr_base64_encode_ tr_base64_encode
#define tr_base64_encode_impl_ tr_base64_encode_impl
#define tr_rand_buffer_ tr_rand_buffer
#define tr_rand_buffer_crypto_ tr_rand_buffer_crypto
#define tr_rand_buffer_std_ tr_rand_buffer_std
#define tr_rand_int_ tr_rand_int
#define tr_rand_obj_ tr_rand_obj
#define tr_salt_shaker_ tr_salt_shaker
#define tr_sha1_ tr_sha1
#define tr_sha1_ctx_t_ tr_sha1_ctx_t
#define tr_sha1_final_ tr_sha1_final
#define tr_sha1_from_string_ tr_sha1_from_string
#define tr_sha1_init_ tr_sha1_init
#define tr_sha1_to_string_ tr_sha1_to_string
#define tr_sha1_update_ tr_sha1_update
#define tr_sha256_ tr_sha256
#define tr_sha256_ctx_t_ tr_sha256_ctx_t
#define tr_sha256_final_ tr_sha256_final
#define tr_sha256_from_string_ tr_sha256_from_string
#define tr_sha256_init_ tr_sha256_init
#define tr_sha256_to_string_ tr_sha256_to_string
#define tr_sha256_update_ tr_sha256_update
#define tr_ssha1_ tr_ssha1
#define tr_ssha1_matches_ tr_ssha1_matches
#define tr_ssha1_test_ tr_ssha1_test
#define tr_ssl_ctx_t_ tr_ssl_ctx_t
#define tr_ssl_get_x509_store_ tr_ssl_get_x509_store
#define tr_urbg_ tr_urbg
#define tr_x509_cert_free_ tr_x509_cert_free
#define tr_x509_cert_new_ tr_x509_cert_new
#define tr_x509_cert_t_ tr_x509_cert_t
#define tr_x509_store_add_ tr_x509_store_add
#define tr_x509_store_t_ tr_x509_store_t

#endif /* CRYPTO_REFERENCE_CHECK */

#endif /* TR_CRYPTO_TEST_REF_H */
