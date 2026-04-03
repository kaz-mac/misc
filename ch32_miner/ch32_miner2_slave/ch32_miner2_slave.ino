/*
    Bitcoin Mining Node
    for UIAPduino Pro Micro (CH32V003)

    Copyright (c) 2026 Kaz  (https://akibabara.com/blog/)
    Released under the MIT license.
    see https://opensource.org/licenses/MIT

    ＜注意＞
    複数台接続するときは I2C_SLAVE_ADDR のI2Cスレーブアドレスを変えて書き込む

    接続方法 UIAPduino Pro Micro (CH32V003F4U6) + USB (minichlink) の場合
    ・PC0 = LED
    ・PC1 = SDA
    ・PC2 = SCL
    ・PD6 (TX) = UART TX（デバッグ用）

    接続方法 UIAPduinoカスタムバリアント CH32V003J4M6用 + WCH-LinkE の場合
    ・Pin 8: SWIO = WCH-LinkEへ
    ・Pin 3: PA2 = LED
    ・Pin 6: PC1 = SDA
    ・Pin 5: PC2 = SCL
    ・Pin 1: PD6 (TX) = UART TX（デバッグ用）
*/
#include <Arduino.h>
#include <Wire.h>
#include <string.h>

// ================
// 基本設定
// ================
static const uint8_t I2C_SLAVE_ADDR = 0x40; // I2Cスレーブアドレス
#define HARDWARE_TYPE 2     // 1=UIAPduino 2=SOP-8
#define MINER_SLAVE_DEBUG 1 // 1=UARTにデバッグログを出力する

// ================
// 定数
// ================

// I2Cコマンド
static const uint8_t OP_PING = 0x00;            // 疎通確認
static const uint8_t OP_RESET = 0x01;           // 状態・ヒット・カウンタを初期化
static const uint8_t OP_HDR_FRAG = 0x10;        // 80B ブロックヘッダを断片転送
static const uint8_t OP_SET_TARGET = 0x11;      // 難易度 target 下位 4 ワード（LE）
static const uint8_t OP_SET_TARGET_HI = 0x18;   // 難易度 target 上位 4 ワード（LE）
static const uint8_t OP_SET_NONCE0 = 0x12;      // 探索開始 nonce（LE）
static const uint8_t OP_START = 0x20;           // 採掘開始（loop 側で再開）
static const uint8_t OP_ABORT = 0x21;           // 採掘中止
static const uint8_t OP_READ_HASHRATE = 0x22;   // 次の read で H/s 用 2B を返す

// I2Cステータス
static const uint8_t ST_IDLE = 0;       // 待機中
static const uint8_t ST_SCAN = 1;       // スキャン中
static const uint8_t ST_FOUND = 2;      // 発見！
static const uint8_t ST_ABORTED = 3;    // 中断
static const uint8_t ST_ERR = 0xFF;     // エラー

// ハードウェア違い
#if HARDWARE_TYPE == 1
#define LED_BUILTIN PC0   // LEDのGPIO (PC0)
#elif HARDWARE_TYPE == 2
#define LED_BUILTIN PA2   // LEDのGPIO (PA2)
#endif

// ================
// グローバル変数ほか
// ================
static volatile uint8_t g_status = ST_IDLE;
static volatile bool g_abort_scan = false;
static volatile bool g_scan_armed = false;
static volatile bool g_have_new_cmd = false;
static volatile uint8_t g_pending_cmd = 0;

static volatile uint32_t g_sha256d_count = 0;
static volatile uint16_t g_hr_sha256d_count = 0;
static volatile bool g_pending_hr_read = false;

static uint8_t g_header[80];
static uint32_t g_pool_target[8];
static uint32_t g_nonce_start = 0;

static uint32_t g_found_nonce = 0;
static uint8_t g_found_hash[32];

static uint32_t g_midstate[8];
static uint32_t g_block1_template[16];
static bool g_hash_cache_valid = false;

static uint8_t g_tx_buf[16];
static uint8_t g_tx_len = 0;

static bool g_led_mining_visual = false;
static uint8_t g_led_count = 0;


// LED を強制点灯して「採掘中の見た目」を ON にする。
static inline void led_force_on(void) {
    digitalWrite(LED_BUILTIN, HIGH);
    g_led_mining_visual = true;
}

// LED を強制消灯して「採掘中の見た目」を OFF にする。
static inline void led_force_off(void) {
    digitalWrite(LED_BUILTIN, LOW);
    g_led_mining_visual = false;
}

// 前回ヒット情報を消して、未ヒット状態に戻す。
static inline void clear_found_hit(void) {
    g_found_nonce = 0;
    memset(g_found_hash, 0, sizeof(g_found_hash));
}

// SHA-256 の round 定数 K[0..63]。
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

// 4 バイトを big-endian の uint32 として読む。
static inline uint32_t sw_load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static inline uint32_t swab32_u32(uint32_t v) {
    return ((v & 0xff000000u) >> 24) |
           ((v & 0x00ff0000u) >> 8) |
           ((v & 0x0000ff00u) << 8) |
           ((v & 0x000000ffu) << 24);
}

// uint32 を big-endian 4 バイトとして書き出す。
static void sw_store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

// SHA-256 の初期 IV を state に設定する。
static void sha256_init_state(uint32_t state[8]) {
    state[0] = 0x6a09e667UL;
    state[1] = 0xbb67ae85UL;
    state[2] = 0x3c6ef372UL;
    state[3] = 0xa54ff53aUL;
    state[4] = 0x510e527fUL;
    state[5] = 0x9b05688cUL;
    state[6] = 0x1f83d9abUL;
    state[7] = 0x5be0cd19UL;
}

// 16 ワードのメッセージブロックを 1 回だけ圧縮して state を更新する。
static void sha256_transform_words(uint32_t state[8], const uint32_t w0_15[16]) {
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    uint32_t m[64];

    // W[0..15] を受け取り、W[16..63] を展開する。
    for (uint32_t i = 0; i < 16; i++) {
        m[i] = w0_15[i];
    }
    for (uint32_t i = 16; i < 64; i++) {
        m[i] = SW_SIG1(m[i - 2]) + m[i - 7] + SW_SIG0(m[i - 15]) + m[i - 16];
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

    // 64 round の SHA-256 本体。
    for (uint32_t i = 0; i < 64; i++) {
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

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

// 80B ヘッダの固定部分から midstate と後半ブロックの定数部分を再構築する。
static void rebuild_hash_cache(void) {
    uint32_t block0[16];

    // ヘッダ前半 64B を 16 ワードへ変換する。
    for (int i = 0; i < 16; i++) {
        block0[i] = sw_load_be32(&g_header[i * 4]);
    }

    // 1 ブロック目は nonce で変わらないので、ここを 1 回だけ圧縮して midstate を得る。
    sha256_init_state(g_midstate);
    sha256_transform_words(g_midstate, block0);

    // 2 ブロック目は W[3] だけが nonce で変わる。残りは固定値として持っておく。
    g_block1_template[0] = sw_load_be32(&g_header[64]);
    g_block1_template[1] = sw_load_be32(&g_header[68]);
    g_block1_template[2] = sw_load_be32(&g_header[72]);
    g_block1_template[3] = 0u;
    g_block1_template[4] = 0x80000000UL;
    for (int i = 5; i < 15; i++) {
        g_block1_template[i] = 0u;
    }
    g_block1_template[15] = 80u * 8u;

    g_hash_cache_valid = true;
}

// ヘッダ更新後に cache が無効なら、採掘前に作り直す。
static inline void ensure_hash_cache(void) {
    if (!g_hash_cache_valid) {
        rebuild_hash_cache();
    }
}

// 1 つの nonce について、midstate から SHA256d の最終 state を求める。
static void sha256d_midstate_nonce(uint32_t nonce_be, uint32_t out_state[8]) {
    uint32_t state1[8];
    uint32_t block1[16];
    uint32_t block2[16];

    // 1 回目 SHA-256 の 2 ブロック目だけを nonce 差し替えで実行する。
    memcpy(state1, g_midstate, sizeof(state1));
    memcpy(block1, g_block1_template, sizeof(block1));
    block1[3] = nonce_be;
    sha256_transform_words(state1, block1);

    // 2 回目 SHA-256 は 32B ハッシュ + padding の 1 ブロックだけで終わる。
    for (int i = 0; i < 8; i++) {
        block2[i] = state1[i];
    }
    block2[8] = 0x80000000UL;
    for (int i = 9; i < 15; i++) {
        block2[i] = 0u;
    }
    block2[15] = 32u * 8u;

    sha256_init_state(out_state);
    sha256_transform_words(out_state, block2);
}

// 最終 SHA state を cpuminer 互換の語順へ合わせて target と比較する。
static bool hash_state_meets_target_full(const uint32_t hash_state[8], const uint32_t target[8]) {
    for (int i = 7; i >= 0; i--) {
        uint32_t v = swab32_u32(hash_state[i]);
        if (v > target[i]) {
            return false;
        }
        if (v < target[i]) {
            return true;
        }
    }
    return true;
}

// ヒット時だけ、最終 state を通常の 32B ハッシュ表現へ変換して保存する。
static void store_hash_state(uint8_t hash[32], const uint32_t hash_state[8]) {
    for (int i = 0; i < 8; i++) {
        sw_store_be32(hash + i * 4, hash_state[i]);
    }
}

// 1 ハッシュごとの volatile 更新を避けるため、スライス単位でカウンタへ反映する。
static void add_hash_counts(uint32_t add_sha256d, uint16_t add_hr) {
    noInterrupts();

    // 通常ステータス読取用の 32bit カウンタ。
    if (g_sha256d_count > (0xFFFFFFFFu - add_sha256d)) {
        g_sha256d_count = 0xFFFFFFFFu;
    } else {
        g_sha256d_count += add_sha256d;
    }

    // H/s 読取専用の 16bit カウンタ。
    uint32_t hr = (uint32_t)g_hr_sha256d_count + (uint32_t)add_hr;
    if (hr > 65535u) {
        g_hr_sha256d_count = 65535u;
    } else {
        g_hr_sha256d_count = (uint16_t)hr;
    }

    interrupts();
}

static void prepare_status_tx(void);

// I2C read に応答する。通常は status、OP_READ_HASHRATE の直後だけ 2B のカウンタを返す。
static void on_request(void) {
    if (g_pending_hr_read) {
        g_pending_hr_read = false;

        // ハッシュレート用カウンタは読んだらゼロへ戻す。
        uint16_t v;
        noInterrupts();
        v = g_hr_sha256d_count;
        g_hr_sha256d_count = 0;
        interrupts();

        uint8_t reply[2];
        reply[0] = (uint8_t)(v & 0xFF);
        reply[1] = (uint8_t)((v >> 8) & 0xFF);
        Wire.write(reply, 2);
        return;
    }

    prepare_status_tx();
    Wire.write(g_tx_buf, g_tx_len);
}

// 通常ステータス読取用の送信フレームを組み立てる。
static void prepare_status_tx(void) {
    uint16_t snap;

    // ステータス読取用カウンタはこのタイミングでスナップしてゼロへ戻す。
    noInterrupts();
    uint32_t c = g_sha256d_count;
    g_sha256d_count = 0;
    interrupts();

    snap = (c > 65535u) ? 65535u : (uint16_t)c;

    g_tx_buf[0] = g_status;
    if (g_status == ST_FOUND) {
        // ST_FOUND 時だけ nonce / hash 先頭 8B / カウンタも返す。
        g_tx_buf[1] = (uint8_t)(g_found_nonce & 0xFF);
        g_tx_buf[2] = (uint8_t)((g_found_nonce >> 8) & 0xFF);
        g_tx_buf[3] = (uint8_t)((g_found_nonce >> 16) & 0xFF);
        g_tx_buf[4] = (uint8_t)((g_found_nonce >> 24) & 0xFF);
        for (int i = 0; i < 8; i++) {
            g_tx_buf[5 + i] = g_found_hash[i];
        }
        g_tx_buf[13] = (uint8_t)(snap & 0xFF);
        g_tx_buf[14] = (uint8_t)((snap >> 8) & 0xFF);
        g_tx_len = 15;
    } else {
        g_tx_len = 1;
    }
}

// I2C write コマンドを受け取り、ヘッダ・target・開始/中止などを反映する。
static void on_receive(int howMany) {
    if (howMany < 1) {
        return;
    }

    uint8_t op = (uint8_t)Wire.read();
    howMany--;

    if (g_pending_hr_read && op != OP_READ_HASHRATE) {
        g_pending_hr_read = false;
    }

    switch (op) {
    case OP_PING:
        // 疎通確認。実際の処理は loop 側のログだけ。
        g_pending_cmd = OP_PING;
        g_have_new_cmd = true;
        break;

    case OP_RESET:
        // 状態、ヒット、各種カウンタを初期化する。
        g_abort_scan = true;
        g_scan_armed = false;
        g_status = ST_IDLE;
        clear_found_hit();
        g_sha256d_count = 0u;
        g_hr_sha256d_count = 0u;
        g_pending_hr_read = false;
        g_hash_cache_valid = false;
        led_force_off();
        g_pending_cmd = OP_RESET;
        g_have_new_cmd = true;
        break;

    case OP_READ_HASHRATE:
        // 次回 on_request だけ 2B の H/s カウンタを返す。
        g_pending_hr_read = true;
        break;

    case OP_HDR_FRAG:
        // 80B ブロックヘッダを offset 付き断片で受け取る。
        if (howMany >= 1) {
            uint8_t off = (uint8_t)Wire.read();
            howMany--;
            while (howMany > 0 && off < 80) {
                g_header[off++] = (uint8_t)Wire.read();
                howMany--;
            }
            g_hash_cache_valid = false;
        }
        break;

    case OP_SET_TARGET:
        // target 下位 4 ワードを LE で更新する。
        if (howMany >= 16) {
            for (int i = 0; i < 4; i++) {
                uint8_t a0 = (uint8_t)Wire.read();
                uint8_t a1 = (uint8_t)Wire.read();
                uint8_t a2 = (uint8_t)Wire.read();
                uint8_t a3 = (uint8_t)Wire.read();
                g_pool_target[i] = (uint32_t)a0 |
                                   ((uint32_t)a1 << 8) |
                                   ((uint32_t)a2 << 16) |
                                   ((uint32_t)a3 << 24);
            }
        }
        break;

    case OP_SET_TARGET_HI:
        // target 上位 4 ワードを LE で更新する。
        if (howMany >= 16) {
            for (int i = 0; i < 4; i++) {
                uint8_t a0 = (uint8_t)Wire.read();
                uint8_t a1 = (uint8_t)Wire.read();
                uint8_t a2 = (uint8_t)Wire.read();
                uint8_t a3 = (uint8_t)Wire.read();
                g_pool_target[i + 4] = (uint32_t)a0 |
                                       ((uint32_t)a1 << 8) |
                                       ((uint32_t)a2 << 16) |
                                       ((uint32_t)a3 << 24);
            }
        }
        break;

    case OP_SET_NONCE0:
        // 探索開始 nonce を LE で受け取る。
        if (howMany >= 4) {
            uint8_t a0 = (uint8_t)Wire.read();
            uint8_t a1 = (uint8_t)Wire.read();
            uint8_t a2 = (uint8_t)Wire.read();
            uint8_t a3 = (uint8_t)Wire.read();
            g_nonce_start = (uint32_t)a0 |
                            ((uint32_t)a1 << 8) |
                            ((uint32_t)a2 << 16) |
                            ((uint32_t)a3 << 24);
        }
        break;

    case OP_START:
        // 直ちに重い処理はせず、loop 側で採掘を再開させる。
        g_abort_scan = false;
        g_scan_armed = true;
        g_status = ST_IDLE;
        clear_found_hit();
        led_force_off();
        g_pending_cmd = OP_START;
        g_have_new_cmd = true;
        break;

    case OP_ABORT:
        // 採掘を中止し、次の run_scan_slice で止まるようにする。
        g_abort_scan = true;
        g_scan_armed = false;
        g_status = ST_ABORTED;
        led_force_off();
        g_pending_cmd = OP_ABORT;
        g_have_new_cmd = true;
        break;

    default:
        // 未知コマンドはエラー状態へ。
        g_status = ST_ERR;
        g_scan_armed = false;
        led_force_off();
        break;
    }
}

// 最大 max_steps 個の nonce を試し、ヒットか中断までを 1 スライスで進める。
static void run_scan_slice(uint32_t max_steps) {
    uint32_t hash_state[8];
    uint32_t n = g_nonce_start;
    uint32_t local_sha256d = 0;
    uint16_t local_hr = 0;

    ensure_hash_cache();
    g_status = ST_SCAN;

    for (uint32_t i = 0; i < max_steps; i++) {
        // マスターから中止が来ていたら、この場で止める。
        if (g_abort_scan) {
            add_hash_counts(local_sha256d, local_hr);
            g_status = ST_ABORTED;
            g_scan_armed = false;
            led_force_off();
            return;
        }

        // midstate から nonce 1 個ぶんの SHA256d を実行する。
        sha256d_midstate_nonce(n, hash_state);
        local_sha256d++;
        if (local_hr < 65535u) {
            local_hr++;
        }

        // target を満たしたら nonce と hash を保存して停止する。
        if (hash_state_meets_target_full(hash_state, g_pool_target)) {
            add_hash_counts(local_sha256d, local_hr);
            g_found_nonce = n;
            store_hash_state(g_found_hash, hash_state);
            g_status = ST_FOUND;
            g_scan_armed = false;
            led_force_off();
            return;
        }

        n++;
    }

    // 今回ぶんの進捗を確定し、次スライスの開始 nonce を更新する。
    add_hash_counts(local_sha256d, local_hr);
    g_nonce_start = n;
    g_status = ST_SCAN;
}

#if MINER_SLAVE_DEBUG
// printf を使わず uint32 を 10 進で出すデバッグ補助。
static void dbg_print_u32(uint32_t v) {
    if (v >= 10u) {
        dbg_print_u32(v / 10u);
    }
    Serial.print((char)('0' + (int)(v % 10u)));
}

// 1 バイトを 16 進 2 桁で出すデバッグ補助。
static void dbg_hex_byte(uint8_t b) {
    static const char k[] = "0123456789abcdef";
    Serial.print(k[b >> 4]);
    Serial.print(k[b & 15]);
}
#endif

// 起動時に LED / Serial / I2C スレーブを初期化する。
void setup() {
#if MINER_SLAVE_DEBUG
    Serial.begin(115200);
    delay(50);
    Serial.print(F("ch32_miner2_slave addr="));
    dbg_hex_byte(I2C_SLAVE_ADDR);
    Serial.println();
#endif

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
    digitalWrite(LED_BUILTIN, LOW);
    led_force_off();

    Wire.begin(I2C_SLAVE_ADDR);
    Wire.onReceive(on_receive);
    Wire.onRequest(on_request);

    g_status = ST_IDLE;
    g_tx_len = 0;
}

// メインループ。新規コマンド処理、採掘スライス実行、LED 表示を行う。
void loop() {
    if (g_have_new_cmd) {
        uint8_t c = g_pending_cmd;
        g_have_new_cmd = false;

#if MINER_SLAVE_DEBUG
        // ISR から渡された直近コマンドだけを軽くログする。
        if (c == OP_PING) {
            Serial.println(F("ping"));
        } else if (c == OP_RESET) {
            Serial.println(F("reset"));
        } else if (c == OP_START) {
            Serial.println(F("start"));
        } else if (c == OP_ABORT) {
            Serial.println(F("abort"));
        }
#endif
    }

    if (g_scan_armed && g_status != ST_FOUND && g_status != ST_ABORTED) {
        // 採掘は細かいスライスに分け、I2C 応答性を保ちながら進める。
        const uint32_t k_batch = 256;
        run_scan_slice(k_batch);

#if MINER_SLAVE_DEBUG
        // ヒット時だけ nonce と hash 先頭 8B を出す。
        if (g_status == ST_FOUND) {
            Serial.print(F("hit nonce="));
            dbg_print_u32(g_found_nonce);
            Serial.print(F(" h="));
            for (int i = 0; i < 8; i++) {
                dbg_hex_byte(g_found_hash[i]);
            }
            Serial.println();
        }
#endif
    }

    if (g_status == ST_SCAN && g_scan_armed) {
        // 元実装どおり、採掘中だけ LED を点滅させる。
        if ((g_led_count++ % 2u) == 0u) {
            led_force_on();
        } else {
            led_force_off();
        }
    } else if (g_led_mining_visual) {
        led_force_off();
    }
}
