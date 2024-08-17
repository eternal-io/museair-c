
This is the official C implementation of [MuseAir](https://github.com/eternal-io/museair).

Currently implemented algorithm version is **0.2**.

## Quick example

```c
uint64_t digest_lo, digest_hi, seed = 42;
char msg[] = "It's a beautiful day outside";

digest_lo = museair_hash_128(msg, strlen(msg), seed, &digest_hi);

printf("%016lx%016lx\n", digest_hi, digest_lo);
```
