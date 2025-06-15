/*
  benchmark_m5gfx2.ino
  ESP32のメモリのベンチマークテスト　M5Stack Core 2想定

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/

#include <M5Unified.h>
// #include <M5GFX.h>
// M5GFX display;

// ダミーのPROGMEMデータ
#include "dummy_progmem.h"

//　Canvasの作成
M5Canvas canvas_src;
M5Canvas canvas_dst;

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))

// グローバル変数
int m5_w, m5_h;
int color_bit = 16;
String csv = "";
int option = 0;

// ---------------------------------------------------------------------------------------------

// メモリ定義方法の種類
enum memory_type_t {
  type_unknown,
  type_malloc,
  type_ps_malloc,
  type_dma,
  type_internal,
  type_spiram,
  type_progmem,
  type_display
};
String memory_type_str[] = {"unknown", "malloc", "ps_malloc", "dma", "internal", "spiram", "progmem", "display"};

// 指定されたタイプでメモリを確保する
void* malloc_memory(memory_type_t type, size_t size, bool quiet=false) {
  if (type == type_progmem) return NULL;
  void* ptr = NULL;
  if (!quiet) spf("  malloc size=%d %s ", size, memory_type_str[type].c_str());
  switch (type) {
    case type_malloc:    ptr = malloc(size);    break;
    case type_ps_malloc: ptr = ps_malloc(size); break;
    // 以下、heap_caps_malloc() をやめて heap_caps_aligned_alloc(16,..) にした
    case type_dma:       ptr = heap_caps_aligned_alloc(16, size, MALLOC_CAP_DMA); break;
    case type_internal:  ptr = heap_caps_aligned_alloc(16, size, MALLOC_CAP_INTERNAL); break;
    case type_spiram:    ptr = heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM); break;
  }
  if (!quiet) sp((ptr != NULL) ? get_memory_region(ptr) : " failed");
  return ptr;
}

// メモリを解放する
void free_memory(memory_type_t type, void* ptr) {
  if (ptr == NULL) return;
  if (type == type_progmem) return;
  // spf("  free %s: %s\n", memory_type_str[type].c_str(), get_memory_region(ptr).c_str());
  free(ptr);
}

// 指定したメモリのアドレスがどの領域なのかを返す for ESP32
// 参考: https://docs.espressif.com/projects/esp-idf/en/v4.3.1/esp32/hw-reference/index.html
// 参考: https://zenn.dev/paradoia/articles/ce34af18e74392
String get_memory_region(void* ptr) {
  String region = "";
  uint32_t addr = (uint32_t)ptr;
  // Embeded Memory
  if      (addr >= 0x3FF80000 && addr <= 0x3FF81FFF) region = "RTC_FAST_Memory /Data"; // 8KB
  else if (addr >= 0x3FF90000 && addr <= 0x3FF9FFFF) region = "in_ROM_1 /Data";// 64KB  "Internal ROM 1";
  else if (addr >= 0x3FFAE000 && addr <= 0x3FFDFFFF) region = "in_SRAM_2_DMA /Data";// 200KB  "Internal SRAM 2 DMA";
  else if (addr >= 0x3FFE0000 && addr <= 0x3FFFFFFF) region = "in_SRAM_1_DMA /Data";// 128KB  "Internal SRAM 1 DMA";
  else if (addr >= 0x40000000 && addr <= 0x40007FFF) region = "in_ROM_0_a /Inst";// 32KB "Internal ROM 0";
  else if (addr >= 0x40008000 && addr <= 0x4005FFFF) region = "in_ROM_0_b /Inst";// 352KB "Internal ROM 0";
  else if (addr >= 0x40070000 && addr <= 0x4007FFFF) region = "in_SRAM_0_cache /Inst";// 64KB  "Internal SRAM 0 Cache";
  else if (addr >= 0x40080000 && addr <= 0x4009FFFF) region = "in_SRAM_0_none /Inst";// 128KB  "Internal SRAM 0 -";
  else if (addr >= 0x400A0000 && addr <= 0x400AFFFF) region = "in_SRAM_1_a /Inst";// 64KB  "Internal SRAM 1 (Inst)";
  else if (addr >= 0x400B0000 && addr <= 0x400B7FFF) region = "in_SRAM_1_b /Inst";// 32KB  "Internal SRAM 1 (Inst)";
  else if (addr >= 0x400B8000 && addr <= 0x400BFFFF) region = "in_SRAM_1_c /Inst";// 32KB  "Internal SRAM 1 (Inst)";
  else if (addr >= 0x400C0000 && addr <= 0x400C1FFF) region = "RTC_FAST_Memory /Inst";// 8KB
  else if (addr >= 0x50000000 && addr <= 0x50001FFF) region = "RTC_SLOW_Memory /Data+Isnt";// 8KB
  // External Memory
  else if (addr >= 0x3F400000 && addr <= 0x3F7FFFFF) region = "ex_Flash_r_a /Data";// 4MB  "External Flash";
  else if (addr >= 0x3F800000 && addr <= 0x3FBFFFFF) region = "ex_SRAM_rw /Data";// 4MB  "External RAM (PSRAM)";  
  else if (addr >= 0x400C2000 && addr <= 0x40BFFFFF) region = "ex_Flash_r_b /Inst";// 11512KB  "External Flash";
  else if (addr == 0) region = "Error";
  else region = "Other";
  return region;
}

// 数値をカンマ区切りで返す
String int2comma(int32_t value) {
  String str = String(value);
  int len = str.length();
  bool neg = (str[0] == '-');
  int start = neg ? 1 : 0;
  for (int i=len-3; i>start; i-=3) {
    str = str.substring(0, i) + ',' + str.substring(i);
  }
  return str;
}

// デバッグ: 空きメモリ確認
void debug_free_memory(String str) {
  sp("## "+str);
  spf("MALLOC_CAP_INTERNAL: %9s / %9s\n", int2comma(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)).c_str(), int2comma(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)).c_str() );
  spf("MALLOC_CAP_DMA     : %9s / %9s\n", int2comma(heap_caps_get_free_size(MALLOC_CAP_DMA)).c_str(), int2comma(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)).c_str() );
  spf("MALLOC_CAP_SPIRAM  : %9s / %9s\n\n", int2comma(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)).c_str(), int2comma(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)).c_str() );
}

// 前回より減ったメイン(DMA)メモリ量を返す
uint32_t get_decrease_dma_memory() {
  static uint32_t prev_free_memory = 0;
  uint32_t free_memory = heap_caps_get_free_size(MALLOC_CAP_DMA);
  uint32_t decrease_memory = prev_free_memory - free_memory;
  prev_free_memory = free_memory;
  return decrease_memory;
}

// 前回より減ったメイン(Internal)メモリ量を返す
uint32_t get_decrease_internal_memory() {
  static uint32_t prev_free_memory = 0;
  uint32_t free_memory = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  uint32_t decrease_memory = prev_free_memory - free_memory;
  prev_free_memory = free_memory;
  return decrease_memory;
}

// 前回より減ったSPIRAM量を返す
uint32_t get_decrease_spiram_memory() {
  static uint32_t prev_free_memory = 0;
  uint32_t free_memory = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  uint32_t decrease_memory = prev_free_memory - free_memory;
  prev_free_memory = free_memory;
  return decrease_memory;
}

// 前回より減った各種メモリ量を文字列で返す
String get_used_memory_str() {
  String str = "Main ";
  str += int2comma(get_decrease_dma_memory());
  str += " , Internal ";
  str += int2comma(get_decrease_internal_memory());
  str += " , Spiram ";
  str += int2comma(get_decrease_spiram_memory());
  return str;
}

// 前回から減ったメモリ量を比較して、どのメモリが使われたのかを推測する
memory_type_t get_used_memory_type() {
  uint32_t dma_memory = get_decrease_dma_memory();
  uint32_t internal_memory = get_decrease_internal_memory();
  uint32_t spiram_memory = get_decrease_spiram_memory();
  if (dma_memory > internal_memory && dma_memory > spiram_memory) return type_dma;
  if (internal_memory > dma_memory && internal_memory > spiram_memory) return type_internal;
  if (spiram_memory > dma_memory && spiram_memory > internal_memory) return type_spiram;
  return type_unknown;
}

// 前回から経過した時間(us)を返す
uint32_t get_elapsed_time() {
  static uint32_t prev_time = 0;
  uint32_t current_time = micros();
  uint32_t elapsed_time = current_time - prev_time;
  prev_time = current_time;
  return elapsed_time;
}

// canvas.createSprite()の代わりに、指定したメモリタイプでスプライトを作成する　　！！失敗！！画像が壊れる
bool createSpriteCustom(M5Canvas& canvas, memory_type_t type, int w, int h, int depth) {
  // 1. 必要なバッファサイズを計算
  size_t bufSize = w * h * (depth / 8) + 1000;
  // if (type == type_internal && bufSize % 4 != 0) {
  //   size_t old_size = bufSize;
  //   bufSize = (bufSize + 3) & ~3;  // 4の倍数に切り上げ
  //   spf("createSpriteCustom: Size aligned to 4-byte boundary: %d -> %d\n", old_size, bufSize);
  // }
  
  // 2. 指定されたメモリタイプでメモリを確保
  void* buf = NULL;
  switch (type) {
    case type_malloc:
      buf = malloc(bufSize);
      break;
    case type_ps_malloc:
      buf = ps_malloc(bufSize);
      break;
    case type_dma:
      buf = heap_caps_malloc(bufSize, MALLOC_CAP_DMA);
      break;
    case type_internal:
      buf = heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL);
      break;
    case type_spiram:
      buf = heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
      break;
    default:
      spf("createSpriteCustom: Unsupported memory type: %s\n", memory_type_str[type].c_str());
      return false;
  }
  
  // 3. メモリ確保の成功チェック
  if (!buf) {
    spf("createSpriteCustom: Memory allocation failed for %s (size=%zu bytes)\n", 
        memory_type_str[type].c_str(), bufSize);
    return false;
  }
  
  // 4. デバッグ情報を表示
  spf("createSpriteCustom: %s allocated %zu bytes at 0x%08X (%s)\n", 
      memory_type_str[type].c_str(), bufSize, (uint32_t)buf, get_memory_region(buf).c_str());
  
  // 5. setBufferでキャンバスに渡す（第4引数はstrideで横ピクセル数）
  canvas.setBuffer(buf, w, h, w);
  
  return true;
}

// createSpriteCustomで確保したメモリを解放する
void deleteSpriteCustom(M5Canvas& canvas) {
  // キャンバスからバッファアドレスを取得
  void* buf = canvas.getBuffer();
  if (buf != NULL) {
    spf("deleteSpriteCustom: Freeing buffer at 0x%08X (%s)\n", 
        (uint32_t)buf, get_memory_region(buf).c_str());
    free(buf);
  }
  // キャンバスをリセット
  canvas.setBuffer(nullptr, 0, 0, 0);
}

// ---------------------------------------------------------------------------------------------

// 初期化
void setup() {
  M5.begin();
  Serial.begin(115200);
  m5_w = M5.Display.width();
  m5_h = M5.Display.height();
  sp("Start");
  spf("m5_w=%d, m5_h=%d\n", m5_w, m5_h);
  debug_free_memory("Start");

  // ディスプレイの初期化
  M5.Display.setColorDepth(16);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setCursor(0, 0);
  M5.Display.setFont(&fonts::Font4);
}

// ---------------------------------------------------------------------------------------------

// mallocで指定できるメモリが何バイト単位なのかを検証する
//
//  検証結果
//  MALLOC_CAP_INTERNALのみ4バイト単位、他は1バイト単位だった
//
void test_memory_block_size() {
  void* ptr1 = NULL, *ptr2 = NULL, *ptr3 = NULL, *ptr4 = NULL;
  sp("\n## mallocで指定できるメモリが何バイト単位なのかを検証する");
  for (int size = 1; size <= 20; size++) {
    spf("Malloc size %d ", size);
    ptr1 = malloc(size);
    spf("malloc:%c ", (ptr1 ? 'o' : 'x'));
    if (size % 4 == 0) {  // 4バイト単位にしないとpanicで落ちる
      ptr2 = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
      spf("cap:%c ", (ptr2 ? 'o' : 'x'));
    }
    ptr3 = heap_caps_malloc(size, MALLOC_CAP_DMA);
    spf("dma:%c ", (ptr3 ? 'o' : 'x'));
    ptr4 = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    spf("spiram:%c ", (ptr4 ? 'o' : 'x'));
    sp("");
    if (ptr1) free(ptr1);
    if (ptr2) free(ptr2);
    if (ptr3) free(ptr3);
    if (ptr4) free(ptr4);
    ptr1 = NULL;
    ptr2 = NULL;
    ptr3 = NULL;
    ptr4 = NULL;
  }
}

// 確保したメモリがどの領域に作成されたのかを調査する
const byte test_progmem_data[10] PROGMEM = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
void test_memory_region() {
  void* ptr = NULL;
  sp("\n## 確保したメモリがどの領域に作成されたのかを調査する");
  csv += "\nMemory Location by Size\n";
  csv += "Type,Size,Location,Address\n";
  int sizes[] = {24*24*2, 48*48*2, 96*96*2, 160*120*2, 200*160*2, 240*240*2, 320*240*2};
  // malloc()
  for (int s=0; s<array_length(sizes); s++) {
    ptr = malloc_memory(type_malloc, sizes[s], true);
    spf("%-20s size=%7d : %-25s (%08x)\n", "malloc()", sizes[s], get_memory_region(ptr).c_str(), (uint32_t)ptr);
    if (ptr) free(ptr);
    // CSVに追加
    char buff[100];
    sprintf(buff, "malloc(),%d,%s,%08X\n", sizes[s], get_memory_region(ptr).c_str(), (uint32_t)ptr);
    csv += String(buff);
  }
  // ps_malloc()
  for (int s=0; s<array_length(sizes); s++) {
    ptr = malloc_memory(type_ps_malloc, sizes[s], true);
    spf("%-20s size=%7d : %-25s (%08x)\n", "ps_malloc()", sizes[s], get_memory_region(ptr).c_str(), (uint32_t)ptr);
    if (ptr) free(ptr);
    // CSVに追加
    char buff[100];
    sprintf(buff, "ps_malloc(),%d,%s,%08X\n", sizes[s], get_memory_region(ptr).c_str(), (uint32_t)ptr);
    csv += String(buff);
  }
  // MALLOC_CAP_INTERNAL
  for (int s=0; s<array_length(sizes); s++) {
    ptr = malloc_memory(type_internal, sizes[s], true);
    spf("%-20s size=%7d : %-25s (%08x)\n", "MALLOC_CAP_INTERNAL", sizes[s], get_memory_region(ptr).c_str(), (uint32_t)ptr);
    if (ptr) free(ptr);
    // CSVに追加
    char buff[100];
    sprintf(buff, "MALLOC_CAP_INTERNAL,%d,%s,%08X\n", sizes[s], get_memory_region(ptr).c_str(), (uint32_t)ptr);
    csv += String(buff);
  }
  // MALLOC_CAP_DMA
  for (int s=0; s<array_length(sizes); s++) {
    ptr = malloc_memory(type_dma, sizes[s], true);
    spf("%-20s size=% d : %-25s (%08x)\n", "MALLOC_CAP_DMA", sizes[s], get_memory_region(ptr).c_str(), (uint32_t)ptr);
    if (ptr) free(ptr);
    // CSVに追加
    char buff[100];
    sprintf(buff, "MALLOC_CAP_DMA,%d,%s,%08X\n", sizes[s], get_memory_region(ptr).c_str(), (uint32_t)ptr);
    csv += String(buff);
  }
  // MALLOC_CAP_SPIRAM
  for (int s=0; s<array_length(sizes); s++) {
    ptr = malloc_memory(type_spiram, sizes[s], true);
    spf("%-20s size=%7d : %-25s (%08x)\n", "MALLOC_CAP_SPIRAM", sizes[s], get_memory_region(ptr).c_str(), (uint32_t)ptr);
    if (ptr) free(ptr);
    // CSVに追加
    char buff[100];
    sprintf(buff, "MALLOC_CAP_SPIRAM,%d,%s,%08X\n", sizes[s], get_memory_region(ptr).c_str(), (uint32_t)ptr);
    csv += String(buff);
  }
  // PROGMEM
  spf("%-20s: %-25s (%08x)\n", "PROGMEM", get_memory_region((void*)test_progmem_data).c_str(), (uint32_t)test_progmem_data);
  // CSVに追加
  char buff[100];
  sprintf(buff, "PROGMEM,%d,%s,%08X\n", 10, get_memory_region((void*)test_progmem_data).c_str(), (uint32_t)test_progmem_data);
  csv += String(buff);
  sp("");
}

// Canvasのメモリ消費量のテスト sub
void test_memory_usage_sub(bool psram, int color_depth, int w, int h) {
  uint32_t memory_main, memory_spiram;

  // キャンバスの初期化
  canvas_src.setPsram(psram);
  canvas_src.setColorDepth(color_depth);
  get_decrease_dma_memory(); //dummy
  get_decrease_spiram_memory(); //dummy
  auto res = canvas_src.createSprite(w, h);
  memory_main = get_decrease_dma_memory();  // メインメモリ消費量を取得
  memory_spiram = get_decrease_spiram_memory();  // SPIRAMメモリ消費量を取得

  // 結果を表示
  String resstr = (res) ? "Pass" : "**Fail**";
  spf("  Create Sprite(%d,%d): %s %s %s\n", w, h, int2comma(memory_main).c_str(), int2comma(memory_spiram).c_str(), resstr.c_str());

  // CSVに追加
  char buff[100];
  sprintf(buff, "%d,%d,%d,%d,%d,%d,%s\n", w, h, psram, color_depth, memory_main, memory_spiram, resstr.c_str());
  csv += String(buff);

  // キャンバスの削除
  canvas_src.deleteSprite();
  spf("  Delete Sprite: %s\n", get_used_memory_str().c_str());
}

// Canvasのメモリ消費量のテスト main
void test_memory_usage_main() {
  csv += "\nCanvas Memory Usage";
  csv += "Width,Height,PSRAM,Color,Memory(Main),Memory(Spiram),Result\n";
  int depth[] = {4, 8, 16};
  bool psram[] = {false, true};
  int wh[][2] = {{24,24}, {48,48}, {96,96}, {160,120}, {200,160}, {240,240}, {320,240}};
  for (int j=0; j<array_length(psram); j++) {
    for (int k=0; k<array_length(depth); k++) {
      for (int i=0; i<array_length(wh); i++) {
        int w = wh[i][0];
        int h = wh[i][1];
        spf("TEST (%d,%d) psram=%d, depth=%d\n", w, h, psram[j], depth[k]);
        test_memory_usage_sub(psram[j], depth[k], w, h);
      }
    }
  }
}

// コピー元の初期データを作成する
#define INIT_MEMORY_FILL 0
#define INIT_MEMORY_INCREMENT 1
#define INIT_MEMORY_RANDOM 2
void create_initial_data(int size, byte *ptr, int mode=0, int pattern=0) {
  if (ptr == NULL) return;
  byte buff[32];
  if (mode == INIT_MEMORY_RANDOM) {
    for (int i=0; i<size; i+=32) {
      for (int j=0; j<32; j++) buff[j] = random(256);
      size_t copy_size = (i+32 > size) ? size-i : 32;
      memcpy(ptr+i, buff, copy_size);
    }
  } else {
    if (mode == INIT_MEMORY_FILL) {
      for (int i=0; i<32; i++) buff[i] = pattern % 256;
    } else if (mode == INIT_MEMORY_INCREMENT) {
      for (int i=0; i<32; i++) buff[i] = (i+pattern) % 256;
    } else {
      memset(buff, 0, 32);
    }
    for (int i=0; i<size; i+=32) {
      size_t copy_size = (i+32 > size) ? size-i : 32;
      memcpy(ptr+i, buff, copy_size);
    }
  }
}

// キャッシュクリア
void clear_cache() {
  size_t size = 32 * 1024;
  void* ptr1 = malloc_memory(type_spiram, size, true);
  void* ptr2 = malloc_memory(type_spiram, size, true);
  if (ptr1 == NULL || ptr2 == NULL) return;
  create_initial_data(size, (byte*)ptr1, INIT_MEMORY_INCREMENT, 0);
  memcpy((byte*)ptr1, (byte*)ptr2, size);
  create_initial_data(size, (byte*)ptr2, INIT_MEMORY_INCREMENT, 1);
  memcpy((byte*)ptr2, (byte*)ptr1, size);
  free_memory(type_spiram, ptr1);
  free_memory(type_spiram, ptr2);
}

// mallocメモリコピーの時間計測 sub
#define INIT_FROM_SAME 0
#define INIT_FROM_DIFF 1
uint32_t test_memory_copy_sub(memory_type_t from_type, memory_type_t to_type, int size, int inipat=INIT_FROM_SAME, bool quiet=false) {
  if (size < 32) return 0;
  byte *from_memory = NULL, *to_memory = NULL;
  uint32_t elapsed_time = 0;
  bool success = false;

  // PROGMEMからinternalはpanicになるのでスキップ
  if (from_type == type_progmem && to_type == type_internal) {
    return 0;
  }

  // コピーする回数を計算
  int copy_count = 10000000 / size;   // 合計10MB分のメモリをコピーする
  if (copy_count < 50) copy_count = 50;  // 最低50回は測定する
  if (!quiet) spf("  Copy %d bytes %d times\n", size, copy_count);
  
  // panic防止のため4バイト境界に揃える（本来はtype_internalのみでいいが、malloc()の場合はどこになるかが未定なので）
  if (size % 4 != 0) {
    size_t old_size = size;
    size = (size + 3) & ~3;  // 4の倍数に切り上げ
    if (!quiet) spf("  Size aligned to 4-byte boundary: %d -> %d\n", old_size, size);
  }
  
  // メモリを確保（明示的にcapabilityを指定）
  if (from_type == type_progmem) {
    size_t no;
    switch (size) {
      case 1152: no = 0; break;
      case 4608: no = 1; break;
      case 18432: no = 2; break;
      case 38400: no = 3; break;
      case 64000: no = 4; break;
      case 115200: no = 5; break;
      case 153600: no = 6; break;
      default: no = 99; break;
    }
    if (no <= 6) {
      from_memory = (byte*) pgm_read_ptr(&test_progmem_datas[no]);
    }
  } else {
    from_memory = (byte*) malloc_memory(from_type, size, quiet);
  }
  to_memory = (byte*) malloc_memory(to_type, size, quiet);
  if (!quiet) sp("  Start memory copy"); delay(100);

  // メモリアドレス情報を表示
  if (!quiet) {
    spf("  Memory addresses: from (%08X) %s\n", (uint32_t)from_memory, get_memory_region(from_memory).c_str());
    spf("  Memory addresses: to   (%08X) %s\n", (uint32_t)to_memory, get_memory_region(to_memory).c_str());
  }

  // ベンチマーク開始
  if (from_memory != NULL && to_memory != NULL) {

    // コピー元の初期データを作成
    if (from_type != type_progmem) {
      create_initial_data(size, from_memory, INIT_MEMORY_INCREMENT, 0);  // 00,01,02,..FF,00,01,..
    }

    // メモリをコピーする（複数回の平均を求める）初期データは同じversion　CACHEの影響を受けるかも
    if (inipat == INIT_FROM_SAME) {
      get_elapsed_time(); //dummy
      for (int i=0; i<copy_count; i++) {
        memcpy(to_memory, from_memory, size);
      }
      elapsed_time = get_elapsed_time() / copy_count;
    }
    
    // メモリをコピーする（複数回の平均を求める）毎回初期データを変えてキャッシュをクリアするversion　CACHEの影響を受けないかも
    if (inipat == INIT_FROM_DIFF) {
      elapsed_time = 0;
      static int pat = 0;
      for (int i=0; i<copy_count; i++) {
        create_initial_data(size, from_memory, INIT_MEMORY_RANDOM, pat++);  // ランダム
        clear_cache();  // キャッシュをクリア
        get_elapsed_time(); //dummy
        memcpy(to_memory, from_memory, size);
        elapsed_time += get_elapsed_time();
      }
      elapsed_time = elapsed_time / copy_count;
    }

    // 最後にデータの整合性チェック
    if (memcmp(from_memory, to_memory, size) == 0) {
      success = true;
    } else {
      if (!quiet) sp("  ERROR: Data mismatch detected!");
    }
  } else {
    if (!quiet) {
      if (from_memory == NULL) spf("  malloc failed! from_type=%s\n", memory_type_str[from_type].c_str());
      if (to_memory == NULL) spf("  malloc failed! to_type=%s\n", memory_type_str[to_type].c_str());
    }
  }

  // メモリを開放
  if (from_memory != NULL) free_memory(from_type, from_memory);
  if (to_memory != NULL) free_memory(to_type, to_memory);

  // 結果を表示
  String resstr = (success) ? "Pass" : "**Fail**";
  float mbps = ((float)size / 1024.0 / 1024.0) / ((float)elapsed_time / 1000000.0);  // MB/s = (size/1024/1024) / (time_us/1000000)
  if (!quiet) spf("  From:%s To:%s Size: %d bytes Copy: %s us (%.1f MB/s) %s\n", 
    memory_type_str[from_type].c_str(), memory_type_str[to_type].c_str(), size, 
    int2comma(elapsed_time).c_str(), mbps, resstr.c_str());

  // CSVに追加
  if (!quiet) {
    char buff[150];
    sprintf(buff, "%s,%s,%d,%d,%.1f,%s,%s,%s\n", 
      memory_type_str[from_type].c_str(), memory_type_str[to_type].c_str(), size, 
      elapsed_time, mbps, resstr.c_str(), 
      get_memory_region(from_memory).c_str(), get_memory_region(to_memory).c_str());
    csv += String(buff);
  }

  return elapsed_time;
}

// mallocメモリコピーの時間計測 main
void test_memory_copy_main() {
  csv += "\nMemory Copy Time\n";
  csv += "From,To,Size,Time(us),MB/s,Result,From(type),To(type)\n";  
  memory_type_t from_type[] = { type_dma, type_internal, type_spiram, type_progmem};
  memory_type_t to_type[] = { type_dma, type_internal, type_spiram};
  int sizes[] = {24*24*2, 48*48*2, 96*96*2, 160*120*2, 200*160*2, 240*240*2, 320*240*2};
  for (int s=0; s<array_length(sizes); s++) {
    for (int f=0; f<array_length(from_type); f++) {
      for (int t=0; t<array_length(to_type); t++) {
        spf("TEST from=%s, to=%s, size=%d\n", 
          memory_type_str[from_type[f]].c_str(), memory_type_str[to_type[t]].c_str(), sizes[s]);
        test_memory_copy_sub(from_type[f], to_type[t], sizes[s], INIT_FROM_SAME, false);
      }
    }
  }
}

// メモリキャッシュの容量を検証する
void test_memory_cache_size(int inipat, int size_min, int size_max, int size_step) {
  spn("\n## メモリキャッシュの容量を検証する");
  spf(" inipat=%d\n", inipat);
  // テスト条件
  memory_type_t fromto_type[][2] = {
    {type_dma, type_dma},
    {type_dma, type_spiram},
    {type_spiram, type_dma},
    {type_spiram, type_spiram}
  };
  float results[array_length(fromto_type)][size_max/size_step];
  // テストを実施
  for (int ft=0; ft<array_length(fromto_type); ft++) {
    for (int size=size_min; size<=size_max; size+=size_step) {
      uint32_t elapsed_time = test_memory_copy_sub(fromto_type[ft][0], fromto_type[ft][1], size, inipat, true);
      float mbps = ((float)size / 1024.0 / 1024.0) / ((float)elapsed_time / 1000000.0);  // MB/s = (size/1024/1024) / (time_us/1000000)
      spf("TEST size=%d from=%s to=%s : %s us (%.1f MB/s)\n", size, 
        memory_type_str[fromto_type[ft][0]].c_str(), memory_type_str[fromto_type[ft][1]].c_str(), 
        int2comma(elapsed_time).c_str(), mbps);
      results[ft][size/size_step] = mbps;
    }
  }
  // 結果を集計
  csv += "\nMemory Cache Size Check (inipat=" + String(inipat) + ")\n";
  csv += "Size";
  for (int ft=0; ft<array_length(fromto_type); ft++) {
    csv += "," + String(memory_type_str[fromto_type[ft][0]].c_str()) + "->" + String(memory_type_str[fromto_type[ft][1]].c_str());
  }
  csv += "\n";
  for (int size=size_min; size<=size_max; size+=size_step) {
    csv += String(size);
    for (int ft=0; ft<array_length(fromto_type); ft++) {
      csv += "," + String(results[ft][size/size_step]);
    }
    csv += "\n";
  }
}

// canvas.createSprite()するサイズで使用するメモリの場所が変わることを検証する
void test_createSprite_size() {
  sp("\n## canvas.createSprite()するサイズで使用するメモリの場所が変わることを検証する");
  csv += "\ncanvas.createSprite() location by size\n";
  csv += "Size,Width,Height,Result,Address,Region\n";
  int wh[][2] = {{24,24}, {48,48}, {96,96}, {160,120}, {200,160}, {240,240}, {320,240}};
  for (int w=0; w<array_length(wh); w++) {
    int width = wh[w][0];
    int height = wh[w][1];
    int size = width * height * 2;
    canvas_src.setPsram(false);
    canvas_src.setColorDepth(16);
    bool res = canvas_src.createSprite(width, height);
    String resstr = (res) ? "Pass" : "**Fail**";
    if (res) {
      void* addr = canvas_src.getBuffer();
      String used_src = get_memory_region(addr);
      spf("TEST size=%d (%dx%d) : addr=%08X, %s\n", size, width, height, (uint32_t)addr, used_src.c_str());
      // CSVに追加
      char buff[100];
      sprintf(buff, "%d,%d,%d,%s,%08X,%s\n", size, width, height, resstr.c_str(), (uint32_t)addr, used_src.c_str());
      csv += String(buff);
    } else {
      spf("TEST size=%d (%dx%d) : %s\n", size, width, height, resstr.c_str());
      char buff[100];
      sprintf(buff, "%d,%d,%d,%s,0,%s\n", size, width, height, resstr.c_str(), "error");
      csv += String(buff);
    }
    canvas_src.deleteSprite();
  }
}

// pushSprite()の時間計測 sub
void test_pushSprite_sub(memory_type_t from_type, memory_type_t to_type, int color_depth, int w, int h) {
  bool res_src = NULL, res_dst = NULL;
  bool res = NULL;
  String used_src, used_dst;
  uint32_t elapsed_time = 0;
  size_t size = w * h * (color_depth / 8);
  int copy_count = 2000000 / size;   // 合計2MB分のメモリをコピーする

  // PROGMEMのテストで対応してないものはスキップ
  if (from_type == type_progmem && color_depth != 16) return;
  if (from_type == type_progmem && option & 1) return;

  // コピー元 キャンバスの初期化
  if (from_type != type_progmem) {
    canvas_src.setPsram(from_type == type_spiram);
    canvas_src.setColorDepth(color_depth);
    res_src = canvas_src.createSprite(w, h);
    // res_src = createSpriteCustom(canvas_src, from_type, w, h, color_depth); //カスタム版createSprite()
    void* buffer_src = canvas_src.getBuffer();
    used_src = get_memory_region(buffer_src);
    canvas_src.fillScreen(TFT_RED);
  }

  // コピー元PROGMEMのアドレスを取得
  byte* from_progmem = NULL;
  if (from_type == type_progmem) {
    size_t no;
    switch (size) {
      case 1152: no = 0; break;
      case 4608: no = 1; break;
      case 18432: no = 2; break;
      case 38400: no = 3; break;
      case 64000: no = 4; break;
      case 115200: no = 5; break;
      case 153600: no = 6; break;
      default: no = 99; break;
    }
    if (no <= 6) {
      from_progmem = (byte*) pgm_read_ptr(&test_progmem_datas[no]);
      used_src = get_memory_region(from_progmem);
      res_src = true;
    }
  }

  // コピー先 キャンバスの初期化
  if (to_type == type_display) {
    M5.Display.setColorDepth(color_depth);
    M5.Display.fillScreen(TFT_GREEN);
    res_dst = true;
    used_dst = "Display";
  } else {
    canvas_dst.setPsram(to_type == type_spiram);
    canvas_dst.setColorDepth(color_depth);
    res_dst = canvas_dst.createSprite(w, h);
    // res_dst = createSpriteCustom(canvas_dst, to_type, w, h, color_depth); //カスタム版createSprite()
    void* buffer_dst = canvas_dst.getBuffer();
    used_dst = get_memory_region(buffer_dst);
    canvas_dst.fillScreen(TFT_BLUE);
  }

  // pushSprite()の時間計測（複数回の平均を求める）
  res = res_src && res_dst;
  if (res) {
    get_elapsed_time(); //dummy
    for (int i=0; i<copy_count; i++) {
      if (from_type == type_progmem) {
        if (to_type == type_display) {
          M5.Display.pushImage(0, 0, w, h, (const uint16_t*)from_progmem);
        } else {
          canvas_dst.pushImage(0, 0, w, h, (const uint16_t*)from_progmem);
        }
      } else {
        if (to_type == type_display) {
          if (option & 1) canvas_src.pushSprite(&M5.Display, 0, 0, 0x0001);
          else canvas_src.pushSprite(&M5.Display, 0, 0);
        } else {
          if (option & 1) canvas_src.pushSprite(&canvas_dst, 0, 0, 0x0001);
          else canvas_src.pushSprite(&canvas_dst, 0, 0);
        }
      }
    }
    elapsed_time = get_elapsed_time() / copy_count;
  }

  // 結果を表示
  String resstr = (res) ? "Pass" : "**Fail**";
  float mbps = ((float)size / 1024.0 / 1024.0) / ((float)elapsed_time / 1000000.0);  // MB/s = (size/1024/1024) / (time_us/1000000)
  spf("  From  : %s , Location: %s\n", 
    memory_type_str[from_type].c_str(), used_src.c_str());
  spf("  To    : %s , Location: %s\n", 
    memory_type_str[to_type].c_str(), used_dst.c_str());
  spf("  Size  : %d x %d depth:%d size:%s bytes\n", w, h, color_depth, int2comma(size).c_str());
  spf("  Result: %s , Copy %s us (%.1f MB/s) \n", resstr.c_str(), int2comma(elapsed_time).c_str(), mbps);

  // CSVに追加
  char buff[150];
  sprintf(buff, "%s,%s,%d,%d,%d,%zu,%u,%.1f,%s,%s,%s\n", 
    memory_type_str[from_type].c_str(), memory_type_str[to_type].c_str(), color_depth, w, h, size, 
    elapsed_time, mbps, resstr.c_str(), 
    used_src.c_str(), used_dst.c_str());
  csv += String(buff);

  // キャンバスの削除
  if (res_src) canvas_src.deleteSprite();
  if (res_dst && to_type != type_display) canvas_dst.deleteSprite();
  // if (res_src) deleteSpriteCustom(canvas_src); //カスタム版deleteSprite()
  // if (res_dst && to_type != type_display) deleteSpriteCustom(canvas_dst); //カスタム版deleteSprite()
}

// pushSprite()の時間計測 main
void test_pushSprite_main() {
  spn("\n\n## pushSprite()の時間計測 - Transparent");
  sp(option & 1 ? "あり" : "なし");
  csv += "\npushSprite() Time - Transparent:";
  csv += (option & 1) ? "Yes\n" : "None\n";
  csv += "From,To,ColorDepth,Width,Height,Size,Time,MB/s,Result,From(used),To(used)\n";
  memory_type_t from_type[] = { type_dma, type_spiram, type_progmem };
  memory_type_t to_type[] = { type_display, type_dma, type_spiram};
  int color_depth[] = {16};//{8, 16};
  int wh[][2] = {{24,24}, {48,48}, {96,96}, {160,120}, {200,160}, {240,240}, {320,240}};
  for (int s=0; s<array_length(wh); s++) {
    for (int f=0; f<array_length(from_type); f++) {
      for (int t=0; t<array_length(to_type); t++) {
        for (int d=0; d<array_length(color_depth); d++) {
          int width = wh[s][0];
          int height = wh[s][1];
          size_t size = width * height * (color_depth[d] / 8);
          spf("TEST from=%s, to=%s, color_depth=%d, size=%zu (%d,%d)\n", 
            memory_type_str[from_type[f]].c_str(), memory_type_str[to_type[t]].c_str(), 
            color_depth[d], size, width, height);
          test_pushSprite_sub(from_type[f], to_type[t], color_depth[d], width, height);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------------------------

// メイン
void loop() {
  M5.update();

  // 確保したメモリがどの領域に作成されたのかを調査する
//  test_memory_region();

  // mallocで指定できるメモリが何バイト単位なのかを検証する
  // test_memory_block_size();

  // sp("\n\n## Canvasメモリ消費量のテスト");
  // test_memory_usage_main();

  // debug_free_memory("before");

  // メモリコピーの時間計測
//  test_memory_copy_main();

  // メモリキャッシュの容量を検証する
  // 51456以降でpanicが発生し、その後も断続的に起こる。やむを得ず途中までにする
  test_memory_cache_size(INIT_FROM_SAME, 512, 49152, 512); // キャッシュの影響あり（同じ値の繰り返し）
  test_memory_cache_size(INIT_FROM_DIFF, 4096, 40960, 4096); // キャッシュの影響なし（ランダム値＆キャッシュクリア）
sp(csv);
delay(999999);

  // canvas.createSprite()するサイズで使用するメモリの場所が変わることを検証する
  test_createSprite_size();

  // pushSprite()の時間計測 - Transparentなし
  option = 0;
  test_pushSprite_main();

  // pushSprite()の時間計測 - Transparentあり
  option = 1;
  test_pushSprite_main();
  option = 0;

  debug_free_memory("Finish");

  // 終了
  sp("\n\n## 終了!!\n");
  sp(csv);
  while (true) delay(1000);
}
