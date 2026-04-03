/*
    Bitcoin Mining Controler
    for M5Stack ATOMS3 (ESP32-S3)

    Copyright (c) 2026 Kaz  (https://akibabara.com/blog/)
    Released under the MIT license.
    see https://opensource.org/licenses/MIT

    接続方法
    ・GPIO 5: SDA
    ・GPIO 6: SCL

    処理の流れ
    [master] I2Cスキャンして接続されているスレーブ(CH32V003)を全て見つける
    [master] マイニングプールに接続する
    [master] 仕事が来たら各スレーブに命令を出す
　      [slave] スレーブはハッシュを計算
　      [slave] ヒットしたらステータスフラグを立てる
    [master] 一定間隔(50ms)でスレーブにアクセスしてステータスを取得
    [master] ヒットしたら値を取得して組み立てて、マイニングプールに報告
    [master] 以降、繰り返し
*/

#include <Arduino.h>
#include <M5Unified.h>
#include <atomic>
#include <stdarg.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>

// ================
// 基本設定
// ================
static const char *WIFI_SSID = "****";    // WiFi SSID
static const char *WIFI_PASS = "****";   // WiFi Password
static const int I2C_SDA_PIN = 5;   // I2C SDA ピン
static const int I2C_SCL_PIN = 6;   // I2C SCL ピン

// ================
// プール設定
// ================

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

// ================
// その他設定（通常変更する必要なし）
// ================

// マイナー名
static const char *MINER_NAME = "esp32-miner/1.0";

// ワーカーコーディネータを動かすコア (0 または 1)
static const BaseType_t MINING_CORE = 1;

// I2C設定 外部 CH32 スレーブ用
static const uint32_t I2C_FREQ_HZ = 100000;

// 許容するスレーブの台数
static const int MIN_CH32_SLAVES = 1;   // min
static const int MAX_CH32_SLAVES = 100;   // max

// デバッグ設定 1=ポーリングで各CH32から読んだステータスを1秒ごとにログに出力
#define MASTER_DEBUG_CH32_I2C_READ 0

// CH32 ステータス: 通常 1B（st のみ）。ST_FOUND のときだけ 15B 全体を返す（nonce+hash8+sha256d 回数）
static const int CH32_STATUS_READ_MAX = 15;

// mining_task 内で各 CH32 のステータスを読むループの待ち時間(ms)
static const uint32_t CH32_STATUS_POLL_MS = 50; // ms 

// 統計情報の出力間隔
static const uint32_t CH32_HIT_LOG_INTERVAL_MS = 5u * 60u * 1000u;  // 5 min

// ================
// 定数
// ================

// I2Cコマンド
static const uint8_t OP_PING = 0x00;
static const uint8_t OP_RESET = 0x01;
static const uint8_t OP_HDR_FRAG = 0x10;
static const uint8_t OP_SET_TARGET = 0x11;      // 続けて target[0..3] を 16B LE
static const uint8_t OP_SET_TARGET_HI = 0x18;   // 続けて target[4..7] を 16B LE（CH32 は fulltest 用）
static const uint8_t OP_SET_NONCE0 = 0x12;
static const uint8_t OP_START = 0x20;
static const uint8_t OP_ABORT = 0x21;
static const uint8_t OP_READ_HASHRATE = 0x22;

// I2Cステータス
static const uint8_t ST_FOUND = 2;

// ================
// グローバル変数ほか
// ================
static uint8_t g_slave_addr[MAX_CH32_SLAVES];
static int g_slave_count = 0;
static double g_ch32_last_hs[MAX_CH32_SLAVES];
static uint64_t g_ch32_avg_hashes[MAX_CH32_SLAVES];
static uint64_t g_ch32_avg_dt_ms[MAX_CH32_SLAVES];
static uint32_t g_ch32_dbg_last_ms = 0;

// デバッグ用: ST_FOUND を CH32 が返した回数（mining_task が更新）。hit は BtnA / 定期ログ後に 0 クリア、hit_total は累計
static std::atomic<uint32_t> g_ch32_hit[MAX_CH32_SLAVES] = {};
static std::atomic<uint32_t> g_ch32_hit_total[MAX_CH32_SLAVES] = {};

// デバッグ用: ch32_miner_slave と同じ値（ログの ST_xxxx 表示用）
static const uint8_t ST_IDLE = 0;
static const uint8_t ST_SCAN = 1;
static const uint8_t ST_ABORTED = 3;
static const uint8_t ST_ERR = 0xFF;

// =========================
// ジョブ
// =========================
static const int MAX_MERKLE_BRANCHES = 16;

struct MiningJob {
    bool valid;
    char job_id[96];
    uint8_t prevhash[32];
    uint8_t coinb1[256];
    size_t coinb1_len;
    uint8_t coinb2[256];
    size_t coinb2_len;
    uint8_t branch_hash[MAX_MERKLE_BRANCHES][32];
    int n_branches;
    uint32_t version;
    uint32_t nbits;
    uint32_t ntime;
    bool clean_jobs;
};

// デバッグ用: CH32 ステータス値をログ向けの文字列に変換する。
static const char *ch32_status_str(uint8_t st) {
    switch (st) {
    case ST_IDLE: return "ST_IDLE";
    case ST_SCAN: return "ST_SCAN";
    case ST_FOUND: return "ST_FOUND";
    case ST_ABORTED: return "ST_ABORTED";
    case ST_ERR: return "ST_ERR";
    default: {
        static char buf[24];
        snprintf(buf, sizeof(buf), "ST_?(0x%02X)", (unsigned)st);
        return buf;
    }
    }
}

// デバッグ用: CH32 から読んだステータス内容を 1 行のデバッグログに整形する。
static void log_ch32_slave_rx_line(int slave_idx, uint8_t i2c_addr, uint8_t st, uint32_t nonce_le, const uint8_t h8[8],
                                   bool have_full_payload, uint16_t sha256d_since_read) {
    if (have_full_payload) {
        log_line(
            "CH32[%d] 0x%02x  st=%s  nonce=%lu  hash[0..7]=%02x%02x%02x%02x%02x%02x%02x%02x  sha256d=%u",
            slave_idx, (unsigned)i2c_addr, ch32_status_str(st), (unsigned long)nonce_le, h8[0], h8[1], h8[2], h8[3], h8[4],
            h8[5], h8[6], h8[7], (unsigned)sha256d_since_read);
    } else {
        log_line("CH32[%d] 0x%02x  st=%s  (1B)  sha256d=n/a", slave_idx, (unsigned)i2c_addr, ch32_status_str(st));
    }
}

// =========================
// mbedtls: Merkle / 共有検証のみ（採掘ループは CH32）
// =========================
// 80 バイトのブロックヘッダを mbedtls で SHA256d する。
static void sha256d_mbedtls80(const uint8_t *input80, uint8_t out32[32]) {
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

// 64 バイト入力を mbedtls で SHA256d する。Merkle ブランチ計算用。
static void sha256d_64(const uint8_t *data64, uint8_t out32[32]) {
    uint8_t tmp[32];
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

// =========================
// ユーティリティ: hex / endian / uint256
// =========================
// 16 進 1 文字を数値に変換する。無効文字は -1。
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 固定長の hex 文字列をバイト列へデコードする。
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

// バイト列を小文字 hex 文字列へエンコードする。
static void hex_encode(const uint8_t *data, size_t len, char *out_hex) {
    static const char *xd = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out_hex[i * 2]     = xd[data[i] >> 4];
        out_hex[i * 2 + 1] = xd[data[i] & 0x0F];
    }
    out_hex[len * 2] = '\0';
}

// 4 バイト little-endian を uint32_t に読む。
static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

// SHA256 出力の各 4 バイトをビッグエンディアン uint32 として読む（cpuminer の be32dec と同じ）
// 4 バイト big-endian を uint32_t に読む。
static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// uint32_t を 4 バイト little-endian で書く。
static void write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

// cpuminer util.c 相当（pooler/cpuminer stratum + sha256d 共有判定と一致させる）
// uint32_t のバイト順を反転する。
static inline uint32_t swab32(uint32_t v) {
    return ((v & 0xff000000u) >> 24) | ((v & 0x00ff0000u) >> 8) | ((v & 0x0000ff00u) << 8) |
           ((v & 0x000000ffu) << 24);
}

// Stratum difficulty を cpuminer 互換の target[8] に変換する。
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

// cpuminer と同じ比較順で hash <= target を判定する。
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
// sha256d_80_swap (sha2.c) は最終行で hash[i] = swab32(state[i]) を返す。
// 標準 SHA256 の出力は BE(state[i]) なので read_be32→state[i]→swab32 で一致させる。
// 32 バイトのハッシュを cpuminer 形式の uint32[8] に並べ替える。
static void hash_bytes_to_cpuminer_u32(const uint8_t *hash32, uint32_t out[8]) {
    for (int i = 0; i < 8; i++) {
        out[i] = swab32(read_be32(hash32 + i * 4));
    }
}

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
//   version / ntime / nbits: Stratum hex → hex_decode → read_le32 → write_le32 → rev4
//     （hex_decode 結果は BE 表現なのでバイト反転が必要）
//   prevhash: Stratum hex は各 4B ワードがブロックヘッダと逆順 → rev4 で復元
//   merkle_root: SHA256d 生 32B をそのまま memcpy
//   nonce: LE で 76–79（mining.submit の nonce hex と一致）
// 4 バイト単位で並びを反転し、ヘッダに書く 1 ワードを作る。
static inline void header_word_rev4(const uint8_t le_or_naive4[4], uint8_t *dst) {
    dst[0] = le_or_naive4[3];
    dst[1] = le_or_naive4[2];
    dst[2] = le_or_naive4[1];
    dst[3] = le_or_naive4[0];
}

// Coinbase と merkle branch からブロックヘッダ用の merkle root を構築する。
static void build_merkle_root(const MiningJob *job, const uint8_t *coinbase, size_t coinbase_len,
                              uint8_t merkle_root[32]) {
    uint8_t h[32];
    uint8_t buf[64];

    // まず coinbase 自体を SHA256d して葉ハッシュを作る。
    {
        uint8_t t1[32], t2[32];
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
        memcpy(h, t2, 32);
    }

    // Stratum から受け取った branch を順に畳み込んで root まで上げる。
    for (int i = 0; i < job->n_branches; i++) {
        memcpy(buf, h, 32);
        memcpy(buf + 32, job->branch_hash[i], 32);
        sha256d_64(buf, h);
    }
    memcpy(merkle_root, h, 32);
}

// Stratum job と merkle root から CH32 が走査する 80 バイトのヘッダを組み立てる。
static void build_block_header(const MiningJob *job, uint8_t header[80], const uint8_t merkle_root[32],
                               uint32_t nonce) {
    uint8_t w[4];

    // sha256d_80_swap (sha2.c) は swap=0 で W[i]=work->data[i] を直接使う。
    // 標準 SHA256 で同じ W[i] を得るには load_be32(header+4*i) = value が必要。
    // したがって全フィールドを be32enc (= write_le32+rev4) で書く。
    // merkle_root だけは sha256d の BE 出力がそのまま be32enc(state[i]) と一致するため memcpy。

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

    // nonce も同様に BE で書く（cpuminer の W[19]=nonce_value と一致させる）
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

static uint32_t g_accepted = 0;
static uint32_t g_rejected = 0;

static bool g_ntp_sync_announced = false;

static void update_stats_display(double total_hs, int slave_count, uint32_t accepted, double accepted_pct) {
    char hs_line[24];
    char ch32_line[24];
    char accepted_line[24];
    char pct_line[24];

    snprintf(hs_line, sizeof(hs_line), "%.0fH/s", total_hs);
    snprintf(ch32_line, sizeof(ch32_line), "CH32 x%d", slave_count);
    snprintf(accepted_line, sizeof(accepted_line), "%u", accepted);
    snprintf(pct_line, sizeof(pct_line), "%.1f%%", accepted_pct);

    auto &disp = M5.Display;
    disp.startWrite();
    disp.setRotation(0);
    disp.fillScreen(TFT_BLACK);
    disp.setTextColor(TFT_WHITE, TFT_BLACK);
    disp.setTextDatum(middle_center);
    disp.setTextSize(2);

    const int cx = disp.width() / 2;
    const int y0 = 20;
    const int dy = 28;
    disp.drawString(hs_line, cx, y0);
    disp.drawString(ch32_line, cx, y0 + dy);
    disp.drawString(accepted_line, cx, y0 + dy * 2);
    disp.drawString(pct_line, cx, y0 + dy * 3);
    disp.endWrite();
}

// NTP 同期が完了した時刻を一度だけ通知する。
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

// 時刻付きの統一ログを 1 行出力する。
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

// 1 行の Stratum JSON を TCP ソケットへ送る。
static bool send_stratum_json(const char *json_line) {
    if (!g_client.connected()) return false;
    g_client.println(json_line);
    return true;
}

// mining.subscribe を送る。
static bool stratum_subscribe() {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"id\":%lu,\"method\":\"mining.subscribe\",\"params\":[\"%s\"]}",
             (unsigned long)g_rpc_id++, MINER_NAME);
    return send_stratum_json(buf);
}

// mining.authorize を送る。
static bool stratum_authorize() {
    char buf[384];
    snprintf(buf, sizeof(buf),
             "{\"id\":%lu,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}",
             (unsigned long)g_rpc_id++, POOL_USER, POOL_PASS);
    return send_stratum_json(buf);
}

// cpuminer-multi 等が送る Stratum 拡張。未対応プールは error を返すだけ。対応プールでは
// extranonce 更新通知に必要（Invalid share の原因になり得る）。
// mining.extranonce.subscribe を送る。
static bool stratum_extranonce_subscribe() {
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"id\":%lu,\"method\":\"mining.extranonce.subscribe\",\"params\":[]}",
             (unsigned long)g_rpc_id++);
    return send_stratum_json(buf);
}

// 共有候補を mining.submit でプールへ送る。
static bool stratum_submit(const char *job_id, const char *extranonce2_hex,
                           const char *ntime_hex, const char *nonce_hex) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"id\":%lu,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}",
             (unsigned long)g_rpc_id++, POOL_USER, job_id, extranonce2_hex, ntime_hex, nonce_hex);
    return send_stratum_json(buf);
}

// mining.notify を MiningJob 構造体へ展開し、最新ジョブとして反映する。
static void parse_mining_notify(JsonArray params) {
    if (params.size() < 8) {
        log_line("mining.notify: params too short");
        return;
    }

    MiningJob j;
    memset(&j, 0, sizeof(j));

    // job_id を取り出す。
    {
        const char *jid = params[0].as<const char *>();
        snprintf(j.job_id, sizeof(j.job_id), "%s", jid ? jid : "");
    }

    // prevhash / coinbase / merkle branch をデコードする。
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

    // ヘッダの version / nbits / ntime を数値化する。
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

    // clean_jobs の指示も含めて最新ジョブへ差し替える。
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

// ログ用（Stratum difficulty 等）: 指数表記を避け、小数点以下の末尾 0 を削る
// 不要な末尾 0 を省いた小数文字列を作る。
static void format_double_decimal_str(char *buf, size_t cap, double v) {
    snprintf(buf, cap, "%.15f", v);
    size_t n = strlen(buf);
    while (n > 0 && buf[n - 1] == '0') {
        n--;
    }
    if (n > 0 && buf[n - 1] == '.') {
        n--;
    }
    buf[n] = '\0';
}

// mining.set_difficulty を反映する。
static void parse_set_difficulty(JsonArray params) {
    if (params.size() < 1) return;
    double d = params[0].as<double>();
    g_difficulty = d > 0 ? d : 1.0;
    char dbuf[48];
    format_double_decimal_str(dbuf, sizeof(dbuf), g_difficulty);
    log_line("Stratum difficulty set to %s", dbuf);
}

// extranonce.subscribe 後にプールが送る。extranonce1 が変わるとコインベースが変わるため、
// ここを無視すると採掘ヘッダと submit が一致せず Invalid share になる。
// mining.set_extranonce を反映し、以後の coinbase 生成に使う値を更新する。
static void parse_set_extranonce(JsonArray params) {
    if (params.size() < 1) return;
    const char *en1 = params[0].as<const char *>();
    if (!en1) return;
    size_t n = strlen(en1);
    if (n < 2 || (n % 2) != 0 || n >= sizeof(g_extranonce1_hex)) return;
    {
        uint8_t tmp[32];
        if (n / 2 > sizeof(tmp) || !hex_decode(en1, tmp, n / 2)) {
            log_line("mining.set_extranonce: bad extranonce1 hex");
            return;
        }
    }
    int en2s = g_extranonce2_size;
    if (params.size() >= 2) {
        int v = params[1].as<int>();
        if (v > 0 && v <= 8) {
            en2s = v;
        }
    }
    if (xSemaphoreTake(g_job_mutex, portMAX_DELAY) == pdTRUE) {
        snprintf(g_extranonce1_hex, sizeof(g_extranonce1_hex), "%s", en1);
        g_extranonce2_size = en2s;
        g_job_updated = true;
        xSemaphoreGive(g_job_mutex);
    }
    log_line("Stratum set_extranonce: extranonce1=%s extranonce2_size=%d", g_extranonce1_hex, g_extranonce2_size);
}

// mining.subscribe 応答から extranonce1 / extranonce2_size を取り出す。
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

// 受信した Stratum JSON 1 件をメソッド種別ごとに処理する。
static void handle_stratum_message(JsonDocument& doc) {
    // サーバ通知: mining.notify / set_difficulty / set_extranonce。
    if (!doc["method"].isNull()) {
        const char *method = doc["method"].as<const char *>();
        if (!method) return;
        if (!doc["params"].is<JsonArray>()) return;
        JsonArray params = doc["params"].as<JsonArray>();
        if (strcmp(method, "mining.notify") == 0) {
            parse_mining_notify(params);
        } else if (strcmp(method, "mining.set_difficulty") == 0) {
            parse_set_difficulty(params);
        } else if (strcmp(method, "mining.set_extranonce") == 0) {
            parse_set_extranonce(params);
        }
        return;
    }

    // ここから下は RPC 応答。
    if (doc["id"].isNull()) return;

    uint32_t rid = doc["id"].as<uint32_t>();

    // submit 系の error は reject として数える。
    if (!doc["error"].isNull()) {
        // id 3 = mining.extranonce.subscribe の応答（シェアではない）
        if (rid > 3) {
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

    // subscribe / authorize / extranonce.subscribe の初期ハンドシェイクを処理する。
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

    if (rid == 3) {
        if (res.is<bool>() && res.as<bool>()) {
            log_line("Stratum extranonce.subscribe ok");
        }
        return;
    }

    // id>3 は share submit の応答として accepted / rejected を更新する。
    if (rid > 3) {
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

// TCP バッファを読み切り、改行区切りの Stratum JSON を順に処理する。
static void drain_client() {
    while (g_client.available()) {
        char c = (char)g_client.read();
        if (c == '\n') {
            // 1 行そろったら JSON として解釈する。
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
            // まだ 1 行の途中なのでバッファへ追記する。
            g_line_buf[g_line_len++] = c;
        } else {
            // 想定より長い行は破棄して次の改行から再同期する。
            g_line_len = 0;
        }
    }
}

// =========================
// Wire (I2C_NUM_0) ↔ CH32 スレーブ
// =========================

// have_full_payload: 15B フレームを受信した（ST_FOUND または旧互換）。1B のみのときは false（H/s 用カウントなし）
// CH32 の状態を読む。ST_FOUND のときだけ nonce / hash / カウンタも受け取る。
static bool slave_read_status(uint8_t addr, uint8_t *st, uint32_t *nonce_le, uint8_t hash8[8],
                              uint16_t *sha256d_since_read, bool *have_full_payload) {
    int n = Wire.requestFrom((int)addr, CH32_STATUS_READ_MAX);
    if (n < 1) {
        return false;
    }
    *st = (uint8_t)Wire.read();
    // ST_FOUND は拡張フレームなので残り 14 バイトも読む。
    if (*st == ST_FOUND) {
        if (n < CH32_STATUS_READ_MAX) {
            return false;
        }
        uint8_t buf[CH32_STATUS_READ_MAX];
        buf[0] = *st;
        for (int i = 1; i < CH32_STATUS_READ_MAX; i++) {
            buf[i] = (uint8_t)Wire.read();
        }
        *nonce_le = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 24);
        memcpy(hash8, buf + 5, 8);
        uint16_t hc = (uint16_t)buf[13] | ((uint16_t)buf[14] << 8);
        if (sha256d_since_read) {
            *sha256d_since_read = hc;
        }
        if (have_full_payload) {
            *have_full_payload = true;
        }
        return true;
    }

    *nonce_le = 0;
    memset(hash8, 0, 8);
    if (sha256d_since_read) {
        *sha256d_since_read = 0;
    }
    if (have_full_payload) {
        *have_full_payload = false;
    }
    return true;
}

// OP_READ_HASHRATE → requestFrom(2)。スレーブは前回 HR 応答以降の sha256d 回数を返す
// CH32 の直近サンプル分の sha256d 回数を読み、H/s 計算用に返す。
static bool slave_read_hashrate_count(uint8_t addr, uint16_t *out_count) {
    Wire.beginTransmission((int)addr);
    Wire.write(OP_READ_HASHRATE);
    if (Wire.endTransmission() != 0) {
        return false;
    }
    int n = Wire.requestFrom((int)addr, 2);
    if (n != 2) {
        return false;
    }
    uint8_t b0 = (uint8_t)Wire.read();
    uint8_t b1 = (uint8_t)Wire.read();
    *out_count = (uint16_t)b0 | ((uint16_t)b1 << 8);
    return true;
}

// 直近 1 秒サンプルから各 CH32 の H/s を合計する。
static double ch32_total_hs_last_sample(void) {
    double s = 0.0;
    for (int i = 0; i < g_slave_count; i++) {
        s += g_ch32_last_hs[i];
    }
    return s;
}

// 前回の 5 分統計以降に観測した平均 H/s をスレーブ単位で返す。
static double ch32_avg_hs_since_last_report(int slave_idx) {
    if (slave_idx < 0 || slave_idx >= g_slave_count) {
        return 0.0;
    }
    uint64_t dt_ms = g_ch32_avg_dt_ms[slave_idx];
    if (dt_ms == 0u) {
        return 0.0;
    }
    return (double)g_ch32_avg_hashes[slave_idx] * 1000.0 / (double)dt_ms;
}

// CH32 へ PING を送り、応答が返るかで生存確認する。
static bool slave_ping(uint8_t addr) {
    Wire.beginTransmission((int)addr);
    Wire.write(OP_PING);
    if (Wire.endTransmission() != 0) {
        return false;
    }
    uint8_t st;
    uint32_t nv;
    uint8_t h8[8];
    uint16_t dummy_hc;
    bool dummy_full;
    return slave_read_status(addr, &st, &nv, h8, &dummy_hc, &dummy_full);
}

// I2C バスを走査し、応答した CH32 ワーカー一覧を更新する。
static void scan_ch32_slaves(bool verbose) {
    // 検出結果と H/s 集計はスキャンし直し時にクリアする。
    g_slave_count = 0;
    memset(g_ch32_last_hs, 0, sizeof(g_ch32_last_hs));
    memset(g_ch32_avg_hashes, 0, sizeof(g_ch32_avg_hashes));
    memset(g_ch32_avg_dt_ms, 0, sizeof(g_ch32_avg_dt_ms));
    for (uint8_t a = 8; a < 120 && g_slave_count < MAX_CH32_SLAVES; a++) {
        Wire.beginTransmission((int)a);
        if (Wire.endTransmission() != 0) {
            continue;
        }
        if (!slave_ping(a)) {
            continue;
        }
        g_slave_addr[g_slave_count++] = a;
        if (verbose) {
            log_line("CH32 worker at 0x%02x (PING ok)", (unsigned)a);
        }
    }
    if (verbose) {
        log_line("I2C scan: %d CH32 worker(s)", g_slave_count);
    }
}

// 起動時に必要台数の CH32 が見つかるまで待機する。
static void wait_for_ch32_slaves_at_boot(void) {
    log_line("I2C scan: CH32 が %d 台以上見つかるまで待機します (GPIO %d/%d)", MIN_CH32_SLAVES, I2C_SDA_PIN, I2C_SCL_PIN);
    for (;;) {
        scan_ch32_slaves(false);
        if (g_slave_count > 0) {
            log_line("CH32 ワーカー %d 台を検出:", g_slave_count);
            for (int i = 0; i < g_slave_count; i++) {
                log_line("  [%d] I2C アドレス 0x%02x", i, (unsigned)g_slave_addr[i]);
            }
            if (g_slave_count >= MIN_CH32_SLAVES) return;
        }
        log_line("スレーブなし — 2 秒後に再スキャンします");
        delay(2000);
    }
}

// 指定 CH32 に現在の探索を中止させる。
static void slave_abort(uint8_t addr) {
    Wire.beginTransmission((int)addr);
    Wire.write(OP_ABORT);
    Wire.endTransmission();
}

// 80 バイトのヘッダを分割して CH32 へ送る。
static void slave_send_header(uint8_t addr, const uint8_t *h80) {
    size_t off = 0;
    while (off < 80) {
        size_t chunk = 80 - off;
        if (chunk > 28) {
            chunk = 28;
        }
        Wire.beginTransmission((int)addr);
        Wire.write(OP_HDR_FRAG);
        Wire.write((uint8_t)off);
        Wire.write(h80 + off, chunk);
        Wire.endTransmission();
        off += chunk;
    }
}

// target[4] を little-endian の生バイト列で CH32 へ送る。
static void slave_write_target_words(uint8_t addr, uint8_t op, const uint32_t *words4) {
    Wire.beginTransmission((int)addr);
    Wire.write(op);
    for (int w = 0; w < 4; w++) {
        uint32_t v = words4[w];
        Wire.write((uint8_t)(v & 0xFFu));
        Wire.write((uint8_t)((v >> 8) & 0xFFu));
        Wire.write((uint8_t)((v >> 16) & 0xFFu));
        Wire.write((uint8_t)((v >> 24) & 0xFFu));
    }
    Wire.endTransmission();
}

// cpuminer の target[8] をそのまま送り、CH32 側の判定を fulltest と一致させる（target[7] 単体では偽陽性）
// cpuminer 互換の full target[8] を CH32 へ設定する。
static void slave_set_target_full(uint8_t addr, const uint32_t target[8]) {
    slave_write_target_words(addr, OP_SET_TARGET, target);
    slave_write_target_words(addr, OP_SET_TARGET_HI, target + 4);
}

// CH32 の走査開始 nonce を設定する。
static void slave_set_nonce0(uint8_t addr, uint32_t n0) {
    Wire.beginTransmission((int)addr);
    Wire.write(OP_SET_NONCE0);
    Wire.write((uint8_t)(n0 & 0xFFu));
    Wire.write((uint8_t)((n0 >> 8) & 0xFFu));
    Wire.write((uint8_t)((n0 >> 16) & 0xFFu));
    Wire.write((uint8_t)((n0 >> 24) & 0xFFu));
    Wire.endTransmission();
}

// 指定 CH32 に探索開始を指示する。
static void slave_start(uint8_t addr) {
    Wire.beginTransmission((int)addr);
    Wire.write(OP_START);
    Wire.endTransmission();
}

// =========================
// ワーカー調停（1タスク）
// =========================

// Stratum job を CH32 に配り、候補 share の回収と submit までを担当する。
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
    char nonce_hex_local[16];

    uint64_t en2_base = 0;
    uint64_t stat_window_start_ms = 0;

    log_line("Coordinator task started for ch32_miner2_slave (Wire I2C0 GPIO %d/%d)", I2C_SDA_PIN, I2C_SCL_PIN);

    // 採掘タスクは常駐し、通信状態とジョブ状態を見ながら回り続ける。
    for (;;) {
        // TCP 未接続中は loop 側の reconnect を待つ。
        if (!g_client.connected()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // CH32 が見えていなければ再スキャンする。
        if (g_slave_count <= 0) {
            log_line("No CH32 slaves; rescanning I2C in 5s");
            vTaskDelay(pdMS_TO_TICKS(5000));
            scan_ch32_slaves(true);
            continue;
        }

        // 最新 job を mutex 下でスナップショット取得する。
        if (xSemaphoreTake(g_job_mutex, portMAX_DELAY) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        MiningJob job = g_job;
        bool updated = g_job_updated;
        g_job_updated = false;
        g_stop_mining = false;
        xSemaphoreGive(g_job_mutex);

        // まだ有効 job が届いていない間は待機する。
        if (!job.valid) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // subscribe 応答で extranonce1 が決まるまで待つ。
        if (strlen(g_extranonce1_hex) == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 新 job 開始時は extranonce2 の配布カウンタを先頭へ戻す。
        if (updated) {
            en2_base = 0;
        }

        // extranonce1 と difficulty を採掘用の内部表現へ変換する。
        size_t en1_bytes = strlen(g_extranonce1_hex) / 2;
        uint8_t en1_bin[32];
        if (en1_bytes > sizeof(en1_bin) || !hex_decode(g_extranonce1_hex, en1_bin, en1_bytes)) {
            log_line("Bad extranonce1");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        diff_to_target(target, g_difficulty);
        uint32_t ntime_le = job.ntime;
        {
            uint8_t le4[4];
            write_le32(le4, ntime_le);
            hex_encode(le4, 4, ntime_hex);
        }

        const int N = g_slave_count;

        // 同じ Stratum job に対し、extranonce2 をずらしながらラウンドを回す。
        for (;;) {
            if (!g_client.connected()) {
                break;
            }

            // ジョブ更新チェックはポーリングループ内（while !found_round）だけで行う。
            // ここでチェックすると、頻繁な notify のときにディスパッチに到達しないまま
            // 永久に goto next_job を繰り返し、CH32 が ST_ABORTED のまま止まる。

            uint64_t round_en2[MAX_CH32_SLAVES];

            // 各 CH32 に担当分の extranonce2 と block header を配布する。
            for (int si = 0; si < N; si++) {
                uint64_t en2v = en2_base + (uint64_t)si;
                round_en2[si] = en2v;

                // このスレーブ用の extranonce2 文字列を作る。
                memset(extranonce2_hex, 0, sizeof(extranonce2_hex));
                {
                    uint8_t en2b[8];
                    memset(en2b, 0, sizeof(en2b));
                    for (int j = 0; j < g_extranonce2_size && j < 8; j++) {
                        en2b[j] = (uint8_t)((en2v >> (8 * j)) & 0xFF);
                    }
                    hex_encode(en2b, (size_t)g_extranonce2_size, extranonce2_hex);
                }

                // coinbase を組み立て、そこから merkle root と header を作る。
                size_t cb_len = job.coinb1_len + en1_bytes + (size_t)g_extranonce2_size + job.coinb2_len;
                if (cb_len > sizeof(coinbase)) {
                    log_line("Coinbase buffer overflow");
                    goto next_job;
                }

                memcpy(coinbase, job.coinb1, job.coinb1_len);
                memcpy(coinbase + job.coinb1_len, en1_bin, en1_bytes);
                {
                    uint8_t en2b[8];
                    memset(en2b, 0, sizeof(en2b));
                    for (int j = 0; j < g_extranonce2_size && j < 8; j++) {
                        en2b[j] = (uint8_t)((en2v >> (8 * j)) & 0xFF);
                    }
                    memcpy(coinbase + job.coinb1_len + en1_bytes, en2b, (size_t)g_extranonce2_size);
                }
                memcpy(coinbase + job.coinb1_len + en1_bytes + (size_t)g_extranonce2_size, job.coinb2, job.coinb2_len);

                build_merkle_root(&job, coinbase, cb_len, merkle);
                build_block_header(&job, header, merkle, 0);

                // スレーブへ job を反映して探索を開始する。
                uint8_t addr = g_slave_addr[si];
                slave_abort(addr);
                vTaskDelay(pdMS_TO_TICKS(1));
                slave_send_header(addr, header);
                slave_set_target_full(addr, target);
                slave_set_nonce0(addr, 0);
                slave_start(addr);
            }

            bool found_round = false;
            while (!found_round) {
                if (!g_client.connected()) {
                    goto reconnect;
                }

#if MASTER_DEBUG_CH32_I2C_READ
                const bool ch32_dbg_tick = (millis() - g_ch32_dbg_last_ms >= 1000u);
                if (ch32_dbg_tick) {
                    g_ch32_dbg_last_ms = millis();
                }
#endif
                // ジョブ更新で goto する前に必ず ST_FOUND を処理する。順序が逆だと
                // 新ジョブ配信でスレーブのヘッダが差し替わり、直前のヒットを現在の job で
                // 検証してしまい fulltest が偽不合格になる。
                // CH32 を順にポーリングし、最初に見つかった候補を採用する。
                for (int si = 0; si < N; si++) {
                    uint8_t st;
                    uint32_t nonce_found = 0;
                    uint8_t h8[8];
                    uint16_t hc = 0;
                    bool have_full = false;
                    if (!slave_read_status(g_slave_addr[si], &st, &nonce_found, h8, &hc, &have_full)) {
#if MASTER_DEBUG_CH32_I2C_READ
                        if (ch32_dbg_tick) {
                            log_line("CH32[%d] 0x%02x: status READ failed", si, (unsigned)g_slave_addr[si]);
                        }
#endif
                        continue;
                    }
#if MASTER_DEBUG_CH32_I2C_READ
                    if (ch32_dbg_tick) {
                        log_ch32_slave_rx_line(si, g_slave_addr[si], st, nonce_found, h8, have_full, hc);
                    }
#endif
                    if (st != ST_FOUND) {
                        continue;
                    }

                    // 候補を返したスレーブの hit 統計を更新する。
                    g_ch32_hit[si].fetch_add(1u, std::memory_order_relaxed);
                    g_ch32_hit_total[si].fetch_add(1u, std::memory_order_relaxed);

                    // submit に必要な extranonce2 / nonce を文字列化する。
                    uint64_t en2v = round_en2[si];
                    memset(extranonce2_hex, 0, sizeof(extranonce2_hex));
                    {
                        uint8_t en2b[8];
                        memset(en2b, 0, sizeof(en2b));
                        for (int j = 0; j < g_extranonce2_size && j < 8; j++) {
                            en2b[j] = (uint8_t)((en2v >> (8 * j)) & 0xFF);
                        }
                        hex_encode(en2b, (size_t)g_extranonce2_size, extranonce2_hex);
                    }

                    {
                        uint8_t nonce_le_bytes[4];
                        write_le32(nonce_le_bytes, nonce_found);
                        hex_encode(nonce_le_bytes, 4, nonce_hex_local);
                    }

                    // ESP32 側でも fulltest を再確認して誤検出を防ぐ。
                    {
                        size_t cb_len = job.coinb1_len + en1_bytes + (size_t)g_extranonce2_size + job.coinb2_len;
                        memcpy(coinbase, job.coinb1, job.coinb1_len);
                        memcpy(coinbase + job.coinb1_len, en1_bin, en1_bytes);
                        uint8_t en2b[8];
                        memset(en2b, 0, sizeof(en2b));
                        for (int j = 0; j < g_extranonce2_size && j < 8; j++) {
                            en2b[j] = (uint8_t)((en2v >> (8 * j)) & 0xFF);
                        }
                        memcpy(coinbase + job.coinb1_len + en1_bytes, en2b, (size_t)g_extranonce2_size);
                        memcpy(coinbase + job.coinb1_len + en1_bytes + (size_t)g_extranonce2_size, job.coinb2,
                                job.coinb2_len);
                        build_merkle_root(&job, coinbase, cb_len, merkle);
                        build_block_header(&job, header, merkle, nonce_found);
                    }

                    sha256d_mbedtls80(header, hash);
                    hash_bytes_to_cpuminer_u32(hash, hash_u32);
                    if (fulltest(hash_u32, target)) {
                        log_line("found share: job=%s nonce=%s ntime=%s extranonce2=%s", job.job_id, nonce_hex_local,
                                 ntime_hex, extranonce2_hex);
                        stratum_submit(job.job_id, extranonce2_hex, ntime_hex, nonce_hex_local);
                    } else {
                        log_line("CH32 candidate failed fulltest (ignored)");
                    }

                    // このラウンドは 1 候補見つけたら全 CH32 を止める。
                    for (int i = 0; i < g_slave_count; i++) {
                        slave_abort(g_slave_addr[i]);
                    }

                    // 次ラウンドは次の extranonce2 群へ進める。
                    en2_base += (uint64_t)N;
                    found_round = true;
                    break;
                }

                // notify 処理は ST_FOUND 検査の後（上記コメント）
                if (xSemaphoreTake(g_job_mutex, pdMS_TO_TICKS(0)) == pdTRUE) {
                    bool stop = g_stop_mining;
                    bool upd = g_job_updated;
                    xSemaphoreGive(g_job_mutex);
                    if (stop || upd) {
                        goto next_job;
                    }
                }

                // 1 秒に 1 回、各スレーブの sha256d 回数を OP_READ_HASHRATE で取得して H/s を更新
                // 1 秒ごとに瞬間 H/s を読み、5 分統計用の平均にも積算する。
                {
                    static uint32_t s_ch32_hr_tick_ms = 0;
                    uint32_t now_hr = millis();
                    if (s_ch32_hr_tick_ms == 0u) {
                        s_ch32_hr_tick_ms = now_hr;
                    } else if ((uint32_t)(now_hr - s_ch32_hr_tick_ms) >= 1000u) {
                        uint32_t dt = (uint32_t)(now_hr - s_ch32_hr_tick_ms);
                        s_ch32_hr_tick_ms = now_hr;
                        if (dt > 0u) {
                            for (int si = 0; si < N; si++) {
                                uint16_t c = 0;
                                if (slave_read_hashrate_count(g_slave_addr[si], &c)) {
                                    g_ch32_last_hs[si] = (double)c * 1000.0 / (double)dt;
                                    g_ch32_avg_hashes[si] += (uint64_t)c;
                                    g_ch32_avg_dt_ms[si] += (uint64_t)dt;
                                }
                            }
                        }
                    }
                }

                // 5 秒ごとに全体 H/s と accepted 率を表示する。
                uint64_t now_ms = millis();
                if (stat_window_start_ms == 0) {
                    stat_window_start_ms = now_ms;
                }
                if (now_ms - stat_window_start_ms >= 5000) {
                    double total_hs = ch32_total_hs_last_sample();
                    uint32_t tot = g_accepted + g_rejected;
                    double pct = tot ? (100.0 * (double)g_accepted / (double)tot) : 0.0;
                    log_line("%.0f H/s (CH32 x%d), accepted: %u/%u (%.1f%%)", total_hs,
                             g_slave_count,
                             g_accepted, tot, pct);
                    update_stats_display(total_hs, g_slave_count, g_accepted, pct);
                    stat_window_start_ms = now_ms;
                }

                vTaskDelay(pdMS_TO_TICKS(CH32_STATUS_POLL_MS));
            }
        }

    // 新 job 到着時は外側ループへ戻り、配布をやり直す。
    next_job:
        continue;

    // 接続断時は少し待ってから状態を見直す。
    reconnect:
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// プールへ接続し、Stratum の初期ハンドシェイクをやり直す。
static void connect_pool() {
    log_line("Connecting to stratum+tcp://%s:%u ...", POOL_HOST, (unsigned)POOL_PORT);
    g_client.stop();

    // 古い接続の job / extranonce 状態をいったんクリアする。
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

    // subscribe -> authorize -> extranonce.subscribe の順で初期化する。
    g_rpc_id = 1;
    stratum_subscribe();
    delay(100);
    drain_client();
    stratum_authorize();
    delay(100);
    drain_client();
    stratum_extranonce_subscribe();
    delay(100);
    drain_client();
}

// ================
// セットアップ
// ================
void setup() {
    Serial.begin(115200);
    delay(800);
    {
        // 今回使わないデバイスは無効化して M5 を初期化する。
        auto m5cfg = M5.config();
        m5cfg.internal_imu = false;
        m5cfg.internal_rtc = false;
        M5.begin(m5cfg);
    }
    update_stats_display(0.0, 0, 0, 0.0);
    // M5Unified は Port A(Ex_I2C) に I2C_NUM_0 を GPIO1/2、内部(In_I2C)に I2C_NUM_1 を GPIO38/39 で初期化する。
    // Arduino の Wire1 は I2C_NUM_1 のため、Wire1.begin(5,6) は内部 IMU 等と同一ドライバで競合し、誤スキャンになる。
    // CH32 は Wire(I2C_NUM_0) を使い、先に Ex_I2C を解放して GPIO5/6 に付け替える。
    M5.Ex_I2C.release();
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ_HZ);
    log_line("Wire (I2C0) %lu Hz, SDA/SCL GPIO %d/%d", (unsigned long)I2C_FREQ_HZ, I2C_SDA_PIN, I2C_SCL_PIN);
    wait_for_ch32_slaves_at_boot();

    Serial.println();
    Serial.println("** esp32-miner2 (AtomS3 + CH32 I2C) **");
    Serial.println("** sha256d: CH32 midstate workers / Merkle+verify: mbedtls **");
    Serial.println();

    // Stratum 共有状態の保護用 mutex を用意する。
    g_job_mutex = xSemaphoreCreateMutex();
    memset(&g_job, 0, sizeof(g_job));

    // WiFi 接続が張れるまで待つ。
    WiFi.mode(WIFI_STA);
    // 省電力による瞬断を避けるため、WiFi スリープを無効化する。
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    log_line("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    log_line("WiFi connected, IP %s", WiFi.localIP().toString().c_str());

    // ログ時刻表示用に NTP 同期を開始する。
    configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    // 採掘制御本体は別タスクで動かす。
    xTaskCreatePinnedToCore(mining_task, "mining", 12288, nullptr, 1, nullptr, MINING_CORE);

    // 最後にプール接続を開始する。
    connect_pool();
}

// 5 分窓の CH32 統計を出力し、平均用カウンタをリセットする。
static void log_ch32_hit_report_and_clear(void) {
    log_line("--- CH32 Total Result ---");
    if (g_slave_count <= 0) {
        log_line("no slaves.");
    } else {
        for (int i = 0; i < g_slave_count; i++) {
            uint32_t hit = g_ch32_hit[i].load(std::memory_order_relaxed);
            uint32_t hit_total = g_ch32_hit_total[i].load(std::memory_order_relaxed);
            double avg_hs = ch32_avg_hs_since_last_report(i);
            log_line("CH32[%d] I2C=0x%02x  avg=%.0f H/s  hit=%lu  total=%lu", i, (unsigned)g_slave_addr[i], avg_hs,
                     (unsigned long)hit, (unsigned long)hit_total);
            g_ch32_hit[i].store(0u, std::memory_order_relaxed);
            g_ch32_avg_hashes[i] = 0u;
            g_ch32_avg_dt_ms[i] = 0u;
        }
    }
    log_line("------");
}

// ================
// メインloop。重い採掘処理は mining_task が担当する。
// ================
void loop() {
    M5.update();

    // BtnA または 5 分経過で CH32 ごとの統計を表示する。
    const uint32_t now_ms = millis();
    static uint32_t s_ch32_hit_log_last_ms = now_ms;
    const bool hit_log_by_timer = (uint32_t)(now_ms - s_ch32_hit_log_last_ms) >= CH32_HIT_LOG_INTERVAL_MS;
    if (M5.BtnA.wasPressed() || hit_log_by_timer) {
        log_ch32_hit_report_and_clear();
        s_ch32_hit_log_last_ms = now_ms;
    }

    try_announce_ntp_sync();

    // WiFi 切断時は復旧を試みる。
    if (!WiFi.isConnected()) {
        log_line("WiFi lost, reconnecting...");
        WiFi.reconnect();
        delay(2000);
        return;
    }

    // Stratum TCP の切断を検出したら再接続へ進む。
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

    // 受信済みの Stratum メッセージをこまめに処理する。
    drain_client();
    delay(5);
}
