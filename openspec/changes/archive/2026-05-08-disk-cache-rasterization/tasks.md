# Tasks: disk-cache-rasterization

## Phase 1：基礎設施建立

### 1. 在 `SLAPrint.hpp` / `SLAPrint.cpp` 新增 `m_raster_cache_key`

- [x] 1.1 在 `SLAPrint` 類別中新增 `std::string m_raster_cache_key`（private 成員）
- [x] 1.2 新增 `const std::string& raster_cache_key() const` getter（供 `generate_prz()` 讀取）
- [x] 1.3 在 `SLAPrint::invalidate_step(slapsRasterize)` 中清空 `m_raster_cache_key = ""`
      （確保 Cache 在 step 失效時同步失效，位置：`SLAPrint.cpp` line ~821）

---

### 2. 建立 `src/libslic3r/SLA/RasterCache.hpp`

定義以下介面（全靜態方法，無全域狀態）：

```cpp
namespace sla {

struct RasterCacheKey {
    std::string             hash;  // 8-char hex (32-bit CRC)
    boost::filesystem::path dir;   // GetTempDir() / "phrozen_sla_cache" / hash
};

class RasterCache {
public:
    // Hash = mz_crc32 over (all layers ExPolygons raw bytes + SLARasterParams bytes + CACHE_VERSION)
    static RasterCacheKey compute_key(
        const SLARasterParams            &rp,
        const std::vector<PrintLayer>    &printer_input);

    // 寫入單層 RLE bytes（przByte 原始內容，不含長度前綴和 CRLF）
    // 若目錄不存在則建立；以原子方式寫入（先寫 .tmp 再 rename）
    static void write_layer(const RasterCacheKey &key, size_t lid,
                            const std::string    &prz_rle_bytes);

    // 全部層寫入後呼叫：寫入 manifest 檔案
    static void finalize(const RasterCacheKey &key, size_t layer_count,
                         const SLARasterParams &rp);

    // 驗證：manifest 存在 + layer_count 符合 + 所有層檔案存在且非零
    static bool is_valid(const RasterCacheKey &key, size_t expected_layers);

    // 讀取單層 RLE bytes；失敗時拋出 std::runtime_error
    static std::string read_layer(const RasterCacheKey &key, size_t lid);

    // 清理舊快取：刪除超過 max_age_days 天的子目錄
    static void cleanup_old(int max_age_days = 7);

private:
    static boost::filesystem::path base_dir();  // temp_directory_path() / "phrozen_sla_cache"
};

} // namespace sla
```

- [x] 2.1 建立 `RasterCacheKey` struct
- [x] 2.2 宣告 `RasterCache` 所有靜態方法
- [x] 2.3 加入必要 `#include`（`<boost/filesystem.hpp>`、`SLAPrint.hpp`）

---

### 3. 實作 `src/libslic3r/SLA/RasterCache.cpp`

- [x] 3.1 實作 `base_dir()`：`boost::filesystem::temp_directory_path() / "phrozen_sla_cache"`

- [x] 3.2 實作 `compute_key()`：
  - 使用 `mz_crc32`（via `#include <miniz.h>`，已是現有 dep）
  - 鏈式計算：所有層 ExPolygons 點資料 raw bytes → SLARasterParams raw bytes → CACHE_VERSION (= 1)
  - 回傳 `{hex8(crc32), base_dir() / hex8(crc32)}`

- [x] 3.3 實作 `write_layer()`：
  - 確保 `key.dir` 目錄存在
  - 檔名：`layer_{lid:04d}.prz_rle`
  - 先寫 `.tmp` 再 `rename`（原子寫入，防止半完成快取被誤認為有效）

- [x] 3.4 實作 `finalize()`：
  - 寫入 `key.dir / "manifest.txt"` 格式：
    ```
    version=1
    layer_count=<N>
    ```
  - 簡單文字格式，不引入 JSON 依賴

- [x] 3.5 實作 `is_valid()`：
  - 讀取 manifest，驗證 `layer_count == expected_layers`
  - 迴圈確認所有 `layer_XXXX.prz_rle` 存在且 `file_size > 0`
  - 任何失敗返回 `false`（不拋出）

- [x] 3.6 實作 `read_layer()`：
  - 讀取 `key.dir / layer_{lid:04d}.prz_rle` 全部內容並回傳
  - 失敗時拋出 `std::runtime_error`

- [x] 3.7 實作 `cleanup_old()`：
  - 遍歷 `base_dir()` 的子目錄
  - 讀取各目錄的 manifest（若存在），比對寫入時間
  - 或使用 `boost::filesystem::last_write_time` 作為年齡估算
  - 超過 `max_age_days` 則整個目錄 `remove_all`

---

### 4. 更新 CMakeLists.txt

- [x] 4.1 在 `src/libslic3r/CMakeLists.txt` 的 libslic3r sources 中加入 `SLA/RasterCache.cpp`

---

## Phase 2：切片期 Pipeline 注入（slapsRasterize）

### 5. 改造 `SLAPrintSteps.cpp::rasterize()`

目標：在現有 SLARasterParams 計算後，注入 Pipeline 引擎將光柵化結果寫入磁碟快取。

- [x] 5.1 在 `rasterize()` 末尾（SLARasterParams 存入 `m_print->m_raster_params` 之後）
      計算 cache key：
      ```cpp
      const auto cache_key = sla::RasterCache::compute_key(rp, m_print->m_printer_input);
      ```

- [x] 5.2 **早期返回優化**：若 cache 已有效則跳過 Pipeline：
      ```cpp
      const size_t N = m_print->m_printer_input.size();
      if (N == 0) { m_print->m_raster_cache_key = ""; return; }
      if (sla::RasterCache::is_valid(cache_key, N)) {
          m_print->m_raster_cache_key = cache_key.hash;
          report_status(100, L("Rasterizing layers..."));
          return;
      }
      ```
      此優化使「只改曝光時間等 PRZ metadata」的場景下，
      `slapsRasterize` 幾乎瞬間完成（不重跑光柵化）。

- [x] 5.3 呼叫 `sla::RasterCache::cleanup_old()` 清理過期快取（在 Pipeline 開始前）

- [x] 5.4 宣告 Pipeline 同步原語（與 `generate_prz()` 相同機制）

- [x] 5.5 實作 Producer lambda（與 `generate_prz()` 幾乎相同）

- [x] 5.6 實作 Consumer loop（差異：輸出目標為磁碟，非 PRZ stream）

- [x] 5.7 Pipeline 正常完成後：
      ```cpp
      sla::RasterCache::finalize(cache_key, N, rp);
      m_print->m_raster_cache_key = cache_key.hash;
      ```

- [x] 5.8 新增必要 `#include`：`"SLA/RasterCache.hpp"`、`<opencv2/core.hpp>`、
      `<tbb/...>`、`<thread>`、`<mutex>`、`<condition_variable>`、`<queue>`、`<atomic>`

---

## Phase 3：匯出期快取讀取（generate_prz）

### 6. 在 `generate_prz()` 最前段加入 Cache Hit 路徑

位置：`src/libslic3r/Format/PhrozenPRZ.cpp`，`generate_prz()` 函式起頭

- [x] 6.1 讀取 cache key：
      ```cpp
      const std::string cache_hash = print.raster_cache_key();
      ```

- [x] 6.2 若 `!cache_hash.empty()` 則建立 key 並驗證：
      ```cpp
      sla::RasterCacheKey cache_key;
      cache_key.hash = cache_hash;
      cache_key.dir  = sla::RasterCache::base_dir_for(cache_hash);  // 或等效方式
      ```

- [x] 6.3 若 `sla::RasterCache::is_valid(cache_key, N)` 為 true：
      - 進入 Cache Hit 迴圈（每層）：
        ```
        prz_layer_content(lc, ...) → out.write(lc)
        rle_bytes = RasterCache::read_layer(cache_key, lid)    // 可能拋出
        write_be(out, rle_bytes.size())
        out.write(rle_bytes)
        out.put('\r'); out.put('\n')
        if (lid == N-1) write end tag
        ctl.update_status(pct)
        if (!ctl(pct)) return  // cancelled
        ```
      - 早期 `return`

- [x] 6.4 read_layer 例外處理：
      ```cpp
      } catch (const std::exception &e) {
          BOOST_LOG_TRIVIAL(warning) << "Cache read failed: " << e.what()
                                    << " — falling back to rasterization";
          // fall through to existing Pipeline code
      }
      ```

- [x] 6.5 Cache Miss / fallback：現有 Pipeline 程式碼**原封不動**保留（不刪除）

---

## Phase 4：清理與收尾

### 7. 應用程式啟動時清理過期快取

- [x] 7.1 在適當的應用程式啟動位置（例如 `BackgroundSlicingProcess` 初始化或
      `SLAPrint::process()` 首次呼叫前）加入：
      ```cpp
      sla::RasterCache::cleanup_old(7);  // 7 天 TTL
      ```
      **注意**：`cleanup_old` 必須不阻塞，且對磁碟錯誤靜默容忍（try/catch 全部）

---

### 8. 功能驗證 Checklist（手動）

- [x] 8.1 切片後首次匯出：確認匯出時間落在 5–15 秒（11K×6K 360 層）
- [x] 8.2 連續兩次匯出（不重新切片）：確認第二次同樣快（Cache Hit）
- [x] 8.3 更改曝光時間（不重新切片）→ 重新切片：
      確認 `slapsRasterize` 幾乎瞬間完成（早期返回）且匯出仍為 Cache Hit
- [x] 8.4 更改模型幾何 → 重新切片 → 匯出：
      確認新快取建立，匯出仍為 5–15 秒
- [x] 8.5 手動刪除快取目錄 → 匯出：確認 Cache Miss fallback 正常執行（~121s，無 crash）
- [x] 8.6 切片過程中取消：確認無 cv::Mat 記憶體洩漏，無殘留快取目錄
- [x] 8.7 快取目錄磁碟空間：確認 360 層模型快取 < 200 MB
- [x] 8.8 進度條：確認切片期顯示「Rasterizing layers... XX%」逐步更新

---

## Phase 5：UX 打磨（進度條平滑化 + 防抖預覽）

### 9. 進度條平滑化（`SLAPrintSteps.cpp`）

目標：讓 Consumer 在等待 Producer 光柵化時也能定時回報進度，消除 ~55% 到 100% 的視覺跳躍。

- [x] 9.1 在 Pipeline 同步原語宣告區（`cancelled` 之後）加入：
      ```cpp
      std::atomic<size_t> producer_rasterized{0};
      ```

- [x] 9.2 在 TBB `parallel_for` lambda 內部每層完成後加入（移至迴圈內，從批次外移到層內）：
      ```cpp
      producer_rasterized.fetch_add(1, std::memory_order_relaxed);
      ```
      （原本的 `fetch_add(batch_n)` 在批次外，底層實心層整批耗時 40s 仍會凍結；改為每層完成即更新）

- [x] 9.3 將 Consumer 的 `queue_cv_not_empty.wait(...)` 替換為帶超時的 `wait_for` 迴圈：
      ```cpp
      while (batch_queue.empty() && !producer_done) {
          if (queue_cv_not_empty.wait_for(lk, std::chrono::milliseconds(100),
                  [&] { return !batch_queue.empty() || producer_done; }))
              break;
          const size_t rast = producer_rasterized.load(std::memory_order_relaxed);
          lk.unlock();
          report_status(static_cast<int>(rast * 100 / N), L("Rasterizing layers..."));
          lk.lock();
      }
      ```

- [x] 9.4 在 `SLAPrintSteps.cpp` includes 中確認 `<chrono>` 已引入

---

### 10. 防抖層預覽（`SLASlice2DCanvas.hpp` + `.cpp`）

目標：層切換時強制向量繪製，停止操作 200ms 後再切換為高畫質光柵預覽，消除拖曳卡頓。

- [x] 10.1 在 `SLASlice2DCanvas.hpp` 中加入前向宣告：
      ```cpp
      class wxTimer;
      class wxTimerEvent;
      ```

- [x] 10.2 在 `SLASlice2DCanvas.hpp` 私有成員中加入：
      ```cpp
      std::unique_ptr<wxTimer> m_hq_timer;
      bool m_layer_interactive{ false };
      ```
      並宣告 `void on_hq_timer(wxTimerEvent&);`

- [x] 10.3 在 `SLASlice2DCanvas.cpp` 建構子末尾初始化 timer：
      ```cpp
      m_hq_timer = std::make_unique<wxTimer>();
      m_hq_timer->SetOwner(this);
      Bind(wxEVT_TIMER, &SLASlice2DCanvas::on_hq_timer, this);
      ```

- [x] 10.4 修改 `set_view_layer_index()`：層索引改變時啟動 timer
      ```cpp
      if (std::max(0, idx) != m_layer_idx) {
          m_layer_interactive = true;
          if (m_hq_timer) m_hq_timer->StartOnce(200);
      }
      ```

- [x] 10.5 新增 `on_hq_timer()` 實作：
      ```cpp
      void SLASlice2DCanvas::on_hq_timer(wxTimerEvent&) {
          m_layer_interactive = false;
          if (m_gl != nullptr) m_gl->Refresh();
      }
      ```

- [x] 10.6 修改 `render()` 中 `slice_geometry_ready` 的分支，在高畫質路徑前插入防抖判斷：
      ```cpp
      } else if (m_layer_interactive) {
          render_vector_fallback(pw, ph);
      } else {
          // 原有高畫質 expolygons_to_cvmat 路徑（不變）
      }
      ```