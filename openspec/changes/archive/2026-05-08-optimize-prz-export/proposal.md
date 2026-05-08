## Why

`[sla-on-demand-rasterization]` 將 PRZ 匯出改為批次並行（Batched On-Demand）模式：每批次 8 層 TBB 平行光柵化 → 循序 RLE 編碼 → 循序寫入磁碟 → 釋放記憶體 → 下一批次。記憶體峰值已從 ≥2.3 GB 降至 ≤200 MB。

然而，批次之間仍為**嚴格串列**：上一批次的 RLE 編碼與寫入完全結束後，下一批次的光柵化才開始。CPU 核心在循序編碼期間大量閒置，匯出總時間仍與層數成線性比例，對於 200~400 層的 Mega 8K 模型，總耗時極長。

## What Changes

- 將 `generate_prz()` 的批次串列流程改為**生產者-消費者 Pipeline**：光柵化執行緒（Producer）與 RLE 編碼+寫入執行緒（Consumer）並行執行，透過有界佇列（Bounded Queue）傳遞批次資料
- Producer 以 TBB 平行化批次內的 `expolygons_to_cvmat()`（與現有實作相同），完成後將批次推入佇列
- Consumer 從佇列取出批次，執行 RLE 編碼與串流寫入（保持序列順序），完成後呼叫 `cv::Mat::release()`
- Queue Depth 設為 2，確保記憶體峰值上限 ≈ `2 × BATCH_SZ × 23 MB ≤ 368 MB`，遠低於原始 2.3 GB
- 整個 Pipeline 繼續在 `ExportPRZJob` 的背景執行緒中執行，UI 主執行緒不受影響
- 選擇性整合方向 B（RLE 並行）：在 Consumer 側引入批次內的平行 RLE 編碼，以 output index 排序後再寫入，進一步縮短編碼等待時間

## Capabilities

### New Capabilities

- `prz-export-pipeline`：PRZ 匯出採生產者-消費者 Pipeline 架構，光柵化與 RLE 編碼並行執行，有界佇列確保記憶體上限

### Modified Capabilities

- `sla-on-demand-prz-export`（`[sla-on-demand-rasterization]`）：保留批次 on-demand 精神（不預先存儲全部影像），僅將批次間的串列改為 Pipeline

## 與 `[sla-on-demand-rasterization]` 精神的相容性聲明

`[sla-on-demand-rasterization]` 的核心約束是：**不在切片階段將全部切層影像常駐記憶體**。本次改動不違反此約束：

- 光柵化依然是「匯出時才做」（on-demand），不在 `slapsRasterize` 步驟預先計算
- 記憶體峰值從 ≤200 MB 略升至 ≤368 MB（Queue Depth 2），仍遠低於原始 2.3 GB
- `SLARasterParams` 快照機制完全保留，無需修改

## Impact

**主要修改檔案：**
- `src/libslic3r/Format/PhrozenPRZ.cpp` — 重寫 `generate_prz()` 的批次迴圈為 Producer-Consumer Pipeline
- `src/slic3r/GUI/Jobs/ExportPRZJob.cpp` — 若需調整進度回報機制

**不受影響：**
- `src/libslic3r/SLAPrint.hpp/cpp`、`SLAPrintSteps.cpp` — `SLARasterParams` 結構與填充邏輯不變
- `src/libslic3r/SLA/RasterToCvMat.hpp/cpp` — `expolygons_to_cvmat()` API 不變
- `src/slic3r/GUI/SLASlice2DCanvas.cpp` — GUI 預覽邏輯不變
