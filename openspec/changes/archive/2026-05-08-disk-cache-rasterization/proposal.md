# SLA 光柵化磁碟快取（Disk-Cache Rasterization）

## Problem

SLA PRZ 匯出在 11K×6K 解析度、360 層模型上需要 **121 秒**（`optimize-prz-export` 優化後），
幾乎全部時間花費在 `expolygons_to_cvmat` 光柵化運算。

相比之下，Python `web_slicer_core` 對同一模型僅需 **20 秒**，因為它直接讀取預先存在的圖檔，
匯出本身是純磁碟 IO——沒有任何光柵化計算。

**根本差距**：C++ 在每次匯出時重新光柵化；Python 跳過光柵化直接讀圖。

## Proposed Change

引入「SLA 光柵化磁碟快取（PRZ-RLE Disk Cache）」機制，分兩個階段重組現有流程：

### 切片階段（slapsRasterize）— 計算成本前移

將 `optimize-prz-export` 已實作的 **Producer-Consumer Pipeline 引擎**，
從 `generate_prz()` **搬移**至 `slapsRasterize` 步驟執行：

- **Producer**（背景執行緒）：逐批次讀取 ExPolygons → TBB 全核心光柵化 → push cv::Mat batch 至 queue
- **Consumer**（步驟主執行緒）：pop batch → 循序 RLE 編碼 → 寫入本機 Temp 快取檔案 → `mat.release()`

Consumer 寫入的目標不再是 PRZ 輸出串流，而是本機 Temp 資料夾，以**快取鍵 Hash** 為目錄名稱。

### 匯出階段（generate_prz）— 0 計算純 IO

`generate_prz()` 大幅簡化為兩條路徑：

- **Cache Hit**（Hash 命中）：循序讀取 Temp 快取中的 RLE bytes → 直接封裝為 PRZ 格式輸出。
  無任何光柵化、無 RLE 計算，預期匯出時間 **5–15 秒**。
- **Cache Miss**（未命中）：Fallback 到現有 Pipeline 光柵化邏輯（`~121 秒`，行為完全不變）。

## Accepted Trade-offs

| 面向 | 舊行為 | 新行為 |
|------|--------|--------|
| 切片時間 | 快（幾秒） | 增加 ~100s（光柵化前移至切片期） |
| 首次匯出時間 | 121s | **5–15s**（Cache Hit） |
| 設定改變後重匯出 | 121s | 121s（Cache Miss，重新切片） |
| 記憶體峰值 | 552 MB（匯出期） | 552 MB（切片期，相同機制） |
| 磁碟用量 | 0 | ~50–150 MB / 模型 |

切片時間增加是**可接受的**，因為 `[sla-on-demand-rasterization]` 的原始目標是降低記憶體峰值，
而非最小化切片時間。本方案完整保留低記憶體精神（QUEUE_DEPTH + mat.release()）。

## Memory Safety Guarantee

本方案完全沿用 `optimize-prz-export` 的 Pipeline 引擎：

- `QUEUE_DEPTH = 2`：queue 中最多 2 個批次的 cv::Mat
- Consumer 每層 `mat.release()`：完成寫入後立即釋放
- 記憶體峰值 = `(QUEUE_DEPTH + 1) × 8 × 23 MB ≈ 552 MB`（遠低於 2.3 GB 限制）
- `slapsRasterize` 完成後：**所有 cv::Mat 完全釋放**，Disk Cache 是唯一持久狀態

## Expected Outcome

| 場景 | 預期時間 |
|------|---------|
| 切片（含光柵化，首次或設定改變） | 現有切片時間 + ~100s |
| 匯出（Cache Hit） | **5–15 秒** |
| 匯出（Cache Miss / Fallback） | ~121 秒（不變） |
| Python web_slicer_core 基準 | ~20 秒 |

## Scope

- **`SLAPrintSteps.cpp`**：改造 `slapsRasterize` 為 Pipeline 光柵化 + RLE Disk Write 步驟
- **`PhrozenPRZ.cpp`**：改造 `generate_prz()` 為 Cache-Hit 直讀 + Cache-Miss Fallback 兩條路徑
- **新增 `SLARasterCache.cpp / .hpp`**：Hash 計算、Temp 目錄管理、RLE 讀寫介面、啟動清理機制
- **切片期進度條**：`slapsRasterize` 執行期間透過 `report_status` 回報光柵化進度，維持良好 UX

## Non-Scope

- 不修改 `expolygons_to_cvmat()` 核心實作
- 不修改 `ExportPRZJob` 的公開 API 或 `SLARasterParams` 結構
- 不引入網路快取或跨機器共享快取
- 不修改 PRZ 檔案格式本身
- 不實作 LRU 淘汰（Startup 清理 + 7 天 TTL 已足夠）

## Related Changes

- `[optimize-prz-export]`：本方案繼承其 Pipeline 引擎；`generate_prz()` 的 fallback 路徑保留此引擎
- `[sla-on-demand-rasterization]`：本方案強化其記憶體目標，切片後不持久 cv::Mat，改持久 RLE bytes