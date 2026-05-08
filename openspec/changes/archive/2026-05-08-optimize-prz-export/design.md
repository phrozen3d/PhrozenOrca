## Context

### 現況（`[sla-on-demand-rasterization]` 後）

```
generate_prz() — 背景執行緒（ExportPRZJob）
│
└─ for each BATCH (8 layers):
     [A] 收集 ExPolygons（循序）
     [B] TBB parallel_for: expolygons_to_cvmat × 8  ← CPU 密集
     [C] for i in batch: RLE encode → stream write → mat.release()  ← 循序，CPU 閒置
     └─ 等待 [C] 結束 → 下一批次 [A]...（批次間嚴格串列）
```

**問題**：[B] 結束後，核心在 [C] 期間大量閒置；下一批次 [B] 要等 [C] 完全結束才開始。

### 目標架構

```
generate_prz() — ExportPRZJob 背景執行緒（Consumer 主控）
│
├─ 啟動 Producer std::thread
│    └─ for each BATCH:
│         [A] 收集 ExPolygons（循序）
│         [B] TBB parallel_for: expolygons_to_cvmat × 8
│         [C] queue.push(std::move(batch_mats))  ← 阻塞若 queue 滿
│
└─ Consumer loop（主 ExportPRZJob 執行緒）:
     [D] batch_mats = queue.pop()  ← 阻塞若 queue 空
     [E] for i in batch: RLE encode → out.write() → mat.release()
     [F] ctl.update_status(progress)  ← 跨執行緒安全（見 UI 章節）
     └─ 直到 queue 收到 sentinel
```

**重疊效果**：當 Consumer 在執行 [E]（RLE 編碼），Producer 已在執行下一批次的 [B]（光柵化），CPU 利用率翻倍。

---

## Goals / Non-Goals

**Goals:**
- 以 Producer-Consumer Pipeline 讓批次光柵化與 RLE 編碼並行，縮短匯出總時間
- Queue Depth = 2，記憶體峰值 ≤ 368 MB（`2 × 8 × 23 MB`），維持遠低於 2.3 GB 的約束
- UI 全程不凍結（繼承自 ExportPRZJob 架構），進度條持續更新
- cv::Mat 所有權清晰：Producer 建立，Consumer 釋放，無 double-free 風險

**Non-Goals:**
- BATCH_SZ 不作為 UI 可設定項（維持硬編碼，本次不引入 QUEUE_DEPTH 為 UI 項）
- 不修改 `SLARasterParams`、`RasterToCvMat`、`ExportPRZJob` 的任何公開 API
- 不在切片階段（`slapsRasterize`）做任何預計算
- 方向 B（RLE 並行）作為選擇性優化，僅在 Consumer 側批次內實施，不影響 Pipeline 主架構

---

## Decisions

### D1：使用 `std::thread` + `std::queue` + `std::mutex` + `std::condition_variable` 實作 Pipeline

**做法：**

```cpp
// in generate_prz() (PhrozenPRZ.cpp)
constexpr size_t BATCH_SZ   = 8;
constexpr size_t QUEUE_DEPTH = 2;

using Batch = std::vector<cv::Mat>;
std::queue<Batch>        batch_queue;
std::mutex               queue_mutex;
std::condition_variable  queue_cv_not_full;
std::condition_variable  queue_cv_not_empty;
bool producer_done = false;
```

**Producer thread**（`std::thread`）:
1. 逐批次執行 [A] 收集 ExPolygons + [B] TBB 平行光柵化
2. 用 `queue_cv_not_full.wait()` 阻塞直到 queue 有空位
3. `queue.push(std::move(batch_mats))`，喚醒 Consumer
4. 全部批次完成後設 `producer_done = true` 並喚醒 Consumer

**Consumer**（原 generate_prz 執行緒）:
1. 用 `queue_cv_not_empty.wait()` 阻塞直到 queue 非空或 producer_done
2. `queue.pop()`，取得 batch 所有權
3. 喚醒 Producer（一格空出）
4. 執行 [E] RLE 編碼 + 寫入 + `mat.release()`

**選擇 `std::thread` + `std::queue` 而非 TBB pipeline 的理由：**

| 方案 | 優點 | 缺點 |
|------|------|------|
| `std::thread` + 手動 queue | 完全掌控所有權語意與取消語意；易在 `cancellation` 路徑中安全中斷 | 需手寫同步原語 |
| `tbb::parallel_pipeline` | TBB 原生；無需手寫 mutex | `tbb::filter` 的 `flow_control::stop()` 取消機制較不直覺；所有權語意較複雜 |
| `tbb::concurrent_bounded_queue` | 免手寫 mutex | 需搭配 `std::thread`；加速效益不明顯 |

結論：**手動 `std::thread` + 標準庫同步原語**，行為完全可預期，取消路徑最清晰。

---

### D2：cv::Mat 所有權與生命週期

**所有權轉移路徑：**

```
Producer:
  batch_mats[i] = expolygons_to_cvmat(...)   // Producer 分配 cv::Mat 資料
  queue.push(std::move(batch_mats))           // 所有權移交 queue

Consumer:
  Batch b = std::move(queue.front()); queue.pop();  // 所有權轉移至 Consumer
  for (auto& mat : b) {
      rle_encode_and_write(mat, out);
      mat.release();                          // Consumer 負責釋放
  }
```

**不變式（Invariants）：**
- Producer 在 `push` 之後不再存取任何 `cv::Mat`
- Consumer 在 `pop` 之前不存取 queue 中的任何資料
- `mat.release()` 在 Consumer 的同一執行緒內循序呼叫，無 data race

**記憶體峰值計算：**
- Queue 最多持有 `QUEUE_DEPTH = 2` 個批次
- Producer 正在計算第 3 批次時，最多 2 個批次在 queue 中
- 峰值 = `(QUEUE_DEPTH + 1) × BATCH_SZ × 23 MB = 3 × 8 × 23 = 552 MB`（最壞情況）
- 實際峰值更低（Consumer 持續消費釋放）

若要嚴格保持 ≤368 MB，可調整 Queue Depth = 1（Producer 先行 1 批，Consumer 消費時 Producer 計算下一批）：

| QUEUE_DEPTH | 最壞峰值 | Pipeline 效益 |
|-------------|----------|---------------|
| 1 | ~184 MB × 2 = ~368 MB | Producer 先行 1 批，已足夠重疊 |
| 2 | ~184 MB × 3 = ~552 MB | 緩衝更寬，對 IO stall 更有容忍度 |

**實驗調整為 QUEUE_DEPTH = 2**（效能測試顯示 QUEUE_DEPTH=1 加上 TBB core 限制後反而退步，改為 2 以提供更好的 IO stall 緩衝）。記憶體峰值從 ≤368 MB 升至 ≤552 MB，仍遠低於 2.3 GB 限制。

---

### D3：取消（Cancellation）安全機制

`ExportPRZJob::process(Ctl& ctl)` 透過 `ctl.was_cancelled()` 提供取消信號。

**做法：**
- Consumer 在每批次 [F] 呼叫 `ctl.was_cancelled()` 後設定 `atomic<bool> cancelled = true`
- Producer 在每批次前（[A] 開始前）檢查 `cancelled`，若為 true 立即結束迴圈
- Consumer 收到 cancel 後 drains queue（對剩餘批次呼叫 `mat.release()`）並 join Producer thread
- 避免 Producer 永遠阻塞在 `queue_cv_not_full.wait()` — cancel 時需 notify Producer

---

### D4：選擇性整合方向 B（RLE 批次內並行）

**前提：** RLE 編碼是否為顯著瓶頸，需要在 D1 Pipeline 實作後 profiling 確認。

**做法（若啟用）：**
- Consumer [E] 改為 `tbb::parallel_for` 對批次內各層並行執行 RLE 編碼，結果存入 `std::vector<std::string> encoded(batch_n)`
- 編碼完成後，循序依序呼叫 `out.write(encoded[i])`（確保輸出順序正確）
- `mat.release()` 在各自的 TBB task 內完成

**此方案的前提假設：** RLE 編碼速度遠慢於 `out.write()` I/O；若 I/O 才是瓶頸，並行 RLE 無顯著效益。

**實驗結論（2026-05-08，已捨棄）：** 方向 B 已實作並測試。匯出時間維持 2 分 2 秒，與方向 A（2 分 1 秒）無顯著差異。根本原因：RLE 編碼並非瓶頸；Consumer 受限於記憶體頻寬與磁碟 IO 天花板（`out.write()` stall），並行 RLE 無效。設計已撤銷，維持循序 RLE。

---

## 效能測試最終結論

| 版本 | 匯出時間（11K×6K，360 層） | 加速比 |
|------|--------------------------|--------|
| 原版（批次串列） | 143s | 基準 |
| 方向 A：Pipeline（QUEUE_DEPTH=2，全核心 TBB） | 121s | +15.4% |
| 方向 A + 方向 B：Pipeline + 並行 RLE（已捨棄） | 122s | 無改善 |

**最終採用設計：** 方向 A（Producer-Consumer Pipeline）+ 循序 RLE。

---

## UI Responsiveness & Thread Safety

### 執行緒架構總覽

```
Main/UI Thread
    │
    │  觸發匯出（non-blocking）
    ▼
PlaterWorker::push(ExportPRZJob)
    │
    │  啟動 Job 背景執行緒
    ▼
ExportPRZJob::process(Ctl& ctl)  ← Consumer 主控執行緒
    │
    ├─ 啟動 Producer std::thread
    │       │
    │       └─ [A] 收集 ExPolygons → [B] TBB parallel rasterize → [C] queue.push()
    │
    └─ Consumer loop: queue.pop() → [E] RLE encode → out.write() → [F] ctl.update_status()
```

### 規則 1：Main/UI Thread 觸發後立即釋放

`Plater::export_prz()` 呼叫 `worker->push(job)` 後**立即返回**。

禁止以下模式：
```cpp
// 絕對禁止：阻塞 UI thread
worker->push(job);
job.wait();   // ← 會凍結 UI
```

`ExportPRZJob::finalize()` 透過 `BoostThreadWorker` 的 event 機制回到 UI thread，以 `wxGetApp().plater()->...` 更新完成狀態。

### 規則 2：TBB 核心資源分配 — 避免 Resource Starvation

Producer 的 [B] 使用 `tbb::parallel_for`，預設會佔用所有 TBB 工作執行緒（`tbb::global_control::max_allowed_parallelism`）。

**做法：** 在 Producer 執行緒中建立 TBB task arena：

```cpp
// Producer thread 內
int max_tbb = std::max(1, tbb::this_task_arena::max_concurrency());
tbb::task_arena arena(max_tbb);
arena.execute([&] {
    tbb::parallel_for(...);  // 光柵化使用全部可用核心
});
```

**效能測試結論（11K×6K，360 層）：** 原本保留 2 核（`- 2`）的設計在高解析度模型上造成 +11s 退步，原因是光柵化是 CPU bound 瓶頸，少 2 核的損失大於 Pipeline 節省的量。UI 響應性測試（Task 5.2）通過確認 wxWidgets event loop 不使用 TBB worker，因此不需預留核心給 UI。

### 規則 3：Progress Bar 跨執行緒安全更新機制

**現有機制（繼承自 ExportPRZJob）：**

`Ctl::update_status(int percent, const std::string& msg)` 在 Job framework 內部透過 `wxQueueEvent()` 將進度 post 至 UI thread event queue，Consumer 執行緒呼叫此函式是 thread-safe 的。

**節流（Throttling）：**

Consumer 在每個批次結束後呼叫一次 `update_status()`（而非每層），避免頻繁的 wxQueueEvent 導致 UI event queue 溢出：

```cpp
// Consumer 每批次 [F] 節點
int pct = static_cast<int>((batch_end * 100) / N);
ctl.update_status(pct);  // 每 BATCH_SZ 層更新一次
```

對於 400 層 / BATCH_SZ=8：50 次 update_status 呼叫，遠低於可能造成 UI 卡頓的閾值。

**禁止模式：**
```cpp
// 禁止：直接操作 wxWidgets UI 物件（非 thread-safe）
wxPostEvent(plater, ProgressEvent(pct));  // ← 可能，但不一致
plater->m_progress->SetValue(pct);        // ← 絕對禁止（非 UI thread）
```

---

## Risks / Trade-offs

| 風險 | 影響 | 緩解策略 |
|------|------|----------|
| Producer 拋出例外（`expolygons_to_cvmat` 失敗） | Consumer 永遠卡在 `queue.pop()` | Producer 捕獲例外後設 `error_flag`、喚醒 Consumer；Consumer 在 `pop()` 後檢查 `error_flag` 並重新 throw |
| Consumer 因 IO 錯誤（磁碟滿）提前結束 | Producer 卡在 `queue.push()` | 相同的 cancel 機制：Consumer 設 `cancelled = true` → notify Producer |
| TBB arena 限制造成 `parallel_for` 實際核心數不穩定 | 光柵化速度波動 | 接受；arena 限制為上限，TBB 內部動態排程 |
| Queue 中的 cv::Mat 在 Consumer join 前洩漏 | 記憶體洩漏 | Destructor/catch 路徑需 drain queue 並呼叫所有 `mat.release()` |
| QUEUE_DEPTH=1 不足以吸收 IO stall | Pipeline stall，效益低 | 先以 QUEUE_DEPTH=1 測試；若 profiling 顯示 Consumer 常因 IO wait 而 block Producer，升至 2 |

---

## Migration Plan

1. 讀取 `PhrozenPRZ.cpp` 確認 `generate_prz()` 現有迴圈結構（行 619–776）
2. 在 `generate_prz()` 中宣告 queue、mutex、cv 同步原語
3. 將現有批次光柵化邏輯（[A]+[B]）抽取至 Producer lambda 或函式
4. 將現有 RLE 編碼邏輯（[C]）保留於 Consumer 迴圈
5. 以 `std::thread producer_thread(...)` 啟動 Producer
6. Consumer 迴圈以 pop-encode-write-release 替換現有 [C]
7. 實作 cancellation 路徑（drain + join）
8. 加入 TBB arena 限制（保留 2 核心）
9. 驗證：編譯通過 → 功能正確性 → 效能測試 → 記憶體峰值確認 → UI 響應性確認

## Open Questions

1. `expolygons_to_cvmat()` 是否為執行緒安全（多個 TBB task 各自呼叫，無共享寫入）？需確認 `RasterToCvMat.cpp` 無靜態可寫狀態。
2. `out.write()`（`std::ofstream`）在 Consumer 的 IO stall 情況下，平均延遲為何？若延遲低，QUEUE_DEPTH=1 足夠；若延遲高，需升至 2。
3. ExportPRZJob 的 `finalize()` 是否已正確處理匯出失敗（例外/取消）的 UI 通知？本次是否需要修改？
