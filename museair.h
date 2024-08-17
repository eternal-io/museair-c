/*
 * Copyright (c) 2024 K--Aethiax
 *
 * Modified from "wyhash.h" (mainly these `#define`s), by Wang Yi <godspeed_china@yeah.net>.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the MIT license as described below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(_MSC_VER) && defined(_M_X64)
    #include <intrin.h>
    #pragma intrinsic(_umul128)
#endif

#define MUSEAIR_ALGORITHM_VERSION "0.2"

static const uint64_t MUSEAIR_SECRET[6] = {
    UINT64_C(0x5ae31e589c56e17a), UINT64_C(0x96d7bb04e64f6da9), UINT64_C(0x7ab1006b26f9eb64),
    UINT64_C(0x21233394220b8457), UINT64_C(0x047cb9557c9f3b43), UINT64_C(0xd24f2590c0bcee28),
};  // ``AiryAi(0)`` mantissas calculated by Y-Cruncher.
static const uint64_t MUSEAIR_RING_PREV = UINT64_C(0x33ea8f71bb6016d8);

#ifndef __cplusplus
typedef uint8_t bool;
    #define false 0
    #define true 1
#endif

/*----------------------------------------------------------------------------*/

#define FORCE_INLINE __attribute__((__always_inline__)) inline
#define NEVER_INLINE __attribute__((__noinline__))

#ifndef MUSEAIR_BSWAP
    #if defined(_WIN32) || defined(__LITTLE_ENDIAN__) || \
        (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        #define MUSEAIR_BSWAP 0
    #elif defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        #define MUSEAIR_BSWAP 1
    #else
        #warning unable to determine endianness! treated as little endian
        #define MUSEAIR_BSWAP 0
    #endif
#endif

#if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
    #define _museair_likely(x) __builtin_expect(x, 1)
    #define _museair_unlikely(x) __builtin_expect(x, 0)
#else
    #define _museair_likely(x) (x)
    #define _museair_unlikely(x) (x)
#endif

/*----------------------------------------------------------------------------*/

static FORCE_INLINE uint64_t _museair_bswap_64(uint64_t v) {
    return
#if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
        __builtin_bswap64(v)
#elif defined(_MSC_VER)
        _byteswap_uint64(v)
#else
        ((v & 0xFF00000000000000ull) >> 56) | ((v & 0x00FF000000000000ull) >> 40) |
        ((v & 0x0000FF0000000000ull) >> 24) | ((v & 0x000000FF00000000ull) >> 8) | ((v & 0x00000000FF000000ull) << 8) |
        ((v & 0x0000000000FF0000ull) << 24) | ((v & 0x000000000000FF00ull) << 40) | ((v & 0x00000000000000FFull) << 56)
#endif
            ;
}

static FORCE_INLINE uint32_t _museair_bswap_32(uint32_t v) {
    return
#if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
        __builtin_bswap32(v)
#elif defined(_MSC_VER)
        _byteswap_ulong(v)
#else
        ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >> 8) | ((v & 0x0000FF00u) << 8) | ((v & 0x000000FFu) << 24)
#endif
            ;
}

static FORCE_INLINE uint64_t _museair_read_u64(const uint8_t* p) {
    uint64_t v;
    memcpy(&v, &p[0], 8);
#if MUSEAIR_BSWAP > 0
    v = _museair_bswap_64(v);
#endif
    return v;
}

static FORCE_INLINE uint64_t _museair_read_u32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, &p[0], 4);
#if MUSEAIR_BSWAP > 0
    v = _museair_bswap_32(v);
#endif
    return (uint64_t)v;
}

static FORCE_INLINE void _museair_read_short(const uint8_t* bytes, const size_t len, uint64_t* i, uint64_t* j) {
    // For short inputs, refer to rapidhash, MuseAir has no much different from that.
    if (len >= 4) {
        int off = (len & 24) >> (len >> 3);  // len >= 8 ? 4 : 0
        *i = (_museair_read_u32(bytes) << 32) | _museair_read_u32(bytes + len - 4);
        *j = (_museair_read_u32(bytes + off) << 32) | _museair_read_u32(bytes + len - 4 - off);
    } else if (len > 0) {
        // MSB <-> LSB
        // [0] [0] [0] for len == 1 (0b01)
        // [0] [1] [1] for len == 2 (0b10)
        // [0] [1] [2] for len == 3 (0b11)
        *i = ((uint64_t)bytes[0] << 48) | ((uint64_t)bytes[len >> 1] << 24) | (uint64_t)bytes[len - 1];
        *j = 0;
    } else {
        *i = 0;
        *j = 0;
    }
}

/*----------------------------------------------------------------------------*/

static FORCE_INLINE uint64_t _museair_rotl(uint64_t v, uint8_t n) {
    return (v << n) | (v >> ((-n) & 63));
}
static FORCE_INLINE uint64_t _museair_rotr(uint64_t v, uint8_t n) {
    return (v >> n) | (v << ((-n) & 63));
}

static FORCE_INLINE void _museair_wmul(uint64_t* lo, uint64_t* hi, uint64_t a, uint64_t b) {
#if defined(__SIZEOF_INT128__)
    __uint128_t v = (__uint128_t)a * (__uint128_t)b;
    *lo = (uint64_t)v;
    *hi = (uint64_t)(v >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
    *lo = _umul128(a, b, hi);
#else
    #error todo.
#endif
}

static FORCE_INLINE void _museair_chixx(uint64_t* t, uint64_t* u, uint64_t* v) {
    uint64_t x = ~*u & *v;
    uint64_t y = ~*v & *t;
    uint64_t z = ~*t & *u;
    *t ^= x;
    *u ^= y;
    *v ^= z;
}

static FORCE_INLINE void _museair_frac_6(const bool BFast,
                                         uint64_t* state_p,
                                         uint64_t* state_q,
                                         const uint64_t input_p,
                                         const uint64_t input_q) {
    uint64_t lo, hi;
    if (!BFast) {
        *state_p ^= input_p;
        *state_q ^= input_q;
        _museair_wmul(&lo, &hi, *state_p, *state_q);
        *state_p ^= lo;
        *state_q ^= hi;
    } else {
        _museair_wmul(&lo, &hi, *state_p ^ input_p, *state_q ^ input_q);
        *state_p = lo;
        *state_q = hi;
    }
}

static FORCE_INLINE void _museair_frac_3(const bool BFast, uint64_t* state_p, uint64_t* state_q, const uint64_t input) {
    uint64_t lo, hi;
    if (!BFast) {
        *state_q ^= input;
        _museair_wmul(&lo, &hi, *state_p, *state_q);
        *state_p ^= lo;
        *state_q ^= hi;
    } else {
        _museair_wmul(&lo, &hi, *state_p, *state_q ^ input);
        *state_p = lo;
        *state_q = hi;
    }
}

/*----------------------------------------------------------------------------*/

static FORCE_INLINE void _museair_layer_12(const bool BFast, uint64_t* state, const uint8_t* p, uint64_t* ring_prev) {
    uint64_t lo0, lo1, lo2, lo3, lo4, lo5;
    uint64_t hi0, hi1, hi2, hi3, hi4, hi5;
    if (!BFast) {
        state[0] ^= _museair_read_u64(p + 8 * 0);
        state[1] ^= _museair_read_u64(p + 8 * 1);
        _museair_wmul(&lo0, &hi0, state[0], state[1]);
        state[0] += *ring_prev ^ hi0;

        state[1] ^= _museair_read_u64(p + 8 * 2);
        state[2] ^= _museair_read_u64(p + 8 * 3);
        _museair_wmul(&lo1, &hi1, state[1], state[2]);
        state[1] += lo0 ^ hi1;

        state[2] ^= _museair_read_u64(p + 8 * 4);
        state[3] ^= _museair_read_u64(p + 8 * 5);
        _museair_wmul(&lo2, &hi2, state[2], state[3]);
        state[2] += lo1 ^ hi2;

        state[3] ^= _museair_read_u64(p + 8 * 6);
        state[4] ^= _museair_read_u64(p + 8 * 7);
        _museair_wmul(&lo3, &hi3, state[3], state[4]);
        state[3] += lo2 ^ hi3;

        state[4] ^= _museair_read_u64(p + 8 * 8);
        state[5] ^= _museair_read_u64(p + 8 * 9);
        _museair_wmul(&lo4, &hi4, state[4], state[5]);
        state[4] += lo3 ^ hi4;

        state[5] ^= _museair_read_u64(p + 8 * 10);
        state[0] ^= _museair_read_u64(p + 8 * 11);
        _museair_wmul(&lo5, &hi5, state[5], state[0]);
        state[5] += lo4 ^ hi5;
    } else {
        state[0] ^= _museair_read_u64(p + 8 * 0);
        state[1] ^= _museair_read_u64(p + 8 * 1);
        _museair_wmul(&lo0, &hi0, state[0], state[1]);
        state[0] = *ring_prev ^ hi0;

        state[1] ^= _museair_read_u64(p + 8 * 2);
        state[2] ^= _museair_read_u64(p + 8 * 3);
        _museair_wmul(&lo1, &hi1, state[1], state[2]);
        state[1] = lo0 ^ hi1;

        state[2] ^= _museair_read_u64(p + 8 * 4);
        state[3] ^= _museair_read_u64(p + 8 * 5);
        _museair_wmul(&lo2, &hi2, state[2], state[3]);
        state[2] = lo1 ^ hi2;

        state[3] ^= _museair_read_u64(p + 8 * 6);
        state[4] ^= _museair_read_u64(p + 8 * 7);
        _museair_wmul(&lo3, &hi3, state[3], state[4]);
        state[3] = lo2 ^ hi3;

        state[4] ^= _museair_read_u64(p + 8 * 8);
        state[5] ^= _museair_read_u64(p + 8 * 9);
        _museair_wmul(&lo4, &hi4, state[4], state[5]);
        state[4] = lo3 ^ hi4;

        state[5] ^= _museair_read_u64(p + 8 * 10);
        state[0] ^= _museair_read_u64(p + 8 * 11);
        _museair_wmul(&lo5, &hi5, state[5], state[0]);
        state[5] = lo4 ^ hi5;
    }
    *ring_prev = lo5;
}

static FORCE_INLINE void _museair_layer_6(const bool BFast, uint64_t* state, const uint8_t* p) {
    _museair_frac_6(BFast, &state[0], &state[1], _museair_read_u64(p + 8 * 0), _museair_read_u64(p + 8 * 1));
    _museair_frac_6(BFast, &state[2], &state[3], _museair_read_u64(p + 8 * 2), _museair_read_u64(p + 8 * 3));
    _museair_frac_6(BFast, &state[4], &state[5], _museair_read_u64(p + 8 * 4), _museair_read_u64(p + 8 * 5));
}

static FORCE_INLINE void _museair_layer_3(const bool BFast, uint64_t* state, const uint8_t* p) {
    _museair_frac_3(BFast, &state[0], &state[3], _museair_read_u64(p + 8 * 0));
    _museair_frac_3(BFast, &state[1], &state[4], _museair_read_u64(p + 8 * 1));
    _museair_frac_3(BFast, &state[2], &state[5], _museair_read_u64(p + 8 * 2));
}

static FORCE_INLINE void
_museair_layer_0(uint64_t* state, const uint8_t* p, size_t q, size_t len, uint64_t* i, uint64_t* j, uint64_t* k) {
    if (q <= 8 * 2) {
        uint64_t i_, j_;
        _museair_read_short(p, q, &i_, &j_);
        *i = i_;
        *j = j_;
        *k = 0;
    } else {
        *i = _museair_read_u64(p);
        *j = _museair_read_u64(p + 8);
        *k = _museair_read_u64(p + q - 8);
    }

    if (len >= 8 * 3) {
        _museair_chixx(&state[0], &state[2], &state[4]);
        _museair_chixx(&state[1], &state[3], &state[5]);
        *i ^= state[0] + state[1];
        *j ^= state[2] + state[3];
        *k ^= state[4] + state[5];
    } else {
        *i ^= state[0];
        *j ^= state[1];
        *k ^= state[2];
    }
}

static FORCE_INLINE void _museair_layer_f(const bool BFast, size_t len, uint64_t* i, uint64_t* j, uint64_t* k) {
    uint8_t rot = (uint8_t)len & 63;
    _museair_chixx(i, j, k);
    *i = _museair_rotl(*i, rot);
    *j = _museair_rotr(*j, rot);
    *k ^= (uint64_t)len;

    uint64_t lo0, lo1, lo2;
    uint64_t hi0, hi1, hi2;
    if (!BFast) {
        _museair_wmul(&lo0, &hi0, *i ^ MUSEAIR_SECRET[3], *j);
        _museair_wmul(&lo1, &hi1, *j ^ MUSEAIR_SECRET[4], *k);
        _museair_wmul(&lo2, &hi2, *k ^ MUSEAIR_SECRET[5], *i);
        *i ^= lo0 ^ hi2;
        *j ^= lo1 ^ hi0;
        *k ^= lo2 ^ hi1;
    } else {
        _museair_wmul(&lo0, &hi0, *i, *j);
        _museair_wmul(&lo1, &hi1, *j, *k);
        _museair_wmul(&lo2, &hi2, *k, *i);
        *i = lo0 ^ hi2;
        *j = lo1 ^ hi0;
        *k = lo2 ^ hi1;
    }
}

/*----------------------------------------------------------------------------*/

static FORCE_INLINE void _museair_tower_loong(const bool BFast,
                                              const uint8_t* bytes,
                                              const size_t len,
                                              const uint64_t seed,
                                              uint64_t* i,
                                              uint64_t* j,
                                              uint64_t* k) {
    const uint8_t* p = bytes;
    size_t q = len;

    uint64_t state[6] = {MUSEAIR_SECRET[0] + seed, MUSEAIR_SECRET[1] - seed, MUSEAIR_SECRET[2] ^ seed,
                         MUSEAIR_SECRET[3],        MUSEAIR_SECRET[4],        MUSEAIR_SECRET[5]};

    if (q >= 8 * 12) {
        state[3] += seed;
        state[4] -= seed;
        state[5] ^= seed;
        uint64_t ring_prev = MUSEAIR_RING_PREV;
        do {
            _museair_layer_12(BFast, &state[0], p, &ring_prev);
            p += 8 * 12;
            q -= 8 * 12;
        } while (_museair_likely(q >= 8 * 12));
        state[0] ^= ring_prev;
    }

    if (q >= 8 * 6) {
        _museair_layer_6(BFast, &state[0], p);
        p += 8 * 6;
        q -= 8 * 6;
    }

    if (q >= 8 * 3) {
        _museair_layer_3(BFast, &state[0], p);
        p += 8 * 3;
        q -= 8 * 3;
    }

    _museair_layer_0(&state[0], p, q, len, i, j, k);
    _museair_layer_f(BFast, len, i, j, k);
}

static FORCE_INLINE void _museair_tower_short(const uint8_t* bytes,
                                              const size_t len,
                                              const uint64_t seed,
                                              uint64_t* i,
                                              uint64_t* j) {
    uint64_t lo, hi;
    _museair_read_short(bytes, len, i, j);
    _museair_wmul(&lo, &hi, seed ^ MUSEAIR_SECRET[0], len ^ MUSEAIR_SECRET[1]);
    *i ^= lo ^ len;
    *j ^= hi ^ seed;
}

/*----------------------------------------------------------------------------*/

static FORCE_INLINE void _museair_epi_short(uint64_t* i, uint64_t* j) {
    uint64_t lo, hi;
    *i ^= MUSEAIR_SECRET[2];
    *j ^= MUSEAIR_SECRET[3];
    _museair_wmul(&lo, &hi, *i, *j);
    *i ^= lo ^ MUSEAIR_SECRET[4];
    *j ^= hi ^ MUSEAIR_SECRET[5];
    _museair_wmul(&lo, &hi, *i, *j);
    *i ^= *j ^ lo ^ hi;
}
static FORCE_INLINE void _museair_epi_short_128(const bool BFast, uint64_t* i, uint64_t* j) {
    uint64_t lo0, lo1;
    uint64_t hi0, hi1;
    if (!BFast) {
        _museair_wmul(&lo0, &hi0, *i ^ MUSEAIR_SECRET[2], *j);
        _museair_wmul(&lo1, &hi1, *i, *j ^ MUSEAIR_SECRET[3]);
        *i ^= lo0 ^ hi1;
        *j ^= lo1 ^ hi0;
        _museair_wmul(&lo0, &hi0, *i ^ MUSEAIR_SECRET[4], *j);
        _museair_wmul(&lo1, &hi1, *i, *j ^ MUSEAIR_SECRET[5]);
        *i ^= lo0 ^ hi1;
        *j ^= lo1 ^ hi0;
    } else {
        _museair_wmul(&lo0, &hi0, *i, *j);
        _museair_wmul(&lo1, &hi1, *i ^ MUSEAIR_SECRET[2], *j ^ MUSEAIR_SECRET[3]);
        *i = lo0 ^ hi1;
        *j = lo1 ^ hi0;
        _museair_wmul(&lo0, &hi0, *i, *j);
        _museair_wmul(&lo1, &hi1, *i ^ MUSEAIR_SECRET[4], *j ^ MUSEAIR_SECRET[5]);
        *i = lo0 ^ hi1;
        *j = lo1 ^ hi0;
    }
}

static FORCE_INLINE void _museair_epi_loong(const bool BFast, uint64_t* i, uint64_t* j, uint64_t* k) {
    uint64_t lo0, lo1, lo2;
    uint64_t hi0, hi1, hi2;
    if (!BFast) {
        _museair_wmul(&lo0, &hi0, *i ^ MUSEAIR_SECRET[0], *j);
        _museair_wmul(&lo1, &hi1, *j ^ MUSEAIR_SECRET[1], *k);
        _museair_wmul(&lo2, &hi2, *k ^ MUSEAIR_SECRET[2], *i);
        *i ^= lo0 ^ hi2;
        *j ^= lo1 ^ hi0;
        *k ^= lo2 ^ hi1;
    } else {
        _museair_wmul(&lo0, &hi0, *i, *j);
        _museair_wmul(&lo1, &hi1, *j, *k);
        _museair_wmul(&lo2, &hi2, *k, *i);
        *i = lo0 ^ hi2;
        *j = lo1 ^ hi0;
        *k = lo2 ^ hi1;
    }
    *i += *j + *k;
}
static FORCE_INLINE void _museair_epi_loong_128(const bool BFast, uint64_t* i, uint64_t* j, uint64_t* k) {
    uint64_t lo0, lo1, lo2;
    uint64_t hi0, hi1, hi2;
    if (!BFast) {
        _museair_wmul(&lo0, &hi0, *i ^ MUSEAIR_SECRET[0], *j);
        _museair_wmul(&lo1, &hi1, *j ^ MUSEAIR_SECRET[1], *k);
        _museair_wmul(&lo2, &hi2, *k ^ MUSEAIR_SECRET[2], *i);
        *i ^= lo0 ^ lo1 ^ hi2;
        *j ^= hi0 ^ hi1 ^ lo2;
    } else {
        _museair_wmul(&lo0, &hi0, *i, *j);
        _museair_wmul(&lo1, &hi1, *j, *k);
        _museair_wmul(&lo2, &hi2, *k, *i);
        *i = lo0 ^ lo1 ^ hi2;
        *j = hi0 ^ hi1 ^ lo2;
    }
}

/*----------------------------------------------------------------------------*/

static FORCE_INLINE void _museair_hash_short(const uint8_t* bytes,
                                             const size_t len,
                                             const uint64_t seed,
                                             uint64_t* i,
                                             uint64_t* j) {
    _museair_tower_short(bytes, len, seed, i, j);
    _museair_epi_short(i, j);
}
static FORCE_INLINE void _museair_hash_short_128(const bool BFast,
                                                 const uint8_t* bytes,
                                                 const size_t len,
                                                 const uint64_t seed,
                                                 uint64_t* i,
                                                 uint64_t* j) {
    _museair_tower_short(bytes, len, seed, i, j);
    _museair_epi_short_128(BFast, i, j);
}

static NEVER_INLINE void _museair_hash_loong(const bool BFast,
                                             const uint8_t* bytes,
                                             const size_t len,
                                             const uint64_t seed,
                                             uint64_t* i,
                                             uint64_t* j,
                                             uint64_t* k) {
    _museair_tower_loong(BFast, bytes, len, seed, i, j, k);
    _museair_epi_loong(BFast, i, j, k);
}
static NEVER_INLINE void _museair_hash_loong_128(const bool BFast,
                                                 const uint8_t* bytes,
                                                 const size_t len,
                                                 const uint64_t seed,
                                                 uint64_t* i,
                                                 uint64_t* j,
                                                 uint64_t* k) {
    _museair_tower_loong(BFast, bytes, len, seed, i, j, k);
    _museair_epi_loong_128(BFast, i, j, k);
}

/*----------------------------------------------------------------------------*/

static FORCE_INLINE uint64_t _museair_hash(const bool BFast, const void* in, const size_t len, const uint64_t seed) {
    uint64_t i, j, k;
    if (_museair_likely(len <= 16)) {
        _museair_hash_short((const uint8_t*)in, len, seed, &i, &j);
    } else {
        _museair_hash_loong(BFast, (const uint8_t*)in, len, seed, &i, &j, &k);
    }
#if MUSEAIR_BSWAP > 0
    i = _museair_bswap_64(i);
#endif
    return i;
}

static FORCE_INLINE uint64_t
_museair_hash_128(const bool BFast, const void* in, const size_t len, const uint64_t seed, uint64_t* upper_half) {
    uint64_t i, j, k;
    if (_museair_likely(len <= 16)) {
        _museair_hash_short_128(BFast, (const uint8_t*)in, len, seed, &i, &j);
    } else {
        _museair_hash_loong_128(BFast, (const uint8_t*)in, len, seed, &i, &j, &k);
    }
#if MUSEAIR_BSWAP > 0
    i = _museair_bswap_64(i);
    j = _museair_bswap_64(j);
#endif
    *upper_half = j;
    return i;
}

/*----------------------------------------------------------------------------*/

static inline uint64_t museair_hash(const void* in, const size_t len, const uint64_t seed) {
    return _museair_hash(false, in, len, seed);
}
static inline uint64_t museair_hash_128(const void* in, const size_t len, const uint64_t seed, uint64_t* upper_half) {
    return _museair_hash_128(false, in, len, seed, upper_half);
}
static inline uint64_t museair_bfast_hash(const void* in, const size_t len, const uint64_t seed) {
    return _museair_hash(true, in, len, seed);
}
static inline uint64_t museair_bfast_hash_128(const void* in,
                                              const size_t len,
                                              const uint64_t seed,
                                              uint64_t* upper_half) {
    return _museair_hash_128(true, in, len, seed, upper_half);
}
