# Spec: SLA On-Demand PRZ 批次匯出

## Requirement: generate_prz 採雙路徑匯出（快取命中 / 快取未命中）

`generate_prz()` SHALL 依 `RasterCache::is_valid()` 決定執行路徑：

**快取命中路徑**：以批次大小 `EXPORT_BATCH = 8` 迭代所有 N 層，每批次以 `tbb::task_arena(EXPORT_BATCH)` 限制並行數，平行呼叫 `RasterCache::read_layer` 讀取原生 PRZ-RLE bytes（`.rle` 檔案）至 `rle_results[]`；再循序寫入 per-layer header（`prz_layer_content`）、4-byte BE 長度、RLE payload、CRLF，最後一層附加 DLP end tag；每層寫完後立即 `clear()` + `shrink_to_fit()`；不執行光柵化、不使用 `cv::imdecode`。

**快取未命中路徑**：以批次大小 `BATCH_SZ = 8` 迭代 `print.print_layers()`，TBB 平行對每層執行**雙軌光柵化**——取 model-track 與 support-track 幾何（各平移 `rp.shift`），依 `lid < bottom_layer_count` 決定 model 是否走二值，support 恆走二值，model 套 `apply_picture_grayscale_lut` 後與 support 合成（`output = max(model_after_LUT, support_255)`）。support 合成 SHALL 走 **support 局部 ROI**（`composite_support_binary`），**不**配置全幀 support 緩衝；`enumerable_thread_specific<cv::Mat> support_tls` SHALL 移除。此雙軌合成 SHALL 呼叫與 `rasterize()` 主迴圈相同的共用函式，確保 byte 一致；其後接 `cv::rotate(90°CW)` 與 `prz_orient_after_rotate(prz_x_mirror)`，RLE 編碼後串流輸出，`cv::Mat` 立即釋放。

ROI 合成輸出 SHALL 與全幀合成逐像素相同，`CACHE_VERSION` 不變，既有快取續用。

### Scenario: 快取命中時 PRZ 匯出成功且輸出 Bit-Perfect
- **WHEN** 使用者點擊匯出 PRZ，且 `RasterCache::is_valid()` 為 `true`
- **THEN** 輸出的 PRZ 檔案中每層的 RLE 編碼像素資料，與重構前版本產生的結果完全相同（bit-perfect）

### Scenario: 快取未命中路徑與主迴圈輸出一致
- **WHEN** 快取未命中，`generate_prz()` 以雙軌光柵化 on-demand 產生各層 RLE
- **THEN** 每層 RLE bytes 與 `rasterize()` 主迴圈經 RasterCache 寫入者完全相同（bit-perfect），支撐區域為純二值且豁免 `picture_grayscale`

### Scenario: cache-miss 路徑不持有全幀 support 緩衝
- **WHEN** 快取未命中路徑並行光柵化各層
- **THEN** 不存在每緒一張全幀 support `cv::Mat`（`support_tls` 已移除）；support 合成僅使用局部 ROI 緩衝

### Scenario: ROI 合成與全幀版逐 byte 一致（不 bump 版本）
- **WHEN** 同一份切片分別以 Opt-2 ROI 合成與全幀合成（Opt-2 前版）匯出 PRZ
- **THEN** 兩者每層 RLE bytes 完全相同；`CACHE_VERSION` 未變，Opt-2 前建立的 disk cache 在 Opt-2 後仍 `is_valid()` 命中且與 cache-miss 輸出一致

### Scenario: 批次邊界正確處理
- **WHEN** 切層總數不為 EXPORT_BATCH 整數倍（例如 100 層，最後一批為 4 層）
- **THEN** 最後一批正確處理剩餘層數，不發生越界存取或遺漏層

### Scenario: 批次內各層 RLE 在循序寫入後立即釋放
- **WHEN** 第 i 批次的循序 append 完成
- **THEN** `rle_results[i].clear()` 與 `shrink_to_fit()` 被呼叫，記憶體即時歸還，不等待整個匯出結束

### Scenario: 記憶體峰值符合預期
- **WHEN** 匯出 360 層 13320×5120 解析度的 PRZ（快取命中路徑）
- **THEN** 同一批次同時存在記憶體中的資料量 ≤ `EXPORT_BATCH × ~5 MB（RLE）≈ 40 MB`；峰值遠低於 650 MB

---

## Requirement: prz_header 改用 print_layers 計算層數

`prz_header()` 中所有 `print.layer_images().size()` 呼叫 SHALL 改為 `print.print_layers().size()`，以獲取正確的切層總數。

### Scenario: header 中的層數欄位正確
- **WHEN** `prz_header()` 被呼叫
- **THEN** header 中的 `total` 層數欄位值等於 `print.print_layers().size()`

---

## Requirement: 匯出按鈕 guard 條件更新

`Plater.cpp` 的匯出前 guard SHALL 改為檢查 `is_step_done(slapsRasterize) && raster_params().has_value()`，不再依賴 `layer_images().empty()`。

### Scenario: 切片完成後匯出按鈕可點擊
- **WHEN** `slapsRasterize` 步驟完成且 `raster_params()` 有值
- **THEN** 匯出按鈕不被 guard 攔截，`generate_prz()` 正常執行

### Scenario: 切片尚未完成時匯出被攔截
- **WHEN** `slapsRasterize` 步驟未完成或 `raster_params()` 無值
- **THEN** 匯出操作顯示錯誤訊息並中止

---

## Requirement: rasterize Pipeline 2 移除

`SLAPrintSteps.cpp::rasterize()` 中建構 `all_layers` 並呼叫 `expolygons_layers_to_cvmat()` 填入 `m_layer_images` 的所有程式碼 SHALL 被移除，改為計算並填入 `m_raster_params`。

### Scenario: rasterize 步驟不再填充 m_layer_images
- **WHEN** `slapsRasterize` 步驟完成
- **THEN** `print.layer_images()` 為空向量，`print.raster_params().has_value()` 為 `true`

### Scenario: rasterize 步驟時間縮短
- **WHEN** `slapsRasterize` 步驟執行
- **THEN** 執行時間僅包含 Pipeline 1（`draw_layers()`），不包含 Pipeline 2 的全量光柵化，等待時間約減少 50%

---

## Requirement: generate_prz 串流寫檔至 std::ostream

`generate_prz()` 簽章 SHALL 改為接收 `std::ostream &out`，每批次處理完成後直接呼叫 `out.write()` 串流寫入，不再在記憶體中累積完整的 PRZ 字串。

### Scenario: 匯出期間記憶體無大型 string buffer
- **WHEN** 匯出任意大小的 PRZ
- **THEN** 不存在持有完整 PRZ 資料的 `std::string`；各層 RLE 資料在寫入後即釋放

---

## Requirement: PRZ 匯出在背景執行緒執行（ExportPRZJob）

PRZ 匯出 SHALL 透過 `ExportPRZJob`（繼承 `Job`）在 `PlaterWorker` 背景執行緒執行，主執行緒不被封鎖。

### Scenario: 匯出期間 UI 保持響應
- **WHEN** 使用者點擊匯出 PRZ
- **THEN** 匯出在背景執行，主 UI 執行緒不凍結，使用者可繼續操作

### Scenario: 進度條正確更新
- **WHEN** `generate_prz()` 的 progress callback 以每批次為單位呼叫
- **THEN** 通知進度條顯示 0-100% 的匯出進度

### Scenario: 取消功能正常
- **WHEN** 使用者在匯出期間點擊取消
- **THEN** `progress` callback 返回 `false`，`generate_prz()` 中止，不完整的 PRZ 檔案被刪除

---

## Requirement: PRZ header `aaLevel` 欄位 SHALL 以採樣等級索引映射表寫入

`prz_header()` 寫入 PRZ header 的 `aaLevel` 欄位（2 bytes big-endian short）時，SHALL 將 `anti_aliasing_level` 設定值視為「採樣等級索引」，並依下表映射為「每軸 AA 採樣像素數」後寫入；不得直接寫入原始整數值。

| `anti_aliasing_level` 設定值 | PRZ `aaLevel` 寫入值 | 語意 |
|---|---|---|
| 0 | 2 | Low |
| 1 | 4 | Mid（預設） |
| 2 | 8 | High |
| 其他值（含 ≥3、負值或未列舉值） | 4 | 安全 fallback 至 Mid |

此映射僅影響 PRZ 寫入端，不影響 `SLAPrintSteps.cpp` 內 `aa_steps` 計算或其他切片器格式輸出。

#### Scenario: 設定值 1 寫入 aaLevel = 4

- **WHEN** profile `anti_aliasing_level = 1`，匯出 PRZ
- **THEN** PRZ header 的 `aaLevel` 欄位寫入 `4`

#### Scenario: 設定值 2 寫入 aaLevel = 8

- **WHEN** profile `anti_aliasing_level = 2`，匯出 PRZ
- **THEN** PRZ header 的 `aaLevel` 欄位寫入 `8`

#### Scenario: 設定值 0 寫入 aaLevel = 2

- **WHEN** profile `anti_aliasing_level = 0`，匯出 PRZ
- **THEN** PRZ header 的 `aaLevel` 欄位寫入 `2`

#### Scenario: 未列舉的設定值 fallback 至 Mid

- **WHEN** profile `anti_aliasing_level` 為 `5`（未列舉值）或負值，匯出 PRZ
- **THEN** PRZ header 的 `aaLevel` 欄位寫入 `4`（Mid fallback）

---

## Requirement: PRZ header 與 layer content 的 `*_second_speed` 在 `*_second_distance == 0` 時 SHALL 強制輸出 `0.f`

`prz_header()` 與 `prz_layer_content()` 寫入以下四組「第二段升降速度」欄位時，SHALL 先檢查對應的 distance 欄位是否為 `0.f`；若為 `0.f`，則 speed 欄位 SHALL 強制輸出 `0.f`，不得寫入 cfg 內的非零預設值。

對應關係：

| Distance cfg key | Speed cfg key |
|---|---|
| `bottom_lift_second_distance` | `bottom_lift_second_speed` |
| `lift_second_distance` | `lift_second_speed` |
| `bottom_retract_second_distance` | `bottom_retract_second_speed` |
| `retract_second_distance` | `retract_second_speed` |

判斷條件 SHALL 為精確相等（`dist == 0.f`），不使用浮點容差。

#### Scenario: 第二段升降距離為 0 時 header speed 寫入 0

- **WHEN** profile `bottom_lift_second_distance = 0` 且 `bottom_lift_second_speed = 45`，匯出 PRZ
- **THEN** PRZ header 的 `BottomLift_second_Speed` 欄位寫入 `0.f`

#### Scenario: 第二段升降距離非 0 時 header speed 寫入 cfg 值

- **WHEN** profile `lift_second_distance = 5` 且 `lift_second_speed = 60`，匯出 PRZ
- **THEN** PRZ header 的 `Lift_second_Speed` 欄位寫入 `60.f`

#### Scenario: 四組 distance/speed 對應皆受保護

- **WHEN** profile 四組 `*_second_distance` 皆為 `0`，且四組 `*_second_speed` 皆為非零預設值
- **THEN** PRZ header 中所有四組 `*_second_Speed`（bottom_lift / lift / bottom_retract / retract）皆寫入 `0.f`

#### Scenario: 每層 layer content 的 second_speed 同樣受保護

- **WHEN** profile `retract_second_distance = 0` 且 `retract_second_speed = 150`，匯出 PRZ
- **THEN** 每層 layer content 的 `Retract_Second_Speed` 欄位皆寫入 `0.f`

#### Scenario: 底層與一般層分別套用對應 distance/speed 對

- **WHEN** profile `bottom_retract_second_distance = 0`、`bottom_retract_second_speed = 150`、`retract_second_distance = 3`、`retract_second_speed = 80`，匯出 PRZ
- **THEN** 底層（index < bottom_layer_count）layer content 的 `Retract_Second_Speed` 寫入 `0.f`，一般層的 `Retract_Second_Speed` 寫入 `80.f`

---

## Requirement: PRZ header `fileTime` 欄位 SHALL 保留 Orca 既有真實時間戳輸出

`prz_header()` 寫入 `fileTime` 欄位時，SHALL 維持現有 `YYYY-MM-DD HH:MM:SS` 真實本地時間戳寫入邏輯，不對齊 Chitubox 輸出的 `"0"` 或其他固定字串。

理由：`fileTime` 對使用者除錯、檔案歸檔、版本追蹤有實質價值，且 Phrozen 韌體不解析此欄位，無相容性問題。

#### Scenario: PRZ header 含有真實切片時間戳

- **WHEN** 在本地時間 `2026-05-25 14:30:00` 匯出 PRZ
- **THEN** PRZ header 的 `fileTime` 欄位寫入 `"2026-05-25 14:30:00"`（以 24 bytes 字元字串填充，尾端補 `\0`）

#### Scenario: fileTime 不因「對齊 Chitubox」要求而被改寫為 0

- **WHEN** 本變更實作完成後檢視 [src/libslic3r/Format/PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 寫入 `fileTime` 的程式碼區段
- **THEN** 寫入邏輯仍呼叫 `time()` / `localtime()` 取得當下本地時間並格式化為 `YYYY-MM-DD HH:MM:SS`，無「寫入 `"0"` 或空字串」的分支

---

## Requirement: PRZ header 的 `software` 與 `softwareVersion` 欄位 SHALL 寫入有意義版本字串

`prz_header()` 寫入 `software`（32 bytes）與 `softwareVersion`（24 bytes）兩欄位時，SHALL 寫入可辨識 Phrozen Orca 來源的版本字串，不得保留空字串輸出。

字串來源（具體值於 tasks 階段最終決定）：
- `software`：寫入辨識字串（如 `"PhrozenOrca"` 或 Orca 主版本字串）
- `softwareVersion`：寫入版本號（如 Orca 內部 `SLIC3R_VERSION` 巨集值）

字串長度超過欄位寬度時 SHALL 截斷至欄位寬度；不足時尾端以 `\0` 填充。

#### Scenario: software 欄位非空

- **WHEN** 匯出 PRZ
- **THEN** `software` 欄位前綴含非 `\0` 字元，可被 hex dump 或檔頭解析工具辨識為有意義字串

#### Scenario: softwareVersion 欄位非空

- **WHEN** 匯出 PRZ
- **THEN** `softwareVersion` 欄位前綴含非 `\0` 字元，且能對應到 Phrozen Orca 的版本號

---

## Requirement: PRZ header 的 `priceUnit` 欄位 SHALL 寫入單位字串

`prz_header()` 寫入 `priceUnit`（8 bytes）欄位時，SHALL 寫入 `"$/L"` 字串，尾端以 `\0` 填充至 8 bytes；不得保留現有的全 `\0` 輸出。

#### Scenario: priceUnit 寫入 "$/L"

- **WHEN** 匯出 PRZ
- **THEN** `priceUnit` 欄位的前 3 bytes 為 ASCII `$`、`/`、`L`，後 5 bytes 為 `\0`
