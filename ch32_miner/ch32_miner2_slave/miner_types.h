#ifndef MINER_TYPES_H
#define MINER_TYPES_H

#include <stdint.h>

// Arduino .ino は #include の直後に関数プロトタイプを挿入するため、
// sw_sha256_* の引数型はこのヘッダで先に定義しておく
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    uint32_t datalen;
} sw_sha256_ctx_t;

#endif
