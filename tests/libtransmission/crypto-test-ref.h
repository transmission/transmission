/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#ifndef TR_CRYPTO_TEST_REF_H
#define TR_CRYPTO_TEST_REF_H

/* #define CRYPTO_REFERENCE_CHECK */

#ifdef CRYPTO_REFERENCE_CHECK

#define KEY_LEN KEY_LEN_

#define tr_sha1_ctx_t tr_sha1_ctx_t_
#define tr_rc4_ctx_t tr_rc4_ctx_t_
#define tr_dh_ctx_t tr_dh_ctx_t_
#define tr_dh_secret_t tr_dh_secret_t_
#define tr_ssl_ctx_t tr_ssl_ctx_t_
#define tr_x509_store_t tr_x509_store_t_
#define tr_x509_cert_t tr_x509_cert_t_
#define tr_crypto tr_crypto_
#define tr_cryptoConstruct tr_cryptoConstruct_
#define tr_cryptoDestruct tr_cryptoDestruct_
#define tr_cryptoSetTorrentHash tr_cryptoSetTorrentHash_
#define tr_cryptoGetTorrentHash tr_cryptoGetTorrentHash_
#define tr_cryptoHasTorrentHash tr_cryptoHasTorrentHash_
#define tr_cryptoComputeSecret tr_cryptoComputeSecret_
#define tr_cryptoGetMyPublicKey tr_cryptoGetMyPublicKey_
#define tr_cryptoDecryptInit tr_cryptoDecryptInit_
#define tr_cryptoDecrypt tr_cryptoDecrypt_
#define tr_cryptoEncryptInit tr_cryptoEncryptInit_
#define tr_cryptoEncrypt tr_cryptoEncrypt_
#define tr_cryptoSecretKeySha1 tr_cryptoSecretKeySha1_
#define tr_sha1 tr_sha1_
#define tr_sha1_init tr_sha1_init_
#define tr_sha1_update tr_sha1_update_
#define tr_sha1_final tr_sha1_final_
#define tr_rc4_new tr_rc4_new_
#define tr_rc4_free tr_rc4_free_
#define tr_rc4_set_key tr_rc4_set_key_
#define tr_rc4_process tr_rc4_process_
#define tr_dh_new tr_dh_new_
#define tr_dh_free tr_dh_free_
#define tr_dh_make_key tr_dh_make_key_
#define tr_dh_agree tr_dh_agree_
#define tr_dh_secret_derive tr_dh_secret_derive_
#define tr_dh_secret_free tr_dh_secret_free_
#define tr_dh_align_key tr_dh_align_key_
#define tr_ssl_get_x509_store tr_ssl_get_x509_store_
#define tr_x509_store_add tr_x509_store_add_
#define tr_x509_cert_new tr_x509_cert_new_
#define tr_x509_cert_free tr_x509_cert_free_
#define tr_rand_int tr_rand_int_
#define tr_rand_int_weak tr_rand_int_weak_
#define tr_rand_buffer tr_rand_buffer_
#define tr_ssha1 tr_ssha1_
#define tr_ssha1_matches tr_ssha1_matches_
#define tr_base64_encode tr_base64_encode_
#define tr_base64_encode_str tr_base64_encode_str_
#define tr_base64_encode_impl tr_base64_encode_impl_
#define tr_base64_decode tr_base64_decode_
#define tr_base64_decode_str tr_base64_decode_str_
#define tr_base64_decode_impl tr_base64_decode_impl_
#define tr_sha1_to_hex tr_sha1_to_hex_
#define tr_hex_to_sha1 tr_hex_to_sha1_

#undef TR_ENCRYPTION_H
#undef TR_CRYPTO_UTILS_H

#include "crypto.h"
#include "crypto-utils.h"
#include "crypto.c"
#include "crypto-utils.c"
#include "crypto-utils-openssl.c"

#undef KEY_LEN_

#undef tr_sha1_ctx_t
#undef tr_rc4_ctx_t
#undef tr_dh_ctx_t
#undef tr_dh_secret_t
#undef tr_ssl_ctx_t
#undef tr_x509_store_t
#undef tr_x509_cert_t
#undef tr_crypto
#undef tr_cryptoConstruct
#undef tr_cryptoDestruct
#undef tr_cryptoSetTorrentHash
#undef tr_cryptoGetTorrentHash
#undef tr_cryptoHasTorrentHash
#undef tr_cryptoComputeSecret
#undef tr_cryptoGetMyPublicKey
#undef tr_cryptoDecryptInit
#undef tr_cryptoDecrypt
#undef tr_cryptoEncryptInit
#undef tr_cryptoEncrypt
#undef tr_cryptoSecretKeySha1
#undef tr_sha1
#undef tr_sha1_init
#undef tr_sha1_update
#undef tr_sha1_final
#undef tr_rc4_new
#undef tr_rc4_free
#undef tr_rc4_set_key
#undef tr_rc4_process
#undef tr_dh_new
#undef tr_dh_free
#undef tr_dh_make_key
#undef tr_dh_agree
#undef tr_dh_secret_derive
#undef tr_dh_secret_free
#undef tr_dh_align_key
#undef tr_ssl_get_x509_store
#undef tr_x509_store_add
#undef tr_x509_cert_new
#undef tr_x509_cert_free
#undef tr_rand_int
#undef tr_rand_int_weak
#undef tr_rand_buffer
#undef tr_ssha1
#undef tr_ssha1_matches
#undef tr_base64_encode
#undef tr_base64_encode_str
#undef tr_base64_encode_impl
#undef tr_base64_decode
#undef tr_base64_decode_str
#undef tr_base64_decode_impl
#undef tr_sha1_to_hex
#undef tr_hex_to_sha1

#else /* CRYPTO_REFERENCE_CHECK */

#define KEY_LEN_ KEY_LEN

#define tr_sha1_ctx_t_ tr_sha1_ctx_t
#define tr_rc4_ctx_t_ tr_rc4_ctx_t
#define tr_dh_ctx_t_ tr_dh_ctx_t
#define tr_dh_secret_t_ tr_dh_secret_t
#define tr_ssl_ctx_t_ tr_ssl_ctx_t
#define tr_x509_store_t_ tr_x509_store_t
#define tr_x509_cert_t_ tr_x509_cert_t
#define tr_crypto_ tr_crypto
#define tr_cryptoConstruct_ tr_cryptoConstruct
#define tr_cryptoDestruct_ tr_cryptoDestruct
#define tr_cryptoSetTorrentHash_ tr_cryptoSetTorrentHash
#define tr_cryptoGetTorrentHash_ tr_cryptoGetTorrentHash
#define tr_cryptoHasTorrentHash_ tr_cryptoHasTorrentHash
#define tr_cryptoComputeSecret_ tr_cryptoComputeSecret
#define tr_cryptoGetMyPublicKey_ tr_cryptoGetMyPublicKey
#define tr_cryptoDecryptInit_ tr_cryptoDecryptInit
#define tr_cryptoDecrypt_ tr_cryptoDecrypt
#define tr_cryptoEncryptInit_ tr_cryptoEncryptInit
#define tr_cryptoEncrypt_ tr_cryptoEncrypt
#define tr_cryptoSecretKeySha1_ tr_cryptoSecretKeySha1
#define tr_sha1_ tr_sha1
#define tr_sha1_init_ tr_sha1_init
#define tr_sha1_update_ tr_sha1_update
#define tr_sha1_final_ tr_sha1_final
#define tr_rc4_new_ tr_rc4_new
#define tr_rc4_free_ tr_rc4_free
#define tr_rc4_set_key_ tr_rc4_set_key
#define tr_rc4_process_ tr_rc4_process
#define tr_dh_new_ tr_dh_new
#define tr_dh_free_ tr_dh_free
#define tr_dh_make_key_ tr_dh_make_key
#define tr_dh_agree_ tr_dh_agree
#define tr_dh_secret_derive_ tr_dh_secret_derive
#define tr_dh_secret_free_ tr_dh_secret_free
#define tr_dh_align_key_ tr_dh_align_key
#define tr_ssl_get_x509_store_ tr_ssl_get_x509_store
#define tr_x509_store_add_ tr_x509_store_add
#define tr_x509_cert_new_ tr_x509_cert_new
#define tr_x509_cert_free_ tr_x509_cert_free
#define tr_rand_int_ tr_rand_int
#define tr_rand_int_weak_ tr_rand_int_weak
#define tr_rand_buffer_ tr_rand_buffer
#define tr_ssha1_ tr_ssha1
#define tr_ssha1_matches_ tr_ssha1_matches
#define tr_base64_encode_ tr_base64_encode
#define tr_base64_encode_str_ tr_base64_encode_str
#define tr_base64_encode_impl_ tr_base64_encode_impl
#define tr_base64_decode_ tr_base64_decode
#define tr_base64_decode_str_ tr_base64_decode_str
#define tr_base64_decode_impl_ tr_base64_decode_impl
#define tr_sha1_to_hex_ tr_sha1_to_hex
#define tr_hex_to_sha1_ tr_hex_to_sha1

#endif /* CRYPTO_REFERENCE_CHECK */

#endif /* TR_CRYPTO_TEST_REF_H */
