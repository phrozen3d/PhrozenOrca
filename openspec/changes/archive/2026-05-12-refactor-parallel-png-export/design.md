## Context

現有 `SLAPrint::Steps::rasterize()` 採用 producer-consumer 架構（batch size 8、queue depth 2），producer 以 TBB 平行光柵化，consumer 單執行緒依序進行 RLE 編碼與磁碟寫入。Consumer 是瓶頸：即使 TBB 完成一批 8 層，下一批必須等 consumer 寫完才能繼續。實測 360 層 13320×5120 約 120s。

Prusa Slicer 採用 `execution::for_each(ALL layers)` 一次平行處理全部層，各執行緒獨立完成光柵化 + PNG 編碼後寫入磁碟，41s 完成。

本次重構 Revert 3 個引入舊架構的 commit，以完全平行的 PNG pipeline 重建。

---

## Goals / Non-Goals

**Goals:**

- `rasterize()` 步驟：全層完全平行（TBB parallel_for），各執行緒獨立 rasterize → stack blur → PNG 編碼 → 磁碟寫入，目標 ≤ 50s
- RasterCache 格式改為 `.png`（format-agnostic，可用 debug 工具直接檢視）
- 匯出路徑：批次並行（≤ 8 threads）從 PNG 快取解碼 → RLE 編碼 → 循序 append，記憶體峰值 ≤ 650 MB
- UI 全程非阻塞（`wxQueueEvent` 非同步投遞進度事件）
- `StatusReporter::m_st` 消除 data race

**Non-Goals:**

- 逐列串流 PNG 解碼（工程複雜度過高，捨棄）
- 更換 AGG 多邊形光柵器（現有實作與 Prusa 相同，不動）
- 更改 PRZ binary format spec（格式不變，只改「何時」轉換）
- 跨版本快取遷移工具（舊 `.prz_rle` 快取由 7 天 TTL cleanup 自然過期）

---

## Decisions

### D1：Revert 策略 — `git revert --no-commit` 三個 commit

採 `git revert --no-commit 8d3fd8d73 b559a5df8 6572a711f` 將三個 commit 一次 revert 到暫存區，手動刪除孤立的 `RleEncode.hpp`，再以單一新 commit 提交。

**捨棄方案**：逐 commit patch——PRZ-RLE 格式鎖死使平行演進不可行，patch 會留下大量殘留介面。

---

### D2：Rasterize 平行模型 — `tbb::parallel_for` 全層，無 producer-consumer

```
tbb::parallel_for(tbb::blocked_range<size_t>(0, N),
    [&](const tbb::blocked_range<size_t>& r) {
        for (size_t lid = r.begin(); lid < r.end(); ++lid) {
            // 各執行緒獨立：rasterize → blur → PNG encode → disk write
        }
    });
```

TBB 的 work-stealing scheduler 自動負載均衡。記憶體峰值：`max_concurrency × (cv::Mat ~68MB + PNG buffer ~3MB) ≈ 16 × 71MB ≈ 1.1GB`，各執行緒在 `MZ_FREE` 後立即釋放，不累積。

**捨棄方案**：保留 producer-consumer queue——根本原因是 serial consumer；queue 深度調整無法解決。

---

### D3：Blur 實作 — `agg::stack_blur_gray8` 取代 `cv::GaussianBlur`

```cpp
// 取代舊實作（RasterToCvMat.cpp lines 74-79）：
if (rp.blur_pixel >= 2) {
    agg::rendering_buffer rbuf(mat.data, mat.cols, mat.rows, mat.cols);
    agg::pixfmt_gray8 pixf(rbuf);
    agg::stack_blur_gray8(pixf, rp.blur_pixel, rp.blur_pixel);
}
```

Stack blur 為 O(radius) 分離式演算法，Gaussian 為 O(radius²) 卷積；13320×5120 下速度差距 3–5×。AGG 已為現有依賴（`agg/agg_blur.h`），無需新增。數學上非嚴格等效，但 Prusa Slicer 已於生產環境驗證品質可接受。

---

### D4：PNG 編碼 — `tdefl_write_image_to_png_file_in_memory`（miniz）

各執行緒在記憶體中編碼完整 PNG frame，再寫入磁碟。miniz 已為現有依賴（`RasterBase.cpp` 使用），無需新增。寫入採既有 RasterCache 的 temp-file + rename 原子模式。

**捨棄方案**：直接寫入最終路徑——rename 才能保證多執行緒並行寫入的原子性。

---

### D5：進度回報 — 無鎖 CAS throttle，避免 UI 事件洪流

TBB 全層完全平行下，360 個執行緒可能在 1ms 內全部完成，若每層都呼叫 `report_status()` 將對 `wxEventQueue` 狂轟炸。採以下無鎖 throttle：

```cpp
std::atomic<int> completed_layers{0};
std::atomic<int> last_reported_pct{-1};

// 在各執行緒 lambda 內：
int done = completed_layers.fetch_add(1, std::memory_order_relaxed) + 1;
int pct  = static_cast<int>(done * 100 / N);
int prev = last_reported_pct.load(std::memory_order_relaxed);
if (pct > prev &&
    last_reported_pct.compare_exchange_strong(prev, pct,
        std::memory_order_relaxed, std::memory_order_relaxed)) {
    report_status(pct, L("Rasterizing layers..."));
}
```

CAS 保證每個百分點最多有一個執行緒呼叫 `report_status()`，最多 100 次事件。`wxQueueEvent`（`Plater.cpp:3215`）本身執行緒安全，不需額外 mutex。

同步修正 `StatusReporter::m_st`：由 `double` 改為 `std::atomic<double>`，消除現有 data race。

---

### D6：Export Batching — `tbb::task_arena` 限制並行數至 8

PRZ 為純循序 append-only stream，無 per-layer offset table，不支援 random seek。Export 採分批策略：

```
EXPORT_BATCH = 8   // 對齊 TBB concurrency 上限

for batch_start in [0, N, step=EXPORT_BATCH]:
    batch_end = min(batch_start + EXPORT_BATCH, N)
    rle_results[0..batch_n]  ← parallel decode + encode（tbb::task_arena(8)）
    for i in [0..batch_n]:   ← 循序 append（保序）
        write_layer_to_prz(rle_results[i])
        rle_results[i].clear()  // 立即釋放
```

記憶體峰值：`8 × (PNG ~3MB + cv::Mat ~68MB + RLE ~5MB) ≈ 608MB`，在可接受範圍內。

使用 `tbb::task_arena arena(EXPORT_MAX_THREADS)` + `arena.execute([&]{ tbb::parallel_for(...) })` 限制並行數，不影響全域 TBB scheduler。

**捨棄方案A**：全部 N 層並行 decode——360 × 68MB ≈ 24GB，OOM。
**捨棄方案B**：pre-compute offsets 後 random seek——PRZ 格式不支援，stream 為 `std::ostream&`。

---

### D7：PNG 解碼（Export 路徑）— `cv::imdecode`

Export 步驟讀取 PNG bytes 後以 `cv::imdecode(buffer, cv::IMREAD_GRAYSCALE)` 解碼為 `cv::Mat`。OpenCV 已為現有依賴（`RasterToCvMat.cpp` 使用），無需新增。整幅解碼（非逐列串流）以換取實作簡單性，峰值由 D6 的 batch size 控制。

---

## Risks / Trade-offs

| 風險 | 緩解措施 |
|------|----------|
| N 個執行緒同時對同一目錄執行 temp+rename，NTFS 鎖競爭 | 各層檔名唯一（`layer_{lid:04d}.png`），rename 不衝突；SSD 上並行寫入性能良好。HDD 用戶可能略慢但不會出錯 |
| `tdefl_write_image_to_png_file_in_memory` 回傳 malloc buffer，需手動 `MZ_FREE` | 明確在寫入後立即呼叫，以 RAII wrapper 封裝確保例外安全 |
| Stack blur 輸出與 Gaussian blur 不完全相同，用戶感知差異 | Prusa Slicer 實務驗證；若有客訴，可在 `SLARasterParams` 加 `blur_mode` flag 讓用戶選擇（future work） |
| 快取格式從 `.prz_rle` 改為 `.png`，舊快取失效 | `CACHE_VERSION` bump 使舊 key 不命中；`cleanup_old(7)` 7 天後自動清理舊檔 |
| Export batch 若某層 PNG 讀取失敗（磁碟錯誤），整批結果不完整 | read_layer 失敗時拋出 exception，ExportPRZJob 捕捉後顯示錯誤訊息並中止，不產生損壞的 PRZ 檔 |

---

## Migration Plan

1. `git revert --no-commit 8d3fd8d73 b559a5df8 6572a711f`
2. 手動刪除 `src/libslic3r/SLA/RleEncode.hpp`
3. 重建 `RasterCache`（`.prz_rle` → `.png`，更新 `write_layer` / `read_layer` / `is_valid`，bump `CACHE_VERSION`）
4. 重寫 `rasterize()`（parallel_for + stack blur + miniz PNG + RasterCache write）
5. 修正 `StatusReporter::m_st` → `std::atomic<double>`
6. 重寫 `generate_prz()` 快取命中路徑（batch arena + cv::imdecode + RLE + 循序 append）
7. 刪除 `generate_prz()` 舊 fallback rasterize 路徑中已無用的 producer-consumer code
8. 建置：`build_release_vs2022.bat slicer`
9. 驗收（詳見 Verification）

**Rollback**：若驗收失敗，`git revert HEAD`（新 revert commit）可回到 revert 前狀態；不需 force push。

---

## Open Questions

- `tdefl_write_image_to_png_file_in_memory` 的壓縮等級預設為 MZ_DEFAULT_LEVEL（6）；是否需要調降至 MZ_BEST_SPEED（1）以換取更快的 encode 速度？待 benchmark 決定。
- 是否評估重用 PhrozenOrca 已移植的 SL1 格式輸出邏輯（若存在）作為 PNG 編碼管線的替代來源？目前直接使用 `RasterBase.cpp` 中已有的 `tdefl` wrapper，若 SL1 路徑有更完整的封裝可考慮統一。

---

## Known Limitations & Future Work（已知限制與未來工作）

### L1：Blur / gamma > 0 退回 AGG 效能陷阱

`cv::fillPoly` Fast-Path（`RasterToCvMat.cpp`）的觸發條件為：

```cpp
if (std::abs(gamma) < 1e-6 && blur_pixel == 0)
```

當使用者啟用 blur（`blur_pixel >= 2`）或設定 `gamma > 0`（啟用次像素抗鋸齒）時，Fast-Path 失效並退回 AGG 渲染引擎。在 13320×5120 解析度下，AGG 需對 ~68M 像素逐一執行次像素覆蓋率計算（`agg::render_scanlines`），導致嚴重的效能降級（預估切片時間回升至分鐘級）。

**目前狀態**：已知技術債，暫不處理。預設印表機設定 blur=0、gamma=0，99% 一般列印任務走 Fast-Path，效能不受影響。

**未來優化方向**（若有需求）：
1. **替換 AGG 光柵器**：以 CPU SIMD（AVX2）或 GPGPU 實作次像素覆蓋率計算，可在保留 AA 精度的前提下大幅提升吞吐量。
2. **GPU 加速**：將 ExPolygon → 像素緩衝的光柵化移至 GPU（OpenGL compute shader 或 CUDA），可同時平行處理多層。
3. **近似快徑**：對 blur 場景，考慮先以 cv::fillPoly 完成二值光柵化，再套用 `agg::stack_blur_gray8` 柔化邊緣，避免重啟完整 AGG 次像素引擎。此近似在視覺品質上與 AGG 完整路徑差異極小，但需使用者接受驗收。
