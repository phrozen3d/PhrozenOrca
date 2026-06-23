## Context

PRZ 層影像目前走「全幀旋轉副本」路線：portrait `mat`（`rows=display_pixels_x=M`、`cols=display_pixels_y=N`，CV_8UC1，13320×5120 ≈ 65MB）先 `cv::rotate(ROTATE_90_CW)` 產出 landscape `mat_rotated`（**再一張 65MB**），`prz_orient_after_rotate` 全幀翻轉，最後 row-major 線性 RLE 掃描。

- **記憶體**：`TLSData.mat_rotated`（[SLAPrintSteps.cpp:1534](../../../src/libslic3r/SLAPrintSteps.cpp#L1534)）每緒一張全幀 Mat，8 緒 +~520MB；PhrozenPRZ cache-miss 批次另每層 malloc 一個 `rotated`（[PhrozenPRZ.cpp:822-824](../../../src/libslic3r/Format/PhrozenPRZ.cpp#L822-L824)）。
- **流量**：rotate（讀 65MB src + 寫 65MB）、flip（讀 65MB + 寫 65MB）、RLE（從 DRAM 讀回 65MB）三趟全幀 DRAM pass。

幾何前提（旋轉的像素映射，全程不變）：
```
ROTATE_90_CW:  dst(i,j) = src(M-1-j, i)
  → landscape dst 為 N 列 × M 行；dst 第 i 列 == src 第 i 行(column)，由下而上讀
  → RLE 對 dst row-major 線性掃描；run 跨 dst 列邊界連續（現行即如此）
翻轉:  prz_orient_after_rotate(dst, final_x_mirror?1:0)   // 1=水平, 0=垂直
```

唯一真相源 `prz_orient_after_rotate` / `prz_final_x_mirror` 已存在於 [PhrozenPRZOrient.hpp](../../../src/libslic3r/Format/PhrozenPRZOrient.hpp)，本設計沿用不動。

## Goals / Non-Goals

**Goals:**
- 消除 65MB 全幀旋轉副本（`mat_rotated` / per-layer `rotated`），−~520MB 常駐峰值，朝整體 ~1.6GB 藍圖收斂。
- 融合輸出與舊「rotate→flip→線性掃描」路徑 **byte-identical**；`CACHE_VERSION` 不變、既有 disk cache 續用。
- 吞吐**持平或更快**：以 L2/L3-friendly 條帶把「rotate 輸出→RLE 消費」做成 producer-consumer 融合，省掉 flip 全幀 pass 與 RLE 的 65MB DRAM 回讀。
- 主迴圈與 PhrozenPRZ cache-miss 共用同一支 `prz_encode_layer_banded`，雙路徑一致由建構保證。

**Non-Goals:**
- `band_cols`(K) 的最終調優定值（留待 profile）、並行度調整（維持 per-layer grain）、thumbnail 路徑、SL1 PNG 路徑、cache 鍵格式、UI。
- 改動旋轉/翻轉的對外幾何語義（輸出嚴格 byte-identical）。

## Decisions

### D1. 新增融合自由函式與簽章

新增 TU `PhrozenPRZRle.{hpp,cpp}`（與 `PhrozenPRZOrient.hpp` 同層的 OpenCV 依賴邊界；大函式置於 `.cpp` 避免 inline 進 header 造成重複 codegen / `LNK2005`）。

```cpp
// PhrozenPRZRle.hpp
namespace Slic3r {
// 條帶化融合：旋轉(ROTATE_90_CW) + 局部翻轉 + PRZ-RLE 編碼。
// 輸入 portrait CV_8UC1（rows=display_pixels_x, cols=display_pixels_y）。
// 產出 PRZ 層位元組流（0x55 head + runs + checksum）寫入 out，與
//   cv::rotate(ROTATE_90_CW) → prz_orient_after_rotate → 線性 RLE
// 逐位元組相同。回傳寫入 out 的位元組數。
// out 由呼叫端持有並跨層重用（維持零-malloc 契約）。
std::size_t prz_encode_layer_banded(const cv::Mat&     portrait,
                                    bool               final_x_mirror,
                                    int                band_cols,   // K，預設常數見 D6
                                    std::vector<char>& out);
}
```

- RLE 常數（`RLE_BLACK/WHITE/GRAY`、`RLE_BYTE_NUMBER`、`RLE_CONT_BOUND`、`RLE_BOUND_0`，現重複於 [SLAPrintSteps.cpp:1512-1517](../../../src/libslic3r/SLAPrintSteps.cpp#L1512-L1517) 與 PhrozenPRZ）一併內收至此 `.cpp`，消除重複。
- **理由**：兩個既有旋轉+RLE 站點合一，二進位一致從「人工維持」升級為「結構不可能不一致」。

### D2. 條帶 = src 直向 column-slab；tile 經 cv::rotate 產生

dst 第 `i` 列 == src 第 `i` 行(column)，故「K 個連續 dst 列」= 「K 個連續 src column」= src 的一個 `M×k` 直向 slab。

```
band b → c0 = band*K,  k = min(K, N - c0)              // 殘餘條帶無特例
slab   = portrait(cv::Range::all(), cv::Range(c0, c0+k))   // M×k，非連續 ROI
cv::rotate(slab, tile_view, ROTATE_90_CLOCKWISE)          // → k×M 連續 tile
```

- 沿用 `cv::rotate`（**否決天真逐像素 index arithmetic**：手寫複合 rotate∘mirror 風險高、且全幀跨步 column 掃描近 100% cache miss）。
- **理由**：把跨步轉置一次做完，讓下游 RLE 對 `tile.data` 純順序掃描。

### D3. 對稱翻轉規則：單一 bool 控制正逆序排程（grill 收斂・CRITICAL）

全圖垂直翻轉在 K>1 時**無法只靠外層條帶逆序**達成——會造成「區段置換（Block Permutation）」沉默錯誤（可正常 RLE 解碼、過長度校驗，卻在硬體印出分段鏡像）。正解：**全圖垂直翻轉 ≡（顛倒 band 排程順序）∘（顛倒每個 tile 內部列序）**，兩者缺一不可。收斂為「雙家族皆呼叫局部翻轉，唯一分歧在外層排程方向」：

```
┌─────────────┬──────────────────┬──────────────────────────────┐
│ 家族         │ 外層 band 排程    │ Tile 內局部操作               │
├─────────────┼──────────────────┼──────────────────────────────┤
│ FALSE 垂直   │ 逆序 Descending  │ prz_orient_after_rotate(0)    │
│ TRUE  水平   │ 正序 Ascending   │ prz_orient_after_rotate(1)    │
└─────────────┴──────────────────┴──────────────────────────────┘
```

外層迴圈以**單一 bool** 控制（不需兩段重複迴圈）：

```cpp
const bool descending = !final_x_mirror;          // 垂直家族逆序
const int  flip_code   = final_x_mirror ? 1 : 0;  // 沿用唯一真相源語義
const int  n_bands     = (N + K - 1) / K;
for (int b = 0; b < n_bands; ++b) {
    const int band = descending ? (n_bands - 1 - b) : b;   // ← 排程指標反轉
    const int c0   = band * K;
    const int k    = std::min(K, N - c0);
    // rotate slab → tile_view(k×M)
    prz_orient_after_rotate(tile_view, flip_code);         // ← 局部翻轉（兩家族皆做）
    enc.feed(tile_view.data, k * M);                       // ← 見 D4
}
```

- **為何水平不需逆序、垂直需要**：水平翻轉是 within-row 操作，tile 含整列全 M 行 → 局部=全域；垂直翻轉是 across-row 操作，K>1 時跨 tile 邊界 → 需「局部翻 tile 列」+「逆序排 band」雙管齊下。
- **替代（捨棄）**：手寫 sign-flipped index arithmetic 直讀 mat（無 flip 緩衝）→ 兩條歧異路徑、繞過唯一真相源、極易再生區段置換漏洞。

### D4. RLE 狀態機跨條帶連續攜帶（feed/finish 封裝）

把 RLE 編碼封成一個就地狀態物件，跨 tile/band 邊界**不重置**：

```cpp
struct PrzRleEncoder {
    std::vector<char>& out;
    uchar  cur;   int count;   int sum;   std::size_t pos;
    bool   started = false;
    void begin();                         // 寫 0x55 head；pos=1；sum=0
    void feed(const uchar* d, int n);     // 續 run：px==cur 則 ++count，否則 flush_run+換色
    std::size_t finish();                 // flush 末段 run + 寫 checksum；回傳 pos
};
```

- 每個 tile 呼叫一次 `feed(tile_view.data, k*M)`；run 跨 `feed` 之間、跨 band 之間、與舊版「跨 dst 列」語義完全一致地連續。
- `0x55` head 於 `begin()` 一次、checksum 於 `finish()` 一次（全幀首尾各一次，非每條帶）。
- **殘餘條帶**：`k=min(K,N-c0)` 動態界定，殘餘 tile 走同一 `feed`，狀態機不被特例打斷。
- **count 上界**：全單色層 → 單一巨型 run，`count ≤ N*M ≈ 68M < 2^31`，落於 4-byte 連續編碼 `RLE_CONT_BOUND[3]=1<<28` 範圍，沿用既有 `flush_run` 分支，無溢位。

### D5. TLS 重用與零-malloc 契約；移除 mat_rotated

- **tile 緩衝**：`prz_encode_layer_banded` 內 `thread_local cv::Mat tile`，一次配置為 **`K×M`**（≈ K×13320 B）後跨層重用。殘餘條帶 (`k<K`) 不重配，改用**頂部 k 列的連續 ROI view** `tile(cv::Rect(0,0,M,k))` 作為 `cv::rotate` 的 dst（尺寸恰為 k×M → OpenCV 不觸發 realloc；頂部 k 列連續 → `feed` 可直接掃 `.data`）。
- **mat 與 out**：仍由呼叫端持有 TLS（主迴圈 `TLSData.mat` / `rle_buf`）。
- **移除**：`TLSData.mat_rotated`（[SLAPrintSteps.cpp:1534](../../../src/libslic3r/SLAPrintSteps.cpp#L1534)）；PhrozenPRZ 批次 per-layer `rotated`（[PhrozenPRZ.cpp:822-824](../../../src/libslic3r/Format/PhrozenPRZ.cpp#L822-L824)）。
- **無 data race**：`tile` 為函式內 `thread_local`，各緒獨立；同層在同緒上 model→LUT→thumb→encode 為循序呼叫，與其他 TLS 緩衝不同時改寫。

### D6. 幾何轉置的記憶體管線優化與 K 取值

```
舊：mat(65MB) ─rotate→ mat_rotated(65MB,常駐) ─flip→(全幀) ─RLE 讀─→ out
     DRAM pass: rotate 讀65+寫65、flip 讀65+寫65、RLE 讀65   ≈ 5×65MB

新：for band: slab ─rotate→ tile(~KxM, 熱於 L2/L3) ─flip(局部)─ feed(順序掃)
     tile 產出後立即被 RLE 消費（producer-consumer 融合），不落 DRAM 回讀
     省下：flip 全幀 pass + RLE 的 65MB DRAM 回讀；常駐 −65MB/緒
```

- **slab 讀取樣式**：`cv::rotate(M×k slab)` 沿 src 列前向掃描（每列讀 k 連續 B、列間定步長 N）→ 規則跨步、prefetcher 友善；輸出 `k×M` tile 連續。
- **K 取值**：令 `tile ≈ K×M` 落在 L2/L3。M=13320 時 `K=256 → ~3.4MB`。K 太小 → 每帶 `cv::rotate` 呼叫開銷與邊界 run 切換變多；K 太大 → tile 溢出 L3、退化為 DRAM 來回。**最終值留待 profile**，先以常數 `PRZ_RLE_BAND_COLS`（暫定 256）定義並註記理由。
- **理由**：吞吐主要收益不在「少做轉置」（總轉置工作量相當），而在「RLE 在 tile 仍熱於快取時就地消費，免去 flip pass 與 65MB 回讀」+「砍掉 65MB 常駐」。

### D7. byte-identity 與不 bump CACHE_VERSION

- 融合輸出與舊路徑對「最終 dst' row-major 位元組序」完全相同（D3 幾何等價 + D4 狀態連續 + D2 同一 `cv::rotate`/`prz_orient_after_rotate`）。
- 故 `CACHE_VERSION` **不變**；既有 disk cache 不失效；cache-hit(舊 bytes) 與 cache-miss(新融合 bytes) 不分歧。由「黃金對拍」與「K-不變性」測試雙重把關（見 specs/tasks）。

## Risks / Trade-offs

- **區段置換鏡像（最毒・沉默錯誤）** → D3 對稱翻轉規則 + **K-不變性測試**（同層在 K=1/質數/K>N 下輸出須完全相同）作為 CI 門禁；單純「能解碼」永遠抓不到。
- **byte 漂移使舊 cache stale** → D7 幾何/狀態定性相同 + 黃金對拍逐 byte 比對；不 bump 版本是「主動宣告相同」。
- **殘餘條帶 tile realloc 破壞零-malloc** → D5 預配 `K×M` + 頂部 k 列 ROI view，尺寸吻合不觸發 realloc。
- **巨型單色 run count 溢位** → `N*M < 2^31`，落於 4-byte 連續編碼範圍（D4）。
- **K 選錯吞吐不升反降** → D6 以 profile 定值；提速論點誠實限定於「省 flip pass + RLE 回讀 + 常駐」，非「少做轉置」。
- **層內並行誘惑** → 條帶為層內**序列**（run-state 序列攜帶）；嚴禁下放為層內並行，並行維持 per-layer grain（於 specs 明文約束）。

## Migration Plan

- 純內部重構，無檔案格式 / profile / API 變更。
- `CACHE_VERSION` 不變 → 既有 disk cache 續用、使用者無感、無需重切。
- Rollback：revert 本 change 即回到全幀 `mat_rotated` 路徑；因 byte-identical，回滾不影響既有 cache 或既有 PRZ 輸出。

## Open Questions

- `PRZ_RLE_BAND_COLS`(K) 最終定值：須 profile 在目標機台（13320×5120）量「wall-clock 不退步且峰值確實 −~520MB」後固定。
- `feed` 的 tile 是否傳 `cv::Mat` 或裸指標：傾向裸 `(data, n)` 以與 RLE 純線性語義解耦；`cv::rotate` 需 `cv::Mat` dst 故 tile 本體仍為 `cv::Mat`。
- 是否一併把 thumbnail 的 `rle_encode_gray` 收進同一 TU（目前獨立於 RasterCache）→ 本變更**不**處理，避免擴大範圍。