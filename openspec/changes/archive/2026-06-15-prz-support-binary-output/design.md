## Context

PRZ 光柵化目前是「單軌」：`merged_input_to_slices()` 在 [SLAPrintSteps.cpp:1329](../../../src/libslic3r/SLAPrintSteps.cpp#L1329) 以 `union_ex(trslices)` 把 `model_polygons` 與 `supports_polygons`（已 `diff_ex` 去除模型重疊）合併成單一 `m_transformed_slices`，origin 自此消失。下游 `rasterize()`、`PhrozenPRZ` 批次、`RasterCache::compute_key`、`SLASlice2DCanvas` 都只看得到這份 union，AA／灰階／模糊／`picture_grayscale` 一律施加於整張影像。

`origin`（`soModel`/`soSupport`）實際存活到 line 1329 才被抹除（`apply_printer_corrections` 對兩 origin 各跑一次即為證），因此「保留雙軌」不需重新分類，只需不要在 1329 合併。

關鍵既有結構：
- `PrintLayer`（[SLAPrint.hpp:573](../../../src/libslic3r/SLAPrint.hpp#L573)）：私有 `ExPolygons m_transformed_slices`，公開唯讀 `transformed_slices()`，setter 為 `friend SLAPrint::Steps`。
- `SLARasterParams`（[SLAPrint.hpp:481](../../../src/libslic3r/SLAPrint.hpp#L481)）：純值型 struct，被 `compute_key` 以 `reinterpret_cast<const unsigned char*>(&rp), sizeof(rp)` 做 raw-byte 雜湊。
- 兩個光柵化落點必須 byte 一致：主迴圈 [SLAPrintSteps.cpp:1547](../../../src/libslic3r/SLAPrintSteps.cpp#L1547) 與 cache-miss 批次 [PhrozenPRZ.cpp:794](../../../src/libslic3r/Format/PhrozenPRZ.cpp#L794)（既有鐵律，PhrozenPRZ.cpp:809 註明）。
- `expolygons_to_cvmat` 有 in-place 變體（`(cv::Mat& dst, ...)`，[RasterToCvMat.hpp:37](../../../src/libslic3r/SLA/RasterToCvMat.hpp#L37)）供 TLS 重用緩衝。

## Goals / Non-Goals

**Goals:**
- `PrintLayer` 保留 model-track 與 support-track 兩份獨立 transformed ExPolygons，同時不破壞既有 `transformed_slices()`（union）消費者。
- 一般層：model 套 AA／灰階／模糊；support 走 threshold 純二值。底層（`idx < bottom_layer_count`）：model 與 support 皆二值。
- 合成在 `picture_grayscale` LUT **之後**：`composite = max(model_after_LUT, support_255)`，support 恆 255。
- `compute_key` 對「兩條 track」做幾何雜湊、納入底層數、`CACHE_VERSION` bump，且**根治 struct padding 不確定性**。
- 主迴圈與 `PhrozenPRZ` 批次共用同一段雙軌合成邏輯，保證 byte 一致。

**Non-Goals:**
- SL1 PNG 匯出路徑（`sla::Raster`，不經 `expolygons_to_cvmat`）→ 後續技術債。
- 使用者開關 → Always-on。
- 效能優化、AA 演算法本身、UI/GUI 變更（Preview/Thumbnail 於合成後擷取自動繼承）。

## Decisions

### D1. PrintLayer 雙軌資料結構（保留 union 相容性）

> **⚠️ 已更新（記憶體優化 Opt-1）**：本節原定「持久儲存 3 份幾何（union + model + support）」已於後續的**記憶體優化 Opt-1 中反轉**，改為**兩軌持久、union 隨選計算**（移除 `m_transformed_slices`，`transformed_slices()` by-value 即時 `union_ex`）。下方原始敘述保留作演進紀錄，現況請以本文末「記憶體峰值優化」章節為準。

`PrintLayer` 私有新增兩份欄位，並保留既有 union 欄位供 legacy 消費者：

```cpp
ExPolygons m_transformed_slices;           // 既有：model ∪ support（union）— 不改語義
ExPolygons m_transformed_model_slices;     // 新：model-track（含 AA 等處理）
ExPolygons m_transformed_support_slices;   // 新：support-track（永遠二值）
// 對應公開 const 存取子：transformed_model_slices() / transformed_support_slices()
// friend setter 由 Steps 在原 1329 處一次填三份
```

**理由（為何保留 union 而非讓 `transformed_slices()` 變成 model-only）**：`transformed_slices()` 仍被 `SLASlice2DCanvas` 向量 fallback（顯示整層輪廓）與其他幾何來源使用；若改成 model-only 會讓向量預覽掉支撐。記憶體成本：ExPolygons 相對影像（65MB/層）微不足道，存三份可接受，且避免任何消費者回算 union。

**替代方案（捨棄）**：只存兩 track、`transformed_slices()` 即時 `union_ex` 合併 → 每次 legacy 呼叫付出布林運算成本，且向量預覽每層重算，得不償失。

### D2. SLARasterParams 擴充 + compute_key 改為 field-wise 雜湊（根治 padding）

新增底層數欄位：

```cpp
struct SLARasterParams {
    ...
    int     blur_pixel        = 0;
    int     bottom_layer_count = 0;   // 新：底層二值門檻；置於 int 群以利對齊
    Point   shift;
    uint8_t picture_grayscale = 255;
};
```

**padding 根治（核心決策）**：目前 `compute_key` 以 `sizeof(rp)` raw-byte 雜湊，而 struct 本就含 padding（`gray_lo/gray_hi` 後、`picture_grayscale` 尾端），且建構點 [SLAPrintSteps.cpp:1459](../../../src/libslic3r/SLAPrintSteps.cpp#L1459) 為 `SLARasterParams rp;`（default-init，**padding 未歸零**）——這是既有潛在的非決定性雜湊風險（跨進程的磁碟 cache 尤甚）。

決策：**`compute_key` 改為逐欄位雜湊**（res/pxdim/trafo 各成員、gamma、aa_steps、gray_lo、gray_hi、blur_pixel、bottom_layer_count、shift.x/y、picture_grayscale），完全不碰 padding bytes。如此新增欄位也不需擔心佈局。

**防禦縱深**：同時把建構點改為 `SLARasterParams rp{};`（value-init 會將 padding 歸零），即使未來有人退回 raw-byte 路徑也安全。

**`CACHE_VERSION` 必須 bump**（雜湊語義改變 + 新增因子）。

**替代方案（捨棄）**：維持 raw-byte 但 `memset(&rp,0,sizeof rp)` → 仍依賴「copy 會保留 padding」的實作定義行為（`m_raster_params` 為 `std::optional` 複製），脆弱。

### D3. compute_key 幾何雜湊走「雙 track」

`compute_key`（[RasterCache.cpp:36](../../../src/libslic3r/SLA/RasterCache.cpp#L36)）目前對 `transformed_slices()`（union）取點雜湊。改為**依序雜湊 model-track 再 support-track 的 contour/holes 點**（含一個 track 分隔標記以避免邊界歧義）。

**理由**：若只雜湊 union，「同一份 union 幾何但 model↔support 分類不同」理論上輸出不同卻同 key → stale hit。雖然多數分類變動必然反映在某條 track 的點集差異，但直接雜湊兩 track 是唯一無歧義的正確作法。

### D4. 共用「單層雙軌光柵」自由函式（強制兩路徑 byte 一致）

抽出一支供主迴圈與 `PhrozenPRZ` 批次共同呼叫的函式，封裝「依 `is_binary` 決定 model 是否走二值 → support 二值 → LUT → 合成」：

```cpp
// 形如（RasterToCvMat 或新 helper 內）：
void rasterize_layer_dual(
    cv::Mat            &dst,           // 重用 TLS，輸出 = composite
    cv::Mat            &support_tmp,   // 重用 TLS support 緩衝
    const ExPolygons   &model_polys,   // 已平移 rp.shift
    const ExPolygons   &support_polys, // 已平移 rp.shift
    const SLARasterParams &rp,
    bool                is_binary);    // = (lid < rp.bottom_layer_count)
```

內部流程：

```
model 光柵：
   is_binary ? expolygons_to_cvmat(dst, model_polys, ... gamma=0, aa_steps=0, blur=0)
             : expolygons_to_cvmat(dst, model_polys, ... rp.gamma, rp.aa_steps,
                                   rp.gray_lo, rp.gray_hi, rp.blur_pixel)
apply_picture_grayscale_lut(dst, rp.picture_grayscale)   // model 受全域減光（含底層）

support 光柵：
   expolygons_to_cvmat(support_tmp, support_polys, ... gamma=0, aa_steps=0, blur=0)  // 恆二值
   // 不套 LUT → support 恆 255

合成：
   for each pixel: dst[i] = max(dst[i], support_tmp[i])   // support 255 必勝、且 <50% 覆蓋不汙染 model
```

**理由**：`max()` 合成使 support 在 LUT **之後**蓋上純 255，且 support sub-pixel（<50% 覆蓋 → 0）不會把 model 變暗。兩呼叫端只是準備好 `dst`/`support_tmp`（TLS）與 `is_binary` 後委派此函式，杜絕邏輯漂移。

`PhrozenPRZ` 端在此函式回傳後，照舊接 `cv::rotate(ROTATE_90_CLOCKWISE)` + `prz_orient_after_rotate(prz_x_mirror)`（與主迴圈一致），維持既有方位處理。

### D5. 底層 predicate 與 faded 正交

- `is_binary = (lid < rp.bottom_layer_count)`；`lid` 為 `printer_input` 全域索引，因 `PrintLayer::operator<` 以 `m_level` 升冪排序，前 `blc` 層即最低 Z 之底層。
- 大象腳補償（faded）為**幾何**前處理（`apply_printer_corrections`），早於光柵化完成；`has_efc = idx < faded_layers` 與 `is_binary` 為**獨立 predicate**，設計中不得互相引用或假設範圍相等。

### D6. TLS 緩衝

主迴圈既有 `TLSData{ mat, mat_rotated, rle_buf, thumb, thumb_rle }`（[SLAPrintSteps.cpp:1526](../../../src/libslic3r/SLAPrintSteps.cpp#L1526)）新增一個 `cv::Mat support_mat`，沿用既有「首次配置、其後零 malloc」契約。Thumbnail 仍於合成後（現 line 1572 位置）擷取，自動繼承雙軌結果。

## Risks / Trade-offs

- **多物件 / 非 z=0 起始層的底層映射** → `bottom_layer_count` 為 print 層級單一值，`is_binary = lid < blc` 假設「全域前 blc 層 == 貼床底層」。單物件 z=0 成立；多物件不同起始高度時，per-object 底層與全域索引可能不一致。**緩解**：實作前確認 `printer_input` 建構是否保證底部對齊；多物件情境列入測試案例，必要時改以 per-object 底層集合標記。（Open Question）
- **峰值記憶體 +1 Mat/緒（最多 +520MB）** → 緩解：`support_mat` 走 TLS 重用，不逐層配置；未來可改 ROI-only 光柵壓低。
- **support sub-pixel 消失**（threshold <50% 覆蓋 → 0）→ 視為**可接受且符合初衷**（≥50% 反而比 AA 強；<50% 灰階本難固化）。文件化，不另處理。
- **兩光柵路徑漂移** → D4 共用函式為唯一真相源；新增/修改務必只改一處。
- **既有 cache 失效** → `CACHE_VERSION` bump 使舊 cache 全失效並自然重建；無檔案格式或 profile 變更。
- **效能淨成本（+30~60% 光柵化）** → 已於 proposal 誠實定性；以列印可靠度辯護，非速度。
- **向量預覽仍顯示 union** → `SLASlice2DCanvas` fallback 為向量輪廓（不顯 AA），維持 union 不影響觀感。

## Open Questions

- 多物件 / 非 z=0 起始時，`bottom_layer_count` 是否需由全域 `lid` 改為 per-object 底層集合？需確認 `printer_input` 底部對齊保證。
- `expolygons_to_cvmat` in-place 變體是否在 render 前清空 `dst`？若否，support_tmp 重用需顯式清零以免殘留上一層內容（實作時驗證）。
  - **已解決（階段三查證）**：fast path `dst.setTo(0)`、slow path 全幀 `memset` 皆完整清空 → 不需顯式清零。

## 記憶體峰值優化（階段四後新增）

階段四落地後使用者回報切片並行期間峰值 RAM 顯著飆升。複查確認「合成函式隱式 clone/copy」與「support_mat 每層重配置」**皆不成立**（`rasterize_layer_dual` 全 in-place、`cv::max` in-place、support_mat 為 TLS 每緒一次重用）。真實來源有二：

- **來源 A（主因）**：`TLSData` 新增全幀 `support_mat`（~65MB/緒），8 緒 +~520MB。設計性、非 leak。
- **來源 B（次因・可消除）**：`m_printer_input` 對全 N 層同時持有 union＋model＋support 三份幾何；階段四後 union 唯一消費者僅 [SLASlice2DCanvas.cpp:701](../../../src/slic3r/GUI/SLASlice2DCanvas.cpp#L701)（單層隨選），為全 N 層存 union 已純浪費。

### 已實作：Opt-1 — 丟棄持久 union（消除來源 B）

- `PrintLayer` 移除 `m_transformed_slices` 成員與 setter；`merged_input_to_slices()` 不再建立/儲存 union。
- `transformed_slices()` 改為 by-value，定義於 `SLAPrint.cpp`，隨選計算 `union_ex(model ∪ support)`（唯一消費者為單層向量預覽，成本可忽略）。
- 效益：全 N 層各省一份 union 幾何；順帶省每層一次 `union_ex`。不改 PRZ bytes。

### 後續技術債：Opt-2 — support ROI 合成（根治來源 A，尚未實作）

全幀 `support_mat`（65MB/緒）可由 ROI-only 合成取代：

- 新增 `composite_support_binary(mat, support_polys, res, pxdim, trafo)`，重用既有 [compute_pixel_roi](../../../src/libslic3r/SLA/RasterToCvMat.cpp#L109) 機制，僅在 support bbox ROI 內以 TLS `small_buf` 光柵化（outer→255、holes→0，**正確處理 diff 造成的洞**），再 `cv::max(mat(roiRect), small_buf_mat, mat(roiRect))`。
- `rasterize_layer_dual` 改呼叫此函式取代「全幀 support_tmp + cv::max」；移除主迴圈 `TLSData.support_mat` 與 PhrozenPRZ 的 `support_tls`。
- 注意：naive `cv::fillPoly(mat, support_outer, 255)` 直接合成**無法保留 support 洞內的 model 像素**，故仍需小 ROI 緩衝而非直接填色。
- 約束：ROI 合成輸出須與全幀 `cv::max` **逐像素相同**（byte-identical）。
- 效益：8 緒省回 ~520MB。

### 臨時手段：Opt-3 — 調降並行數

- `RASTERIZE_CONCURRENCY` 8→4 可即時砍半 `support_mat` 佔用；純 tuning、不影響 bytes、吞吐減半。作為 Opt-2 之前的暫緩選項。