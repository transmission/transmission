/*
 sha1.h: Implementation of SHA-1 Secure Hash Algorithm-1

 Based upon: NIST FIPS180-1 Secure Hash Algorithm-1
   http://www.itl.nist.gov/fipspubs/fip180-1.htm

 Non-official Japanese Translation by HIRATA Yasuyuki:
   http://yasu.asuka.net/translations/SHA-1.html

 Copyright (C) 2002 vi@nwr.jp. All rights reserved.

 This software is provided 'as-is', without any express or implied
 warranty. In no event will the authors be held liable for any damages
 arising from the use of this software.

 Permission is granted to anyone to use this software for any purpose,
 including commercial applications, and to alter it and redistribute it
 freely, subject to the following restrictions:

 1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software
    in a product, an acknowledgement in the product documentation would be
    appreciated but is not required.
 2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as beging the original software.
 3. This notice may not be removed or altered from any source distribution.

 Note:
   The copyright notice above is copied from md5.h by L. Petet Deutsch
   <ghost@aladdin.com>. Thank him since I'm not a good speaker of English. :)
 */
#ifndef SHA1_H
#define SHA1_H

/* We use OpenSSL whenever possible, since it is likely to be more
   optimized and it is ok to use it with a MIT-licensed application.
   Otherwise, we use the included implementation by vi@nwr.jp. */
#if defined(HAVE_OPENSSL) || defined(HAVE_LIBSSL)

    #undef SHA_DIGEST_LENGTH
    #include <openssl/sha.h>

#else

    #include <inttypes.h>
    #define SHA1_OUTPUT_SIZE 20 /* in bytes */

    typedef struct sha1_state_s sha1_state_t;
    typedef uint32_t sha1_word_t;  /* 32bits unsigned integer */
    typedef uint8_t sha1_byte_t;   /* 8bits unsigned integer */

    /* Initialize SHA-1 algorithm */
    void sha1_init(sha1_state_t *pms);

    /* Append a string to SHA-1 algorithm */
    void sha1_update(sha1_state_t *pms, sha1_byte_t *input_buffer, int length);

    /* Finish the SHA-1 algorithm and return the hash */
    void sha1_finish(sha1_state_t *pms, sha1_byte_t output[SHA1_OUTPUT_SIZE]);

    /* Convenience version of the above */
    void tr_sha1( const void * input_buffer, int length, unsigned char * output);

    #define SHA1(in,inlen,out) tr_sha1(in,inlen,out)

#endif /* ifndef HAVE_OPENSSL, HAVE_LIBSSL */

#endif /* #ifndef SHA1_H */
