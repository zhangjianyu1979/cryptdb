/*
 * Based on OpenBSD's src/sbin/bioctl/pbkdf2.c, which had the following
 * copyright notice:
 *
 * Copyright (c) 2008 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/param.h>

#include <string.h>
#include <limits.h>
#include <stdlib.h>

#include <openssl/sha.h>

#include <crypto/pbkdf2.hh>
#include <util/errstream.hh>

/*
 * HMAC-SHA-1 (from RFC 2202).
 */
#define SHA1_DIGEST_LENGTH 20
#define SHA1_BLOCK_LENGTH  64

static void
hmac_sha1(const u_int8_t *text, size_t text_len, const u_int8_t *key,
          size_t key_len, u_int8_t digest[SHA1_DIGEST_LENGTH])
{
    SHA_CTX ctx;
    u_int8_t k_pad[SHA1_BLOCK_LENGTH];
    u_int8_t tk[SHA1_DIGEST_LENGTH];
    int i;

    if (key_len > SHA1_BLOCK_LENGTH) {
        SHA1_Init(&ctx);
        SHA1_Update(&ctx, key, key_len);
        SHA1_Final(tk, &ctx);

        key = tk;
        key_len = SHA1_DIGEST_LENGTH;
    }

    bzero(k_pad, sizeof k_pad);
    bcopy(key, k_pad, key_len);
    for (i = 0; i < SHA1_BLOCK_LENGTH; i++)
        k_pad[i] ^= 0x36;

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, k_pad, SHA1_BLOCK_LENGTH);
    SHA1_Update(&ctx, text, text_len);
    SHA1_Final(digest, &ctx);

    bzero(k_pad, sizeof k_pad);
    bcopy(key, k_pad, key_len);
    for (i = 0; i < SHA1_BLOCK_LENGTH; i++)
        k_pad[i] ^= 0x5c;

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, k_pad, SHA1_BLOCK_LENGTH);
    SHA1_Update(&ctx, digest, SHA1_DIGEST_LENGTH);
    SHA1_Final(digest, &ctx);
}

/*
 * Password-Based Key Derivation Function 2 (PKCS #5 v2.0).
 * Code based on IEEE Std 802.11-2007, Annex H.4.2.
 */
static int
pkcs5_pbkdf2(const char *pass, size_t pass_len, const char *salt,
             size_t salt_len,
             u_int8_t *key, size_t key_len,
             u_int rounds)
{
    u_int8_t *asalt, obuf[SHA1_DIGEST_LENGTH];
    u_int8_t d1[SHA1_DIGEST_LENGTH], d2[SHA1_DIGEST_LENGTH];
    u_int i, j;
    u_int count;
    size_t r;

    if (rounds < 1 || key_len == 0)
        return -1;
    if (salt_len == 0 || salt_len > SIZE_MAX - 1)
        return -1;
    if ((asalt = (uint8_t *) malloc(salt_len + 4)) == NULL)
        return -1;

    memcpy(asalt, salt, salt_len);

    for (count = 1; key_len > 0; count++) {
        asalt[salt_len + 0] = (uint8_t) ((count >> 24) & 0xff);
        asalt[salt_len + 1] = (count >> 16) & 0xff;
        asalt[salt_len + 2] = (count >> 8) & 0xff;
        asalt[salt_len + 3] = count & 0xff;
        hmac_sha1(asalt, salt_len + 4,
                  (const uint8_t *) pass, pass_len, d1);
        memcpy(obuf, d1, sizeof(obuf));

        for (i = 1; i < rounds; i++) {
            hmac_sha1(d1, sizeof(d1),
                      (const uint8_t *) pass, pass_len, d2);
            memcpy(d1, d2, sizeof(d1));
            for (j = 0; j < sizeof(obuf); j++)
                obuf[j] ^= d1[j];
        }

        r = MIN(key_len, SHA1_DIGEST_LENGTH);
        memcpy(key, obuf, r);
        key += r;
        key_len -= r;
    };
    bzero(asalt, salt_len + 4);
    free(asalt);
    bzero(d1, sizeof(d1));
    bzero(d2, sizeof(d2));
    bzero(obuf, sizeof(obuf));

    return 0;
}

using namespace std;

string
pbkdf2(const string &pass, const string &salt, uint key_len, uint rounds)
{
    string key;
    key.resize(key_len);

    int r = pkcs5_pbkdf2(pass.c_str(), pass.length(),
                         salt.c_str(), salt.length(),
                         (uint8_t *) &key[0], key.length(), rounds);
    throw_c(r == 0);

    return key;
}
