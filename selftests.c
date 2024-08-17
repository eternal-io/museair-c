/*
 * Stand-alone hash verification code generator for SMHasher3
 * Copyright (C) 2022  Frank J. T. Wojcik
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

uint64_t HASH_INIT(uint64_t seed) {
    return seed;
}

uint32_t ComputedVerifyImpl(const uint32_t hashbits,
                            void (*HASH)(const void* in, const size_t len, const uint64_t seed, void* out)) {
    const uint32_t hashbytes = hashbits / 8;

    uint8_t* key = (uint8_t*)calloc(256, 1);
    uint8_t* hashes = (uint8_t*)calloc(hashbytes, 256);
    uint8_t* total = (uint8_t*)calloc(hashbytes, 1);

    // Hash keys of the form {}, {0}, {0,1}, {0,1,2}... up to N=255, using
    // 256-N as the seed
    for (int i = 0; i < 256; i++) {
        uint64_t seed = 256 - i;
        seed = HASH_INIT(seed);
        HASH(key, i, seed, &hashes[i * hashbytes]);
        key[i] = (uint8_t)i;
    }

    // Then hash the result array
    uint64_t seed = 0;
    seed = HASH_INIT(0);
    HASH(hashes, hashbytes * 256, seed, total);

    // The first four bytes of that hash, interpreted as a little-endian
    // integer, is our verification value
    uint32_t verification = (total[0] << 0) | (total[1] << 8) | (total[2] << 16) | (total[3] << 24);

    free(total);
    free(hashes);
    free(key);

    return verification;
}

#include "museair.h"
int main() {
    // ensure we are on a little endian machine
    if (ComputedVerifyImpl(64, museair_hash) != 0x46B2D34D)
        printf("Unexpected museair_hash!\n");
    if (ComputedVerifyImpl(128, museair_hash_128) != 0xCABAA4CD)
        printf("Unexpected museair_hash_128!\n");
    if (ComputedVerifyImpl(64, museair_bfast_hash) != 0x98CDFE3E)
        printf("Unexpected museair_bfast_hash!\n");
    if (ComputedVerifyImpl(128, museair_bfast_hash_128) != 0x81D30B6E)
        printf("Unexpected museair_bfast_hash_128!\n");
    printf("Finish.\n");
}
