# prz-band-tiled-rle-encoding Specification

## Purpose
TBD - created by archiving change prz-band-tiled-rle-fusion. Update Purpose after archive.
## Requirements
### Requirement: 融合編碼器輸出與全幀路徑 Byte-Identity（核心不變式）

`prz_encode_layer_banded()` 對任一 portrait 輸入產出的 PRZ 層位元組流（`0x55` head + runs + checksum）SHALL 與舊路徑 `cv::rotate(ROTATE_90_CW)` → `prz_orient_after_rotate(final_x_mirror?1:0)` → 全幀 row-major 線性 RLE 的輸出**逐位元組完全相同**。此為本能力的第一核心不變式（Byte-identity Invariant）；任何破壞它的變更 SHALL 視為回歸。`CACHE_VERSION` SHALL 維持不變，使既有 disk cache 續用且 cache-hit 與 cache-miss 不分歧。

#### Scenario: 融合輸出與全幀參考逐 byte 相同
- **WHEN** 同一 portrait 層分別以 `prz_encode_layer_banded()` 與舊全幀路徑編碼
- **THEN** 兩者位元組長度相同，且每一位元組（含 `0x55` head、各 run、checksum）完全相等

#### Scenario: 兩個呼叫站點輸出一致
- **WHEN** 同一份切片分別經 `rasterize()` 主迴圈與 `generate_prz()` cache-miss 路徑，皆呼叫同一支 `prz_encode_layer_banded()`
- **THEN** 每層 RLE bytes 完全相同（bit-perfect），一致性由共用單一函式結構性保證

---

### Requirement: K-不變性（K-Invariance 殺手測試・核心不變式）

`prz_encode_layer_banded()` 的輸出 SHALL 與 `band_cols`（K）取值**完全無關**：對同一 portrait 輸入，任意合法 K（含 `K=1`、質數、`K>N`、`K=N`）所產出的位元組流 SHALL 兩兩逐位元組相同。此為第二核心不變式（K-invariance Invariant），用以結構性偵測「區段置換（Block Permutation）」類沉默錯誤——該類錯誤可正常 RLE 解碼並通過長度校驗，僅在實體列印呈現分段鏡像，單純「能否解碼」永遠無法捕捉。

#### Scenario: 跨 K 值輸出恆等
- **WHEN** 對同一 portrait 以 `K ∈ {1, 7(質數), 256, N, N+1}` 分別編碼
- **THEN** 所有 K 值產出的位元組流兩兩完全相同

#### Scenario: K 不整除 N 時殘餘條帶不破壞輸出
- **WHEN** `N` 不被 `K` 整除（存在高度 `< K` 的殘餘條帶）
- **THEN** 輸出仍與 `K=1` 的參考結果逐位元組相同；殘餘條帶以 `min(K, N - offset)` 動態界定，無特例分支

---

### Requirement: 雙家族對稱翻轉幾何等價

`prz_encode_layer_banded()` SHALL 依 `final_x_mirror` 套用對稱翻轉規則：**兩家族皆對每個 tile 呼叫 `prz_orient_after_rotate(tile, code)` 做局部翻轉**，唯一分歧為外層 band 排程方向（由單一 bool `descending = !final_x_mirror` 控制）。垂直翻轉家族 SHALL 為「逆序排程 + 局部 `code 0`」，水平翻轉家族 SHALL 為「正序排程 + 局部 `code 1`」。全圖垂直翻轉 SHALL 由「顛倒 band 排程順序」與「顛倒每個 tile 內部列序」**兩者合成**達成；任一單獨皆不充分（K>1 時單靠外層逆序會產生區段置換）。

#### Scenario: Mega 系列（final_x_mirror = FALSE，垂直翻轉）
- **WHEN** 以 `final_x_mirror = false` 編碼某層（Mega / normal 系列）
- **THEN** 外層 band 採**逆序（Descending）**排程，每個 tile 套 `prz_orient_after_rotate(tile, 0)`（垂直翻轉）
- **AND** 輸出與舊全幀「rotate → cv::flip(code 0) → 線性 RLE」逐位元組相同

#### Scenario: Revo 系列（final_x_mirror = TRUE，水平翻轉）
- **WHEN** 以 `final_x_mirror = true` 編碼某層（Revo / lcd_mirror 系列）
- **THEN** 外層 band 採**正序（Ascending）**排程，每個 tile 套 `prz_orient_after_rotate(tile, 1)`（水平翻轉）
- **AND** 輸出與舊全幀「rotate → cv::flip(code 1) → 線性 RLE」逐位元組相同

#### Scenario: 垂直家族 K>1 不得退化為區段置換
- **WHEN** 以 `final_x_mirror = false` 且 `K>1` 編碼一張各條帶內容相異的測試圖
- **THEN** 輸出與全圖垂直翻轉參考相同，**不得**出現「band 間顛倒但 band 內順向」的分段鏡像（區段置換）結果

---

### Requirement: 跨條帶 RLE 狀態連續攜帶

RLE 狀態（目前色 `cur`、連續計數 `count`、checksum 累加 `sum`、寫入位置 `pos`）SHALL 跨 tile 邊界與跨 band 邊界**連續攜帶不重置**，與舊版「跨 dst 列連續」語義一致。`0x55` layer head SHALL 於全幀起始僅寫一次，checksum SHALL 於全幀結束僅寫一次（非每條帶）。全單色層 SHALL 編碼為單一巨型 run（`count` 上界 `N*M < 2^31`，落於 4-byte 連續編碼範圍，無溢位）。

#### Scenario: 跨 band 同色 run 不被切斷
- **WHEN** 一條跨越多個 band 邊界的同色像素序列被編碼
- **THEN** 該序列輸出為單一 run（或與舊全幀路徑相同的 run 切分），run 不因 band 邊界而被人為切斷

#### Scenario: 全黑層編碼為單一巨型 run
- **WHEN** 編碼一張全 `0x00` 的層（`N*M ≈ 68M` 像素）
- **THEN** 輸出為單一 black run，`count` 以 4-byte 連續編碼正確表示，與舊路徑逐位元組相同，無溢位

---

### Requirement: 不物化全幀旋轉副本之記憶體不變式

`prz_encode_layer_banded()` SHALL **不**配置任何全幀（`N×M`，≈65MB）旋轉或翻轉中介緩衝。tile 緩衝 SHALL 為函式內 `thread_local cv::Mat`，一次配置為 `band_cols × M` 後跨層重用（維持零-malloc 契約）；殘餘條帶 SHALL 重用該緩衝的頂部 `k` 列連續 ROI view，不觸發 realloc。`TLSData.mat_rotated`（主迴圈）與 PhrozenPRZ cache-miss 的 per-layer `rotated` SHALL 移除。

#### Scenario: 不存在全幀旋轉緩衝
- **WHEN** 任一執行緒編碼任一層
- **THEN** 不存在 `N×M` 全幀旋轉/翻轉中介 `cv::Mat`；唯一中介為 `≤ band_cols × M` 的 tile 緩衝

#### Scenario: tile 緩衝跨層零配置
- **WHEN** 同一執行緒連續編碼多層
- **THEN** tile 緩衝於首層配置後跨層重用，後續層不再 malloc；殘餘條帶以 ROI view 重用，尺寸吻合不觸發 realloc

---

### Requirement: 驗證 SHALL 採可手動觸發、終端機文字回饋的 CLI 比對手段

本能力的 byte-identity 與 K-不變性驗證 SHALL 透過**可由人工手動觸發**、具備**清晰終端機文字回饋**的 CLI 比對與驗證手段進行（例如獨立比對命令或可從命令列直接執行的測試二進位）。驗證輸出 SHALL 明確列出每層的 PASS/FAIL、首個分歧位元組的 offset、層號與相關 K 值，使結果可被人直觀監控。本能力 SHALL **不**以「無法直觀監控的背景黑箱自動化」作為唯一或主要驗證途徑。

#### Scenario: byte-identity 比對輸出清晰文字結果
- **WHEN** 操作者從命令列手動觸發「融合輸出 vs 全幀參考」比對
- **THEN** 終端機逐層印出 `PASS`/`FAIL`；任一 `FAIL` SHALL 印出該層號與首個分歧位元組的 offset 與兩側位元組值，並以非零退出碼結束

#### Scenario: K-不變性掃描輸出可讀對照
- **WHEN** 操作者從命令列手動觸發 K-不變性掃描（對一組 K 值編碼同一層）
- **THEN** 終端機印出各 K 值的輸出是否與基準（K=1）相同的對照結果；全部相同則 `PASS`、否則標示首個分歧的 K 值與 offset

#### Scenario: 嚴禁無法監控的背景黑箱作為唯一驗證
- **WHEN** 檢視本變更的驗證設計
- **THEN** SHALL 存在至少一種人工可即時觸發並從終端機文字直接判讀結果的手段；不得僅依賴無終端機回饋、無法直觀監控的背景自動化流程

