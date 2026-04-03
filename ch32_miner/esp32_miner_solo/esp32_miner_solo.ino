// M5Stack CoreS3: Stratum SHA256d (Bitcoin系) マイニング

#include <Arduino.h>
#include <stdarg.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>

// Arduino IDE が #include の直後に自動生成する関数プロトタイプより前に型名が必要
struct MiningJob;

// =========================
// 設定（WiFi は環境に合わせて変更）
// =========================
static const char *WIFI_SSID = "****";
static const char *WIFI_PASS = "****";

// altpool FxTC マイニング
// static const char *POOL_HOST = "altpool.eu";
// static const uint16_t POOL_PORT = 3333;
// static const char *POOL_USER = "Your_Wallet_Address";
// static const char *POOL_PASS = "c=FxTC-sha256d";

// nerdminers.org BTCマイニング　全然開始しない
// static const char *POOL_HOST = "pool.nerdminers.org";
// static const uint16_t POOL_PORT = 3333;
// static const char *POOL_USER = "Your_Wallet_Address.esp32";
// static const char *POOL_PASS = "x";

// public-pool.io BTCマイニング　掘れる
// static const char *POOL_HOST = "public-pool.io";
// static const uint16_t POOL_PORT = 3333;
// static const char *POOL_USER = "Your_Wallet_Address.esp32";
// static const char *POOL_PASS = "x";

// NMMiner公式 BTCマイニング　掘れる
static const char *POOL_HOST = "solobtc.nmminer.com";
static const uint16_t POOL_PORT = 3333;
static const char *POOL_USER = "Your_Wallet_Address.esp32";
static const char *POOL_PASS = "x";

static const char *MINER_NAME = "esp32-miner/1.0";

// 1 = mbedtls の SHA-256、0 = 同梱ソフトウェア実装（ビルド前に切り替え）
#define USE_MBEDTLS_SHA256 0
#ifndef USE_MBEDTLS_SHA256
#define USE_MBEDTLS_SHA256 1
#endif

// マイニングを実行するコア (0 または 1)。将来もう一方のコアも使う場合は別タスクで変更。
static const BaseType_t MINING_CORE = 1;

// 1コアのみ。nonce をこの回数ごとに TCP 処理へ譲る
static const uint32_t NONCE_BATCH = 2048;

// 有効化: nonce ループ内の各区間を micros() で集計し、kH/s と同じ 5 秒窓で平均 µs を 1 行ログ
// #define ESP32_MINER_PROFILE_NONCE_LOOP

// =========================
// sha256d 関数ポインタ型
// =========================
typedef void (*sha256d_func_t)(const uint8_t *, uint8_t *);

volatile uint32_t g_sink = 0;

#ifdef ESP32_MINER_PROFILE_NONCE_LOOP
static uint64_t g_prof_sum_nonce_hex_us = 0;
static uint64_t g_prof_sum_build_header_us = 0;
static uint64_t g_prof_sum_sha256d_us = 0;
static uint64_t g_prof_sum_hash_fulltest_us = 0;
static uint32_t g_prof_sample_count = 0;
#endif

// =========================
// 最小 SHA-256 ソフト実装
// =========================
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    uint32_t datalen;
} sw_sha256_ctx_t;

static const uint32_t sw_k[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

#define SW_ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32 - (b))))
#define SW_CH(x,y,z)     (((x) & (y)) ^ (~(x) & (z)))
#define SW_MAJ(x,y,z)    (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SW_EP0(x)        (SW_ROTRIGHT((x), 2) ^ SW_ROTRIGHT((x),13) ^ SW_ROTRIGHT((x),22))
#define SW_EP1(x)        (SW_ROTRIGHT((x), 6) ^ SW_ROTRIGHT((x),11) ^ SW_ROTRIGHT((x),25))
#define SW_SIG0(x)       (SW_ROTRIGHT((x), 7) ^ SW_ROTRIGHT((x),18) ^ ((x) >> 3))
#define SW_SIG1(x)       (SW_ROTRIGHT((x),17) ^ SW_ROTRIGHT((x),19) ^ ((x) >> 10))

static uint32_t sw_load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |
           ((uint32_t)p[3] <<  0);
}

static void sw_store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8);
    p[3] = (uint8_t)(v >>  0);
}

static void sw_store_be64(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v >> 56);
    p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >>  8);
    p[7] = (uint8_t)(v >>  0);
}

static void sw_sha256_transform(sw_sha256_ctx_t *ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    uint32_t m[64];
    uint32_t i;

    for (i = 0; i < 16; i++) {
        m[i] = sw_load_be32(&data[i * 4]);
    }
    for (i = 16; i < 64; i++) {
        m[i] = SW_SIG1(m[i - 2]) + m[i - 7] + SW_SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + SW_EP1(e) + SW_CH(e, f, g) + sw_k[i] + m[i];
        t2 = SW_EP0(a) + SW_MAJ(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sw_sha256_init(sw_sha256_ctx_t *ctx) {
    ctx->datalen = 0;
    ctx->bitlen  = 0;
    ctx->state[0] = 0x6a09e667UL;
    ctx->state[1] = 0xbb67ae85UL;
    ctx->state[2] = 0x3c6ef372UL;
    ctx->state[3] = 0xa54ff53aUL;
    ctx->state[4] = 0x510e527fUL;
    ctx->state[5] = 0x9b05688cUL;
    ctx->state[6] = 0x1f83d9abUL;
    ctx->state[7] = 0x5be0cd19UL;
}

static void sw_sha256_update(sw_sha256_ctx_t *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            sw_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sw_sha256_final(sw_sha256_ctx_t *ctx, uint8_t hash[32]) {
    ctx->bitlen += ((uint64_t)ctx->datalen * 8);

    ctx->data[ctx->datalen++] = 0x80;

    if (ctx->datalen > 56) {
        while (ctx->datalen < 64) ctx->data[ctx->datalen++] = 0x00;
        sw_sha256_transform(ctx, ctx->data);
        ctx->datalen = 0;
    }

    while (ctx->datalen < 56) ctx->data[ctx->datalen++] = 0x00;

    sw_store_be64(&ctx->data[56], ctx->bitlen);
    sw_sha256_transform(ctx, ctx->data);

    for (uint32_t i = 0; i < 8; i++) {
        sw_store_be32(&hash[i * 4], ctx->state[i]);
    }
}

static void sw_sha256(const uint8_t *data, size_t len, uint8_t hash[32]) {
    sw_sha256_ctx_t ctx;
    sw_sha256_init(&ctx);
    sw_sha256_update(&ctx, data, len);
    sw_sha256_final(&ctx, hash);
}

// =========================
// sha256d
// =========================
static void sha256d_hw(const uint8_t *input80, uint8_t out32[32]) {
    uint8_t tmp[32];
    mbedtls_sha256_context ctx;

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, input80, 80);
    mbedtls_sha256_finish(&ctx, tmp);
    mbedtls_sha256_free(&ctx);

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, tmp, 32);
    mbedtls_sha256_finish(&ctx, out32);
    mbedtls_sha256_free(&ctx);
}

static void sha256d_sw(const uint8_t *input80, uint8_t out32[32]) {
    uint8_t tmp[32];
    sw_sha256(input80, 80, tmp);
    sw_sha256(tmp, 32, out32);
}

#if USE_MBEDTLS_SHA256
static sha256d_func_t g_sha256d = sha256d_hw;
#else
static sha256d_func_t g_sha256d = sha256d_sw;
#endif

static void sha256d_64(const uint8_t *data64, uint8_t out32[32]) {
    uint8_t tmp[32];
#if USE_MBEDTLS_SHA256
    {
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, data64, 64);
        mbedtls_sha256_finish(&ctx, tmp);
        mbedtls_sha256_free(&ctx);
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, tmp, 32);
        mbedtls_sha256_finish(&ctx, out32);
        mbedtls_sha256_free(&ctx);
    }
#else
    {
        sw_sha256(data64, 64, tmp);
        sw_sha256(tmp, 32, out32);
    }
#endif
}

static void init_sha256d_backend() {
#if USE_MBEDTLS_SHA256
    g_sha256d = sha256d_hw;
#else
    g_sha256d = sha256d_sw;
#endif
}

// =========================
// ユーティリティ: hex / endian / uint256
// =========================
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hex_decode(const char *hex, uint8_t *out, size_t out_len) {
    if (!hex) return false;
    size_t n = strlen(hex);
    if (n != out_len * 2) return false;
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_val(hex[i * 2]);
        int lo = hex_val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static void hex_encode(const uint8_t *data, size_t len, char *out_hex) {
    static const char *xd = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out_hex[i * 2]     = xd[data[i] >> 4];
        out_hex[i * 2 + 1] = xd[data[i] & 0x0F];
    }
    out_hex[len * 2] = '\0';
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

// SHA256 出力の各 4 バイトをビッグエンディアン uint32 として読む（cpuminer の be32dec と同じ）
static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

// cpuminer util.c 相当（pooler/cpuminer stratum + sha256d 共有判定と一致させる）
static inline uint32_t swab32(uint32_t v) {
    return ((v & 0xff000000u) >> 24) | ((v & 0x00ff0000u) >> 8) | ((v & 0x0000ff00u) << 8) |
           ((v & 0x000000ffu) << 24);
}

static void diff_to_target(uint32_t *target, double diff) {
    uint64_t m;
    int k;
    for (k = 6; k > 0 && diff > 1.0; k--) {
        diff /= 4294967296.0;
    }
    m = (uint64_t)(4294901760.0 / diff);
    if (m == 0 && k == 6) {
        memset(target, 0xff, 32);
    } else {
        memset(target, 0, 32);
        target[k]   = (uint32_t)m;
        target[k + 1] = (uint32_t)(m >> 32);
    }
}

static bool fulltest(const uint32_t *hash, const uint32_t *target) {
    for (int i = 7; i >= 0; i--) {
        if (hash[i] > target[i]) {
            return false;
        }
        if (hash[i] < target[i]) {
            return true;
        }
    }
    return true;
}

// SHA256 出力 32B → cpuminer fulltest 用 uint32[8]
// sha256d_80_swap は最終行で hash[i] = swab32(state[i]) を返す。
// 標準 SHA256 の出力は BE(state[i]) なので read_be32→state[i]→swab32 で一致させる。
static void hash_bytes_to_cpuminer_u32(const uint8_t *hash32, uint32_t out[8]) {
    for (int i = 0; i < 8; i++) {
        out[i] = swab32(read_be32(hash32 + i * 4));
    }
}

// =========================
// ジョブ
// =========================
static const int MAX_MERKLE_BRANCHES = 16;

struct MiningJob {
    bool valid;
    char job_id[96];
    uint8_t prevhash[32];
    uint8_t coinb1[256];
    size_t  coinb1_len;
    uint8_t coinb2[256];
    size_t  coinb2_len;
    uint8_t branch_hash[MAX_MERKLE_BRANCHES][32];
    int     n_branches;
    uint32_t version;
    uint32_t nbits;
    uint32_t ntime;
    bool clean_jobs;
};

static char g_extranonce1_hex[64];
static int g_extranonce2_size = 4;

static MiningJob g_job;
static SemaphoreHandle_t g_job_mutex;
static volatile bool g_job_updated = false;
static volatile bool g_stop_mining = false;

static double g_difficulty = 1.0;

// =========================
// Merkle / ヘッダ
// =========================
// 80B ブロックヘッダのバイト配置:
//   version / ntime / nbits: hex_decode の BE 表現を rev4 でブロックヘッダの LE に変換
//   prevhash: Stratum hex は各 4B ワードがブロックヘッダと逆順 → rev4 で復元
//   merkle_root: SHA256d 生 32B をそのまま memcpy。nonce: LE で 76–79。
static inline void header_word_rev4(const uint8_t le_or_naive4[4], uint8_t *dst) {
    dst[0] = le_or_naive4[3];
    dst[1] = le_or_naive4[2];
    dst[2] = le_or_naive4[1];
    dst[3] = le_or_naive4[0];
}

static void build_merkle_root(const MiningJob *job, const uint8_t *coinbase, size_t coinbase_len,
                              uint8_t merkle_root[32]) {
    uint8_t h[32];
    uint8_t buf[64];

    {
        uint8_t t1[32], t2[32];
#if USE_MBEDTLS_SHA256
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, coinbase, coinbase_len);
        mbedtls_sha256_finish(&ctx, t1);
        mbedtls_sha256_free(&ctx);
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, t1, 32);
        mbedtls_sha256_finish(&ctx, t2);
        mbedtls_sha256_free(&ctx);
#else
        sw_sha256(coinbase, coinbase_len, t1);
        sw_sha256(t1, 32, t2);
#endif
        memcpy(h, t2, 32);
    }

    for (int i = 0; i < job->n_branches; i++) {
        memcpy(buf, h, 32);
        memcpy(buf + 32, job->branch_hash[i], 32);
        sha256d_64(buf, h);
    }
    memcpy(merkle_root, h, 32);
}

static void build_block_header(const MiningJob *job, uint8_t header[80], const uint8_t merkle_root[32],
                               uint32_t nonce) {
    uint8_t w[4];

    write_le32(w, job->version);
    header_word_rev4(w, header + 0);

    for (int i = 0; i < 8; i++) {
        header_word_rev4(job->prevhash + 4 * i, header + 4 + 4 * i);
    }

    memcpy(header + 36, merkle_root, 32);

    write_le32(w, job->ntime);
    header_word_rev4(w, header + 68);

    write_le32(w, job->nbits);
    header_word_rev4(w, header + 72);

    write_le32(w, nonce);
    header_word_rev4(w, header + 76);
}

// =========================
// Stratum クライアント
// =========================
static WiFiClient g_client;
static uint32_t g_rpc_id = 1;
static char g_line_buf[16384];
static size_t g_line_len = 0;

static uint64_t g_hashes_done = 0;
static uint64_t g_hash_window_start_ms = 0;
static uint32_t g_accepted = 0;
static uint32_t g_rejected = 0;

static bool g_ntp_sync_announced = false;

static void try_announce_ntp_sync() {
    if (g_ntp_sync_announced) return;
    time_t t = time(nullptr);
    if (t < 1000000000L) return;
    struct tm ti;
    localtime_r(&t, &ti);
    Serial.println();
    Serial.printf("[NTP] 同期完了 — 現在日時: %04d-%02d-%02d %02d:%02d:%02d\n",
                  ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                  ti.tm_hour, ti.tm_min, ti.tm_sec);
    g_ntp_sync_announced = true;
}

static void log_line(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    struct tm ti;
    time_t now = time(nullptr);
    if (now > 100000) {
        localtime_r(&now, &ti);
        Serial.printf("[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
                      ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                      ti.tm_hour, ti.tm_min, ti.tm_sec, buf);
    } else {
        Serial.printf("[%10lu s] %s\n", (unsigned long)(millis() / 1000), buf);
    }
}

static bool send_stratum_json(const char *json_line) {
    if (!g_client.connected()) return false;
    g_client.println(json_line);
    return true;
}

static bool stratum_subscribe() {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"id\":%lu,\"method\":\"mining.subscribe\",\"params\":[\"%s\"]}",
             (unsigned long)g_rpc_id++, MINER_NAME);
    return send_stratum_json(buf);
}

static bool stratum_authorize() {
    char buf[384];
    snprintf(buf, sizeof(buf),
             "{\"id\":%lu,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}",
             (unsigned long)g_rpc_id++, POOL_USER, POOL_PASS);
    return send_stratum_json(buf);
}

static bool stratum_submit(const char *job_id, const char *extranonce2_hex,
                           const char *ntime_hex, const char *nonce_hex) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"id\":%lu,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}",
             (unsigned long)g_rpc_id++, POOL_USER, job_id, extranonce2_hex, ntime_hex, nonce_hex);
    return send_stratum_json(buf);
}

static void parse_mining_notify(JsonArray params) {
    if (params.size() < 8) {
        log_line("mining.notify: params too short");
        return;
    }

    MiningJob j;
    memset(&j, 0, sizeof(j));

    {
        const char *jid = params[0].as<const char *>();
        snprintf(j.job_id, sizeof(j.job_id), "%s", jid ? jid : "");
    }

    const char *prev_hex = params[1].as<const char *>();
    if (!prev_hex || strlen(prev_hex) != 64 || !hex_decode(prev_hex, j.prevhash, 32)) {
        log_line("mining.notify: bad prevhash");
        return;
    }

    const char *c1 = params[2].as<const char *>();
    const char *c2 = params[3].as<const char *>();
    if (!c1 || !c2) {
        log_line("mining.notify: missing coinbase");
        return;
    }
    j.coinb1_len = strlen(c1) / 2;
    j.coinb2_len = strlen(c2) / 2;
    if (j.coinb1_len > sizeof(j.coinb1) || j.coinb2_len > sizeof(j.coinb2)) {
        log_line("mining.notify: coinbase too large");
        return;
    }
    if (!hex_decode(c1, j.coinb1, j.coinb1_len) || !hex_decode(c2, j.coinb2, j.coinb2_len)) {
        log_line("mining.notify: coinb hex decode failed");
        return;
    }

    if (!params[4].is<JsonArray>()) {
        log_line("mining.notify: merkle branches missing");
        return;
    }
    JsonArray br = params[4].as<JsonArray>();
    j.n_branches = 0;
    for (JsonVariant v : br) {
        if (j.n_branches >= MAX_MERKLE_BRANCHES) break;
        const char *bh = v.as<const char *>();
        if (!bh || strlen(bh) != 64 || !hex_decode(bh, j.branch_hash[j.n_branches], 32)) {
            log_line("mining.notify: bad branch");
            return;
        }
        j.n_branches++;
    }

    const char *ver_hex = params[5].as<const char *>();
    const char *nbits_hex = params[6].as<const char *>();
    const char *ntime_hex = params[7].as<const char *>();
    if (!ver_hex || !nbits_hex || !ntime_hex) {
        log_line("mining.notify: bad header fields");
        return;
    }
    uint8_t tmp[4];
    if (strlen(ver_hex) != 8 || !hex_decode(ver_hex, tmp, 4)) {
        log_line("mining.notify: bad version");
        return;
    }
    j.version = read_le32(tmp);
    if (strlen(nbits_hex) != 8 || !hex_decode(nbits_hex, tmp, 4)) {
        log_line("mining.notify: bad nbits");
        return;
    }
    j.nbits = read_le32(tmp);
    if (strlen(ntime_hex) != 8 || !hex_decode(ntime_hex, tmp, 4)) {
        log_line("mining.notify: bad ntime");
        return;
    }
    j.ntime = read_le32(tmp);

    j.clean_jobs = (params.size() > 8) ? params[8].as<bool>() : false;
    j.valid = true;

    if (xSemaphoreTake(g_job_mutex, portMAX_DELAY) == pdTRUE) {
        if (j.clean_jobs) {
            g_stop_mining = true;
        }
        g_job = j;
        g_job_updated = true;
        xSemaphoreGive(g_job_mutex);
    }

    log_line("Stratum new job: job_id=%s clean_jobs=%s", j.job_id, j.clean_jobs ? "yes" : "no");
}

static void parse_set_difficulty(JsonArray params) {
    if (params.size() < 1) return;
    double d = params[0].as<double>();
    g_difficulty = d > 0 ? d : 1.0;
    log_line("Stratum difficulty set to %g", g_difficulty);
}

static void parse_subscribe_result(JsonArray arr) {
    if (arr.size() < 3) return;
    const char *en1 = arr[1].as<const char *>();
    int en2s = arr[2].as<int>();
    if (en1 && en2s > 0 && en2s <= 8) {
        snprintf(g_extranonce1_hex, sizeof(g_extranonce1_hex), "%s", en1);
        g_extranonce2_size = en2s;
        log_line("Subscribed: extranonce1=%s extranonce2_size=%d", g_extranonce1_hex, g_extranonce2_size);
    }
}

static void handle_stratum_message(JsonDocument& doc) {
    if (!doc["method"].isNull()) {
        const char *method = doc["method"].as<const char *>();
        if (!method) return;
        if (!doc["params"].is<JsonArray>()) return;
        JsonArray params = doc["params"].as<JsonArray>();
        if (strcmp(method, "mining.notify") == 0) {
            parse_mining_notify(params);
        } else if (strcmp(method, "mining.set_difficulty") == 0) {
            parse_set_difficulty(params);
        }
        return;
    }

    if (doc["id"].isNull()) return;

    uint32_t rid = doc["id"].as<uint32_t>();

    if (!doc["error"].isNull()) {
        if (rid > 2) {
            g_rejected++;
        }
        char errdetail[256];
        errdetail[0] = '\0';
        serializeJson(doc["error"], errdetail, sizeof(errdetail));
        log_line("rpc error id=%lu: %s", (unsigned long)rid, errdetail);
        return;
    }

    JsonVariant res = doc["result"];
    if (res.isNull()) return;

    if (rid == 1 && res.is<JsonArray>()) {
        parse_subscribe_result(res.as<JsonArray>());
        return;
    }

    if (rid == 2) {
        if (res.is<bool>() && res.as<bool>()) {
            log_line("Authorized");
        }
        return;
    }

    if (rid > 2) {
        if (res.is<bool>() && res.as<bool>()) {
            g_accepted++;
            uint32_t tot = g_accepted + g_rejected;
            double pct = tot ? (100.0 * (double)g_accepted / (double)tot) : 0.0;
            log_line("accepted: %u/%u (%.1f%%)", g_accepted, tot, pct);
        } else {
            g_rejected++;
            log_line("rejected (result false) id=%lu", (unsigned long)rid);
        }
    }
}

static void drain_client() {
    while (g_client.available()) {
        char c = (char)g_client.read();
        if (c == '\n') {
            g_line_buf[g_line_len] = '\0';
            if (g_line_len > 0) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, g_line_buf);
                if (err) {
                    Serial.printf("JSON parse error: %s\n", err.c_str());
                } else if (doc.overflowed()) {
                    Serial.println("JSON parse: document overflowed (notify が大きすぎます)");
                } else {
                    handle_stratum_message(doc);
                }
            }
            g_line_len = 0;
        } else if (g_line_len + 1 < sizeof(g_line_buf)) {
            g_line_buf[g_line_len++] = c;
        } else {
            g_line_len = 0;
        }
    }
}

// =========================
// マイニング（1コア）
// =========================
static void mining_task(void *arg) {
    (void)arg;

    uint8_t header[80];
    uint8_t hash[32];
    uint32_t target[8];
    uint32_t hash_u32[8];
    uint8_t merkle[32];
    uint8_t coinbase[512];
    char extranonce2_hex[32];
    char ntime_hex[16];

    uint64_t extranonce2_counter = 0;

    log_line("1 miner thread started, using 'sha256d' algorithm.");

    for (;;) {
        if (!g_client.connected()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (xSemaphoreTake(g_job_mutex, portMAX_DELAY) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        MiningJob job = g_job;
        bool updated = g_job_updated;
        g_job_updated = false;
        g_stop_mining = false;
        xSemaphoreGive(g_job_mutex);

        if (!job.valid) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (strlen(g_extranonce1_hex) == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (updated) {
            extranonce2_counter = 0;
        }

        size_t en1_bytes = strlen(g_extranonce1_hex) / 2;
        uint8_t en1_bin[32];
        if (en1_bytes > sizeof(en1_bin) || !hex_decode(g_extranonce1_hex, en1_bin, en1_bytes)) {
            log_line("Bad extranonce1");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        diff_to_target(target, g_difficulty);

        uint32_t ntime_le = job.ntime;

        for (;;) {
            if (!g_client.connected()) break;

            if (xSemaphoreTake(g_job_mutex, pdMS_TO_TICKS(0)) == pdTRUE) {
                bool stop = g_stop_mining;
                bool upd = g_job_updated;
                xSemaphoreGive(g_job_mutex);
                if (stop || upd) break;
            }

            memset(extranonce2_hex, 0, sizeof(extranonce2_hex));
            {
                uint8_t en2[8];
                memset(en2, 0, sizeof(en2));
                for (int i = 0; i < g_extranonce2_size && i < 8; i++) {
                    en2[i] = (uint8_t)((extranonce2_counter >> (8 * i)) & 0xFF);
                }
                hex_encode(en2, (size_t)g_extranonce2_size, extranonce2_hex);
            }

            size_t cb_len = job.coinb1_len + en1_bytes + (size_t)g_extranonce2_size + job.coinb2_len;
            if (cb_len > sizeof(coinbase)) {
                log_line("Coinbase buffer overflow");
                break;
            }

            memcpy(coinbase, job.coinb1, job.coinb1_len);
            memcpy(coinbase + job.coinb1_len, en1_bin, en1_bytes);
            {
                uint8_t en2b[8];
                memset(en2b, 0, sizeof(en2b));
                for (int i = 0; i < g_extranonce2_size && i < 8; i++) {
                    en2b[i] = (uint8_t)((extranonce2_counter >> (8 * i)) & 0xFF);
                }
                memcpy(coinbase + job.coinb1_len + en1_bytes, en2b, (size_t)g_extranonce2_size);
            }
            memcpy(coinbase + job.coinb1_len + en1_bytes + (size_t)g_extranonce2_size, job.coinb2, job.coinb2_len);

            build_merkle_root(&job, coinbase, cb_len, merkle);

            {
                uint8_t le4[4];
                write_le32(le4, ntime_le);
                hex_encode(le4, 4, ntime_hex);
            }

            for (uint32_t nonce = 0; ; nonce++) {
                uint8_t nonce_le_bytes[4];
                char nonce_hex_local[16];
#ifdef ESP32_MINER_PROFILE_NONCE_LOOP
                uint32_t prof_t;
                uint32_t prof_t1;
                prof_t = micros();
#endif
                write_le32(nonce_le_bytes, nonce);
                hex_encode(nonce_le_bytes, 4, nonce_hex_local);
#ifdef ESP32_MINER_PROFILE_NONCE_LOOP
                prof_t1 = micros();
                g_prof_sum_nonce_hex_us += (uint64_t)(prof_t1 - prof_t);
                prof_t = prof_t1;
#endif
                if ((nonce % NONCE_BATCH) == 0) {
                    if (!g_client.connected()) goto reconnect;

                    if (xSemaphoreTake(g_job_mutex, pdMS_TO_TICKS(0)) == pdTRUE) {
                        bool stop = g_stop_mining;
                        bool upd = g_job_updated;
                        xSemaphoreGive(g_job_mutex);
                        if (stop || upd) goto next_job;
                    }

                    uint64_t now_ms = millis();
                    if (g_hash_window_start_ms == 0) g_hash_window_start_ms = now_ms;
                    if (now_ms - g_hash_window_start_ms >= 5000) {
                        double secs = (now_ms - g_hash_window_start_ms) / 1000.0;
                        double kh = (g_hashes_done / 1000.0) / secs;
                        uint32_t tot = g_accepted + g_rejected;
                        double pct = tot ? (100.0 * (double)g_accepted / (double)tot) : 0.0;
                        log_line("%.2f kH/s, accepted: %u/%u (%.1f%%)", kh, g_accepted, tot, pct);
#ifdef ESP32_MINER_PROFILE_NONCE_LOOP
                        if (g_prof_sample_count > 0) {
                            double n = (double)g_prof_sample_count;
                            log_line(
                                "[prof] samples=%lu avg_us: nonce_hex=%.2f build_hdr=%.2f sha256d=%.2f "
                                "hash+fulltest=%.2f total=%.2f",
                                (unsigned long)g_prof_sample_count,
                                (double)g_prof_sum_nonce_hex_us / n, (double)g_prof_sum_build_header_us / n,
                                (double)g_prof_sum_sha256d_us / n, (double)g_prof_sum_hash_fulltest_us / n,
                                (double)(g_prof_sum_nonce_hex_us + g_prof_sum_build_header_us +
                                         g_prof_sum_sha256d_us + g_prof_sum_hash_fulltest_us) /
                                    n);
                            g_prof_sum_nonce_hex_us     = 0;
                            g_prof_sum_build_header_us    = 0;
                            g_prof_sum_sha256d_us         = 0;
                            g_prof_sum_hash_fulltest_us   = 0;
                            g_prof_sample_count           = 0;
                        }
#endif
                        g_hashes_done = 0;
                        g_hash_window_start_ms = now_ms;
                    }
                }

                build_block_header(&job, header, merkle, nonce);
#ifdef ESP32_MINER_PROFILE_NONCE_LOOP
                prof_t1 = micros();
                g_prof_sum_build_header_us += (uint64_t)(prof_t1 - prof_t);
                prof_t = prof_t1;
#endif
                g_sha256d(header, hash);
#ifdef ESP32_MINER_PROFILE_NONCE_LOOP
                prof_t1 = micros();
                g_prof_sum_sha256d_us += (uint64_t)(prof_t1 - prof_t);
                prof_t = prof_t1;
#endif
                g_hashes_done++;
                g_sink ^= hash[0];

                hash_bytes_to_cpuminer_u32(hash, hash_u32);
                if (fulltest(hash_u32, target)) {
                    log_line("found share: job=%s nonce=%s ntime=%s extranonce2=%s", job.job_id, nonce_hex_local,
                             ntime_hex, extranonce2_hex);
                    stratum_submit(job.job_id, extranonce2_hex, ntime_hex, nonce_hex_local);
                }
#ifdef ESP32_MINER_PROFILE_NONCE_LOOP
                prof_t1 = micros();
                g_prof_sum_hash_fulltest_us += (uint64_t)(prof_t1 - prof_t);
                g_prof_sample_count++;
#endif

                if (nonce == 0xFFFFFFFFu) break;
            }

            extranonce2_counter++;
            if (extranonce2_counter == 0) {
                log_line("extranonce2 space exhausted, waiting for new job");
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            }
        }

    next_job:
        continue;

    reconnect:
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void connect_pool() {
    log_line("Connecting to stratum+tcp://%s:%u ...", POOL_HOST, (unsigned)POOL_PORT);
    g_client.stop();

    if (xSemaphoreTake(g_job_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        memset(&g_job, 0, sizeof(g_job));
        g_extranonce1_hex[0] = '\0';
        g_extranonce2_size = 4;
        g_job_updated = true;
        xSemaphoreGive(g_job_mutex);
    }

    if (!g_client.connect(POOL_HOST, POOL_PORT)) {
        log_line("Pool connection failed, retry in 5s");
        delay(5000);
        return;
    }
    log_line("Stratum from pool: stratum+tcp://%s:%u", POOL_HOST, (unsigned)POOL_PORT);

    g_rpc_id = 1;
    stratum_subscribe();
    delay(100);
    drain_client();
    stratum_authorize();
    delay(100);
    drain_client();
}

void setup() {
    Serial.begin(115200);
    delay(800);

    init_sha256d_backend();

    Serial.println();
    Serial.println("** esp32-miner (CoreS3) **");
    Serial.printf("** algorithm: sha256d (%s) **\n", (USE_MBEDTLS_SHA256 != 0) ? "mbedtls" : "software");
    Serial.println();

    g_job_mutex = xSemaphoreCreateMutex();
    memset(&g_job, 0, sizeof(g_job));

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    log_line("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    log_line("WiFi connected, IP %s", WiFi.localIP().toString().c_str());

    configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    xTaskCreatePinnedToCore(mining_task, "mining", 12288, nullptr, 1, nullptr, MINING_CORE);

    connect_pool();
}

void loop() {
    try_announce_ntp_sync();

    if (!WiFi.isConnected()) {
        log_line("WiFi lost, reconnecting...");
        WiFi.reconnect();
        delay(2000);
        return;
    }

    static bool stratum_was_connected = false;
    const bool pool_connected = g_client.connected();
    if (stratum_was_connected && !pool_connected) {
        log_line("Stratum TCP lost: pool client connected() is false, reconnecting...");
    }
    stratum_was_connected = pool_connected;

    if (!pool_connected) {
        connect_pool();
        return;
    }

    drain_client();
    delay(5);
}
