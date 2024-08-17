
This is the official C implementation of [MuseAir](https://github.com/eternal-io/museair).

Currently implemented algorithm version is **0.2**.

## Quick example

```c
uint64_t digest, seed = 42;
char msg[] = "It's a beautiful day outside";

museair_hash(msg, strlen(msg), seed, &digest);

printf("%016lx\n", digest);
```
