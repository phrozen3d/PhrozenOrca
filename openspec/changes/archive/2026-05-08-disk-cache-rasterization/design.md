# Design: SLA 光柵化磁碟快取

## 架構總覽

```
═══════════════ 切片階段（slapsRasterize）═══════════════

SLAPrint::Steps::rasterize()
│
│  [既有] 計算並儲存 SLARasterParams → m_print->m_raster_params
│
│  [新增] 建立快取鍵 Hash → cache_dir
│
├─ 啟動 Producer std::thread  ←─ 繼承自 optimize-prz-export
│    └─ for each BATCH (8 layers):
│         [A] 讀取 m_printer_input[lid].transformed_slices()
│         [B] TBB parallel_for: expolygons_to_cvmat × 8
│         [C] queue.push(std::move(batch_mats))
│
└─ Consumer loop（rasterize() 主執行緒）:
     [D] batch = queue.pop()
     [E] for each layer:
           RLE encode → 寫入 cache_dir/layer_XXXX.prz_rle
           mat.release()
     [F] throw_if_canceled() + report_status(progress)
     └─ 直到 sentinel 或取消

═══════════════ 匯出階段（generate_prz）═══════════════

generate_prz()
│
├─ 計算 cache_hash → cache_dir
│
├─ [Cache Hit] manifest 驗證通過？
│    └─ for each layer (0..N-1):
│         prz_layer_content() → out.write()    ← 輕量，不需快取
│         rle_bytes = read_file(layer_XXXX)
│         write_be(rle_bytes.size()); out.write(rle_bytes)
│         out.put('\r'); out.put('\n')
│         ctl.update_status(pct)
│         [若最後層] write 11-byte end tag
│    預計總時間：5–15 秒（純磁碟 IO）
│
└─ [Cache Miss / 驗證失敗]
     Fallback：現有 Pipeline 光柵化邏輯（不變，~121 秒）
```

---

## Goals / Non-Goals

**Goals:**
- Cache Hit 時匯出時間 5–15 秒（目標接近 Python web_slicer_core 的 20 秒）
- 記憶體峰值維持 ≤ 552 MB（QUEUE_DEPTH=2 × 8 × 23 MB + 1 批次計算中）
- `slapsRasterize` 完成後零 cv::Mat 駐留記憶體（全釋放）
- 切片期進度條（report_status）持續更新，避免 UI 凍結感
- Cache Miss 時 Fallback 路徑完整保留現有功能

**Non-Goals:**
- 不修改 `expolygons_to_cvmat()` 核心
- 不修改 `ExportPRZJob` 公開 API
- 不在 `generate_prz()` fallback 路徑複製 Pipeline（沿用現有代碼）
- 不實作 LRU（7 天 TTL 清理已足夠）

---

## Decisions

### D1：引擎搬移——Pipeline 從 generate_prz() 遷移至 rasterize()

**做法：**
將 `optimize-prz-export` 實作的 `std::thread` Producer + Consumer queue 引擎，
從 `PhrozenPRZ.cpp::generate_prz()` 搬至 `SLAPrintSteps.cpp::rasterize()`，
Consumer 的輸出目標從 `std::ofstream out`（PRZ 串流）改為磁碟快取目錄。

**關鍵差異：**

| 面向 | optimize-prz-export 版本 | disk-cache-rasterization 版本 |
|------|--------------------------|-------------------------------|
| 執行時機 | 使用者點「匯出」時 | 使用者完成切片時 |
| Consumer 輸出 | 寫入 PRZ 輸出串流 | 寫入 Temp 快取檔案 |
| 取消信號來源 | `ExportPRZJob::ctl.was_cancelled()` | `SLAPrint::Steps::canceled()` |
| 進度回報 | `ctl.update_status()` | `report_status()` |
| 輸入資料 | `m_print->m_printer_input` | 同左（直接讀取） |

**繼承不變的部分：**
- `BATCH_SZ = 8`、`QUEUE_DEPTH = 2`（相同記憶體邊界）
- `std::queue<Batch>` + `std::mutex` + `std::condition_variable`（相同同步原語）
- `tbb::task_arena(max_concurrency())` 全核心 TBB（相同）
- `atomic<bool> cancelled` + `drain_queue` + `producer_thread.join()`（相同取消安全）
- `std::exception_ptr producer_exception`（相同例外傳遞）
- 每層 `mat.release()`（相同記憶體管理）

---

### D2：快取鍵（Cache Key Hash）設計

快取鍵必須在以下任一輸入改變時失效：
1. 模型幾何（ExPolygons 資料）
2. 光柵化參數（解析度、像素尺寸、方向、抗鋸齒等）
3. 快取格式版本（代碼更新後需重新計算）

**做法：**

```
hash_inputs:
  1. SLARasterParams 序列化（width, height, pixel_size_x/y, orientation,
                              mirror_x/y, gamma, aa_steps, gray_lo/hi,
                              blur_pixel, picture_grayscale）
  2. 各層 ExPolygons 資料的累積雜湊
     （遍歷 m_printer_input 每層 transformed_slices() 序列化）
  3. CACHE_VERSION 常數（int，初始值 = 1）

hash_algorithm: xxHash64（快速、確定性）
cache_dir: GetTempDirectory() / "phrozen_sla_cache" / hex(hash)[0:16] /
```

**為何不用 SHA256：** xxHash64 對大資料（360 層 × ExPolygons）速度遠快於 SHA256，
碰撞機率對此場景已充分低（2^-64 per pair）。

**Hash 計算時機：** 在 `rasterize()` 開始時計算一次，結果存入 `m_print->m_raster_cache_key`（新增欄位）。
`generate_prz()` 直接讀取此欄位，無需重新計算。

**快取失效規則：** `slapsRasterize` 步驟由 `SLAPrint` 的標準步驟失效機制觸發（輸入參數或幾何改變時）。
每次 `rasterize()` 執行就是一次完整的快取重建；舊 hash 目錄由清理機制處理。

---

### D3：快取目錄與檔案佈局

```
%TEMP%\phrozen_sla_cache\
│
└─ <hash_16chars>\              ← e.g., "a3f1b2c4d5e60789"
    ├─ manifest.json            ← 驗證元資料
    ├─ layer_0000.prz_rle       ← layer 0 的 RLE bytes（即 przByte 字串內容）
    ├─ layer_0001.prz_rle
    ├─ ...
    └─ layer_0359.prz_rle       ← layer N-1
```

**manifest.json 格式：**

```json
{
  "version": 1,
  "layer_count": 360,
  "cache_key": "a3f1b2c4d5e60789",
  "created_at": "2026-05-08T14:30:00Z",
  "raster_params": {
    "width": 11520, "height": 6480,
    "pixel_size_x": 0.04826, "pixel_size_y": 0.04826
  }
}
```

**layer_XXXX.prz_rle 內容：** 僅儲存 `przByte` 字串的原始位元組
（即 `0x55` 標頭 + RLE encoded 資料 + checksum，不含 4-byte 長度前綴和 CRLF）。
`generate_prz()` 讀取後自行加上長度前綴和 CRLF。

**選擇此格式而非完整 PRZ layer 資料的原因：**
`prz_layer_content()` 包含每層的 PRZ 元資料（層號、曝光時間等），這些參數可能在不改變幾何的情況下被使用者調整，
若把它也快取起來，就需要更複雜的失效邏輯。分開儲存讓 RLE 快取只捕捉光柵化結果，元資料在匯出時實時生成。

---

### D4：Cache Hit 驗證邏輯（generate_prz 端）

```cpp
// generate_prz() 開始時
std::string cache_key = m_print->m_raster_cache_key;   // 由 rasterize() 寫入
fs::path cache_dir = GetTempDir() / "phrozen_sla_cache" / cache_key;

if (!cache_key.empty() && SLARasterCache::is_valid(cache_dir, N)) {
    // Cache Hit 路徑
    SLARasterCache::read_and_write_prz(cache_dir, N, print, cfg, out, ctl);
    return;
}

// Cache Miss：執行現有 Pipeline 光柵化（不變）
```

**`is_valid()` 檢查：**
1. `cache_dir` 目錄存在
2. `manifest.json` 存在且 `layer_count == N`
3. 所有 `layer_0000.prz_rle` 到 `layer_{N-1:04}.prz_rle` 存在且非零

**Fallback 觸發條件：**
- `m_raster_cache_key` 為空（切片未完成或舊版流程）
- manifest 不存在或 layer_count 不符
- 任何快取檔案缺失或讀取失敗（優雅降級，記錄 warning 後 fallback）

---

### D5：記憶體安全——繼承 sla-on-demand-rasterization 精神

| 時間點 | cv::Mat 狀態 |
|--------|-------------|
| `rasterize()` 執行中 | 最多 `(QUEUE_DEPTH + 1) × BATCH_SZ = 24` 個 Mat，峰值 ≈ 552 MB |
| `rasterize()` 完成後 | 所有 Mat 已 `mat.release()`，記憶體峰值歸零 |
| `generate_prz()` Cache Hit | 無 cv::Mat，純磁碟讀取 |
| `generate_prz()` Cache Miss | 執行現有 Pipeline，峰值 ≈ 552 MB（與現況相同） |

**不變式：**
- Producer 在 `queue.push(std::move(batch))` 後不存取任何 Mat
- Consumer 在每層 `mat.release()` 後不存取該 Mat
- `drain_queue()` 確保取消/例外路徑中所有 Mat 被釋放

---

### D6：取消安全（rasterize() 端）

`SLAPrint::Steps` 提供 `canceled()` 和 `throw_if_canceled()`（與其他步驟相同）。

```cpp
// Consumer 每批次結束後
if (canceled()) {
    cancelled.store(true);      // 通知 Producer 停止
    queue_cv_not_full.notify_all();
    drain_queue();
    producer_thread.join();
    return;                     // rasterize() 正常返回（SLAPrint 框架處理取消狀態）
}
```

**與 optimize-prz-export 版本的差異：**
- 不使用 `ctl.was_cancelled()`，改用 `canceled()`
- 取消後直接 `return`（不 throw），符合 SLAPrintSteps 的慣例
  （`SLAPrint` 框架在步驟返回後會檢查取消狀態）

---

### D7：切片期進度條（UI 更新）

`rasterize()` 是 `SLAPrintStep::slapsRasterize` 的實作，進度回報透過 `report_status()`。

```cpp
// Consumer 每批次 [F] 節點
int pct = static_cast<int>(batch_end * 100 / N);
report_status(pct, L("Rasterizing layers..."));
// PRINT_STEP_LEVELS[slapsRasterize] = 90（目前設定）
// 匯出前的切片進度條顯示「Rasterizing layers... X%」
```

**節流：** 每 `BATCH_SZ = 8` 層呼叫一次（360 層 = 45 次），不會造成 UI event queue 擁塞。

**進度條範圍：** `slapsRasterize` 佔整體切片進度的 90 級（PRINT_STEP_LEVELS 中最大的步驟），
現有進度條架構不需改動。

---

### D8：快取清理機制

**策略 A（每次 rasterize() 執行）：**
寫入新快取前，掃描 `phrozen_sla_cache/` 子目錄，刪除所有 hash 不符且建立時間超過 24 小時的目錄。
（保留 24 小時內的舊快取以防止並行匯出衝突）

**策略 B（應用程式啟動時）：**
在應用程式啟動時（或首次 `rasterize()` 呼叫時），
掃描並刪除所有超過 **7 天**的快取目錄。

**實作：**
```cpp
// SLARasterCache::cleanup_old(max_age_days = 7)
// 掃描 GetTempDir() / "phrozen_sla_cache" / *
// 刪除 manifest.json 中 created_at 超過 max_age_days 的目錄
```

**不實作 LRU：** 典型使用場景（單一模型反覆切片+匯出）中，
7 天 TTL + 每次 rasterize 主動清理已能控制磁碟用量在合理範圍內。

---

### D9：新模組 SLARasterCache

為避免 `SLAPrintSteps.cpp` 和 `PhrozenPRZ.cpp` 之間直接依賴，
引入一個輕量模組封裝快取邏輯：

```cpp
// src/libslic3r/SLA/RasterCache.hpp
namespace sla {

struct RasterCacheKey {
    std::string hash;       // 16-char hex
    fs::path    dir;        // Temp / phrozen_sla_cache / hash
};

class RasterCache {
public:
    // 計算快取鍵（由 rasterize() 呼叫）
    static RasterCacheKey compute_key(
        const SLARasterParams &rp,
        const SLAPrint::PrinterInput &printer_input);

    // 寫入單層 RLE bytes（由 rasterize() Consumer 呼叫）
    static void write_layer(const RasterCacheKey &key, size_t lid,
                            const std::string &prz_rle_bytes);

    // 寫入 manifest（rasterize() 完成後呼叫）
    static void write_manifest(const RasterCacheKey &key, size_t layer_count,
                               const SLARasterParams &rp);

    // 驗證快取完整性（由 generate_prz() 呼叫）
    static bool is_valid(const RasterCacheKey &key, size_t expected_layers);

    // 讀取單層 RLE bytes（由 generate_prz() 呼叫）
    static std::string read_layer(const RasterCacheKey &key, size_t lid);

    // 清理舊快取（由 rasterize() 或啟動時呼叫）
    static void cleanup_old(int max_age_days = 7);
};

} // namespace sla
```

**為何獨立模組：**
- `SLAPrintSteps.cpp` 不需 include `PhrozenPRZ.cpp` 相關標頭
- `PhrozenPRZ.cpp` 不需 include `SLAPrintSteps.cpp`
- 快取邏輯集中於一處，易於測試和維護

---

### D10：generate_prz() 中 cache_key 的取得方式

`rasterize()` 完成後，將 hash 寫入 `m_print->m_raster_cache_key`（`std::string` 型，
新增於 `SLAPrint.hpp` 的 `SLAPrint` 成員）。
`generate_prz()` 透過 `print.m_raster_cache_key` 讀取，無需重新計算 hash。

```cpp
// SLAPrint.hpp（新增）
class SLAPrint {
    ...
    std::string m_raster_cache_key;  // 由 slapsRasterize 步驟寫入
    friend class Steps;
    friend void generate_prz(...);   // 需要 friend 或 getter
};
```

---

## Risks / Trade-offs

| 風險 | 影響 | 緩解策略 |
|------|------|----------|
| 快取一致性失效（Hash 碰撞） | 匯出錯誤 PRZ（嚴重） | xxHash64 碰撞率極低；manifest 驗證 layer_count 作為二次確認 |
| Hash 計算本身耗時（大模型 ExPolygons 序列化） | 切片開始延遲 | 量測確認；若超過 1s 則改用近似 hash（面積總和 + 點數） |
| 磁碟空間不足（寫入失敗） | rasterize() 中途失敗 | 捕捉 IO 例外 → 記錄 warning → fallback（不中斷切片，fallback 到 generate_prz() 重新光柵化） |
| 快取目錄被外部程式刪除（讀取失敗） | generate_prz() cache hit 失敗 | 讀取失敗 → catch → fallback to Pipeline（優雅降級） |
| 切片 +100s 使用者體驗退步 | 使用者感知切片變慢 | 進度條明確顯示「Rasterizing layers...」；首次匯出顯著加速補償體驗 |
| 並行匯出衝突（同時兩個 ExportPRZJob 讀同一快取） | 讀取競爭（無寫入競爭） | 讀取為唯讀操作，多個 reader 安全；cleanup 不在匯出期執行 |
| 快取目錄含未完成的寫入（意外崩潰留下孤兒） | 下次 generate_prz() 讀取不完整資料 | `is_valid()` 驗證所有 layer 檔案存在 + 非零大小；失敗則 fallback |

---

## 效能預測

| 操作 | 耗時（11K×6K，360 層） | 說明 |
|------|----------------------|------|
| Hash 計算 | ~0.5–2s | ExPolygons 序列化 + xxHash64 |
| 切片期光柵化（rasterize Pipeline） | ~100s | 與現有 generate_prz() Pipeline 相同 |
| 切片期 RLE + Disk Write | ~15s | 順序寫入 ~100 MB 快取（SSD） |
| 匯出期 Cache Hit 讀取 | ~5–15s | 順序讀取 ~100 MB + prz_layer_content（輕量） |
| 匯出期 Cache Miss Fallback | ~121s | 現有 Pipeline，不變 |

**總切片時間增加：** ~115s（首次切片後，每次匯出節省 ~106s）

---

## 關鍵檔案

- [src/libslic3r/SLAPrintSteps.cpp](src/libslic3r/SLAPrintSteps.cpp) — `rasterize()`（注入點，line ~1380）
- [src/libslic3r/SLAPrint.hpp](src/libslic3r/SLAPrint.hpp) — 新增 `m_raster_cache_key` 成員
- [src/libslic3r/Format/PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) — `generate_prz()`（Cache Hit/Miss 路徑）
- [src/libslic3r/SLA/RasterCache.hpp](src/libslic3r/SLA/RasterCache.hpp) — 新增模組（快取鍵、讀寫、清理）
- [src/libslic3r/SLA/RasterCache.cpp](src/libslic3r/SLA/RasterCache.cpp) — 新增模組實作

## Open Questions — 調查結論（2026-05-08）

### OQ1：Hash 計算效能 → 使用 mz_crc32（已內建）

**調查結果**：
- xxHash64 **未納入** deps，引入需要新的依賴項
- `mz_crc32` 已透過 `<miniz.h>` 提供（`SLA/RasterBase.cpp`、`Format/bbs_3mf.cpp` 均使用）
- 資料量估算：360 層 × ~500 pts × 8 bytes ≈ 1.4 MB；mz_crc32 約 1 GB/s → **~1.4 ms**，可忽略
- 最壞情形（高密度模型，~5000 pts/層）：~14 MB → **~14 ms**，仍可接受

**決策**：使用 `mz_crc32(uint32_t crc, const unsigned char* buf, mz_ulong len)` 鏈式累積
（iterate over all `PrintLayer::transformed_slices()` 點資料 + `SLARasterParams` raw bytes + CACHE_VERSION = 1）。
結果為 uint32 → 8-char hex 目錄名稱。無新依賴項。

**額外發現（早期返回優化）**：
`steps_rasterize` 中的參數（`exposure_time`、`display_pixels_x` 等）變更會觸發 `slapsRasterize` 重新執行，
但這些參數**不影響**圖像內容，不納入 hash。因此 `rasterize()` 開頭可優先做：

```cpp
auto key = RasterCache::compute_key(rp, printer_input);
if (RasterCache::is_valid(key, N)) {
    m_raster_cache_key = key.hash;  // 直接重用，跳過 Pipeline
    return;
}
```

這使「只改曝光時間」的重切片場景從 +100s 縮短至幾乎瞬間完成。

---

### OQ2：Temp 目錄路徑 → `boost::filesystem::temp_directory_path()`

**調查結果**：
- `boost::filesystem::temp_directory_path()` 已被多個 libslic3r 模組使用
  （`GUI_App.cpp`、`UpgradeNetworkJob.cpp`、`FixModelByWin10.cpp` 等）
- `libslic3r` 層本身可直接使用 Boost（已有依賴）
- 跨平台行為：Windows → `%TEMP%`，macOS/Linux → `/tmp`

**決策**：使用 `boost::filesystem::temp_directory_path() / "phrozen_sla_cache"`，
初始版本不提供使用者自訂快取路徑選項（YAGNI）。

---

### OQ3：`prz_layer_content()` 依賴項 → 已解決，無依賴問題

**結論**：Cache 只儲存 RLE bytes（`przByte` 字串內容），不儲存 layer content。
`prz_layer_content()` 在 `PhrozenPRZ.cpp::generate_prz()` 中維持原位，
匯出時實時生成，無需跨模組共用。

---

### OQ4：`m_raster_cache_key` Thread Safety → 無 race condition

**調查結果**：
- `ExportPRZJob` 儲存 `const SLAPrint &m_print`（reference to the same object）
- `rasterize()` 寫入 `m_raster_cache_key` → `set_done(slapsRasterize)` → UI 通知切片完成
- 使用者手動點擊匯出 → `ExportPRZJob` 啟動 → 讀取 `m_raster_cache_key`
- 這是**嚴格的 happens-before 關係**（user action 為同步邊界）

**決策**：無需額外鎖。此假設與現有 `m_raster_params` 的使用模式完全一致
（`generate_prz()` 也讀取 `m_raster_params`，同樣無鎖）。

---

### generate_prz() Fallback 路徑澄清

D1 中「Cache Miss fallback = 現有 Pipeline（~121s）」的表述需更新：

遷移後 `generate_prz()` 的程式碼結構為：
1. **Cache Hit 路徑**（新增）：讀磁碟 → 封裝 PRZ → early return
2. **Cache Miss / read 失敗 fallback**：保留現有 `generate_prz()` Pipeline 原始程式碼（**不刪除**）

亦即 `generate_prz()` 的 Pipeline 程式碼（從 `optimize-prz-export` 繼承）**保持不動**，
只在最前段插入 Cache Hit 早期返回邏輯。這是最小侵入性的做法，fallback 行為與現況完全一致（~121s）。