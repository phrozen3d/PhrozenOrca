## 1. 前置調查（Pre-flight）

- [x] 1.1 讀取 `src/libslic3r/Format/PhrozenPRZ.cpp` generate_prz() 現有迴圈，確認行號與結構和 Explore 分析一致
- [x] 1.2 確認 `src/libslic3r/SLA/RasterToCvMat.cpp` 中 `expolygons_to_cvmat()` 無靜態可寫狀態（thread-safe）
- [x] 1.3 確認 `src/slic3r/GUI/Jobs/ExportPRZJob.cpp` 的 `Ctl::update_status()` 呼叫為 thread-safe（透過 wxQueueEvent 或等效機制）
- [x] 1.4 確認 `ExportPRZJob::finalize()` 是否處理了匯出失敗（例外/取消）的 UI 通知，若未處理記錄為 Open Question

## 2. Producer-Consumer 基礎架構（PhrozenPRZ.cpp）

- [x] 2.1 在 `generate_prz()` 函式頂部宣告以下同步原語：
  - `using Batch = std::vector<cv::Mat>`
  - `std::queue<Batch> batch_queue`（有界，以 QUEUE_DEPTH = 1 初始）
  - `std::mutex queue_mutex`
  - `std::condition_variable queue_cv_not_full`
  - `std::condition_variable queue_cv_not_empty`
  - `bool producer_done = false`
  - `std::exception_ptr producer_exception = nullptr`
  - `std::atomic<bool> cancelled{false}`
- [x] 2.2 將現有批次光柵化邏輯（[A] ExPolygons 收集 + [B] TBB parallel_for）抽取為 Producer lambda（在 `generate_prz()` 內定義，capture by reference）
- [x] 2.3 在 Producer lambda 末端加入 `queue.push(std::move(batch_mats))` 邏輯，附帶 `queue_cv_not_full.wait()` 阻塞保護
- [x] 2.4 Producer lambda 完成後設 `producer_done = true`，以 `queue_cv_not_empty.notify_one()` 喚醒 Consumer
- [x] 2.5 以 `std::thread producer_thread(producer_lambda)` 啟動 Producer

## 3. Consumer 迴圈實作（PhrozenPRZ.cpp）

- [x] 3.1 在原批次迴圈位置改寫為 Consumer while 迴圈：
  - `queue_cv_not_empty.wait()` 阻塞等待批次或 producer_done
  - `batch_queue.front()` + `queue.pop()`（RAII 取得所有權）
  - 喚醒 Producer（`queue_cv_not_full.notify_one()`）
- [x] 3.2 Consumer 取得批次後：循序呼叫現有 RLE encode + `out.write()` + `mat.release()`（邏輯不變，僅搬移位置）
- [x] 3.3 Consumer 每批次結束後呼叫 `ctl.update_status(pct)` 回報進度（不在每層呼叫）
- [x] 3.4 Consumer 在 `ctl.was_cancelled()` 為 true 時設 `cancelled = true`，drain queue（對剩餘批次呼叫所有 `mat.release()`）後跳出迴圈

## 4. 取消與例外安全（Cancellation & Exception Safety）

- [x] 4.1 Producer lambda 以 `try/catch(...)` 包覆全部邏輯，catch 中設 `producer_exception = std::current_exception()`，喚醒 Consumer 後返回
- [x] 4.2 Consumer 在每次 pop 後檢查 `producer_exception != nullptr`，若有則 rethrow（讓 ExportPRZJob 的外層 catch 處理）
- [x] 4.3 在 Consumer 迴圈結束後（無論正常/取消/例外）執行 `producer_thread.join()`，確保 Producer 不洩漏
- [x] 4.4 確認 cancelled = true 時 Producer 能跳出批次迴圈（在每批次 [A] 開始前檢查 `cancelled.load()`）
- [x] 4.5 確認 Producer 阻塞在 `queue_cv_not_full.wait()` 時，Consumer 的 cancel 路徑也會 `notify_all()` 將其喚醒

## 5. TBB 核心資源限制（UI Starvation 防護）

- [x] 5.1 在 Producer lambda 的 TBB parallel_for 外層包裝 `tbb::task_arena`：
  - `int max_tbb = std::max(1, tbb::this_task_arena::max_concurrency() - 2)`
  - `tbb::task_arena arena(max_tbb); arena.execute([&]{ tbb::parallel_for(...); })`
- [x] 5.2 驗證：在匯出期間拖動 UI 滑桿或切換頁籤不出現卡頓

## 6. 選擇性整合方向 B（RLE 批次內並行）[已捨棄]

> **[已捨棄 Discarded — 2026-05-08]** 方向 B 已實作並測試。結論：匯出時間維持 2 分 2 秒，與 Pipeline Only（2 分 1 秒）無顯著差異。根本原因為記憶體頻寬與磁碟 IO 天花板（非 CPU bound），並行 RLE 無效。程式碼已撤銷，維持方向 A 的循序 RLE 設計。

- ~~[x] 6.1 在 Consumer [E] 中，以 `tbb::parallel_for` 對批次內各層並行執行 RLE 編碼，結果存入 `std::vector<std::string> encoded(batch_n)`~~
- ~~[x] 6.2 編碼完成後，循序依 index 呼叫 `out.write(encoded[i])`（確保輸出二進位順序正確）~~
- ~~[x] 6.3 `mat.release()` 在各自的 TBB task 中完成（編碼後立即釋放）~~
- ~~[x] 6.4 確認 `out.write()` 的呼叫序列與原始實作完全一致（binary diff 或 checksum 比對）~~

## 7. 驗證（Verification）

- [x] 7.1 編譯通過，無新增警告
- [x] 7.2 匯出一個 10×10×10 正方體（少層數），確認 PRZ 二進位內容與舊版實作完全一致（checksum 比對或 prz decode 驗證）
- [x] 7.3 匯出一個複雜模型（≥ 100 層），確認 PRZ 可正常匯入列印機韌體並開始列印
- [x] 7.4 量測：舊版（批次串列）vs 新版（Pipeline）在 100 層模型的匯出時間，記錄加速比
- [x] 7.5 記憶體峰值確認：使用工作管理員或 MSVC 記憶體分析器，確認匯出期間峰值 ≤ 400 MB
- [x] 7.6 UI 響應性確認：匯出期間進行以下操作，確認不卡頓：
  - 拖動視圖
  - 切換設定頁籤
  - 滑動 Layer 預覽滑桿
- [x] 7.7 進度條確認：進度百分比在匯出期間持續遞增，取消按鈕可正常中斷匯出並刪除不完整檔案
- [x] 7.8 取消測試：在匯出 50% 時按取消，確認：Producer thread 已 join、queue 已 drain、記憶體無洩漏
- [x] 7.9 例外測試（模擬）：若可行，模擬 `expolygons_to_cvmat()` 拋出例外，確認 Consumer 正確捕獲並顯示錯誤訊息至 UI
