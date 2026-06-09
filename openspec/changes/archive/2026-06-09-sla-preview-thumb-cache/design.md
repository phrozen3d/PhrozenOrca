## Context

SLA 切層預覽（[SLASlice2DCanvas](src/slic3r/GUI/SLASlice2DCanvas.cpp)）目前依 `sla-on-demand-preview` 在 UI render thread 上 on-demand 呼叫 `sla::expolygons_to_cvmat()`。當 `gamma > 0` 或 `blur_pixel >= 2` 時走 AGG sub-pixel 慢速路徑，逐層光柵化在單執行緒上執行，造成滑動卡頓；且全解析度結果因 GL texture 上限（16.77M px）被丟棄。

切片時的 `rasterize()` Phase 5（[SLAPrintSteps.cpp:1474-1679](src/libslic3r/SLAPrintSteps.cpp#L1474-L1679)）已在 `tbb::task_arena(8)` 內把全層光柵化為 PRZ-RLE 並落地 `RasterCache`（[RasterCache.cpp](src/libslic3r/SLA/RasterCache.cpp)）。本設計新增一份「降採樣預覽快取（thumb）」搭上同一條平行管線，並把預覽改為純讀取，使 UI thread 永不觸發 AGG。

關鍵既有約束：
- Phase 5 worker 的像素方位演進：`mat`（panel）→ `apply_picture_grayscale_lut`（line 1559）→ `cv::rotate(90°CW)`→`mat_rotated`（line 1566）→ `prz_orient_after_rotate`（line 1573，PRZ X-mirror）→ RLE。
- worker 以 `tbb::enumerable_thread_specific<TLSData>` 重用 `cv::Mat mat / mat_rotated / rle_buf`，**刻意不釋放**（[line 1650-1651](src/libslic3r/SLAPrintSteps.cpp#L1650-L1651)），以避開每層 65MB malloc/free 造成的 CRT heap lock 競爭。
- `RasterCache::is_valid()` 是單一 `cache_complete` sentinel；cache-hit 時 Phase 5 在 [line 1488](src/libslic3r/SLAPrintSteps.cpp#L1488) 直接 `return`。
- 預覽 `render()` 每次重繪/滑鼠移入 slider 都會被呼叫；`compute_key()` 會對全模型幾何跑 CRC32。

## Goals / Non-Goals

**Goals:**
- UI 預覽路徑在 render thread 上**永不**呼叫 `expolygons_to_cvmat`（AGG）或全解析度解碼。
- thumb 與全解析度 `.rle` 在同一 worker 內生成、共用存活性（liveness binding）。
- 列印輸出與 PRZ `.rle` bytes **bit-perfect 不變**；thumb 為旁路產物。
- 記憶體峰值不隨層數 N 線性成長（沿用 TLS 重用）。

**Non-Goals:**
- 不更換 AGG 演算法、不改 AA / blur 語意、不追求 thumb 與列印逐像素一致（thumb 是預覽近似）。
- 不改 cache-hit 早退路徑（[line 1488](src/libslic3r/SLAPrintSteps.cpp#L1488)）為「兩套獨立檢查」——改以 `CACHE_VERSION` 遞增使舊快取整體失效。
- 不引入新外部相依。

## Decisions

### D1：thumb 擷取時機 —— TBB worker 內、panel 方位、rotate 之前
在 Phase 5 worker 迴圈中，於 [line 1559 `apply_picture_grayscale_lut(mat)`](src/libslic3r/SLAPrintSteps.cpp#L1559) **之後**、[line 1566 `cv::rotate`](src/libslic3r/SLAPrintSteps.cpp#L1566) **之前**插入 thumb 擷取：

```
expolygons_to_cvmat → mat                         (panel 方位)
apply_picture_grayscale_lut(mat)                  (LUT 已烙入)   ← line 1559
┌─── 【新增 thumb 擷取】───────────────────────────────────────┐
│  scale = 4096 / max(mat.cols, mat.rows)                       │
│  if (scale < 1.0):                                            │
│      cv::resize(mat → tls.thumb, dsize(round), 0,0,          │
│                 cv::INTER_AREA)        // 等比、抗鋸齒友善     │
│  else:                                                        │
│      tls.thumb = mat              (小於上限則原樣，仍 < 16M)  │
│  rle_encode_gray(thumb_src, w, h, tls.thumb_rle)  // 灰階RLE │
│  RasterCache::write_thumb(key, lid, thumb_rle) // 失敗 throw  │
└──────────────────────────────────────────────────────────────┘
cv::rotate(mat → mat_rotated, 90°CW)               ← line 1566（不受影響）
prz_orient_after_rotate(mat_rotated, x_mirror)     ← line 1573（不受影響）
RLE 編碼 mat_rotated → write_layer(.rle)
```

**理由**：thumb 取自 panel 方位的 `mat`，與目前 preview on-demand 路徑的輸入方位一致，故 preview 既有的 `render_texture_letterbox` / `uvs_rot90_cw` 顯示旋轉、letterbox 邏輯**完全不需改動**，thumb 成為 drop-in。`picture_grayscale` 已於 line 1559 烙入，thumb 自帶正確亮暗度。
**替代方案（否決）**：取自 `mat_rotated`（PRZ 方位）→ preview 會再套一次 `uvs_rot90_cw` + X-mirror，畫面雙重旋轉/翻面。

### D2：降採樣參數 —— `INTER_AREA`、長邊 ≤ 4096、等比
以單一 scale = `4096 / max(w,h)` 對兩軸等比縮放，保 aspect ratio 與 letterbox 一致；最大尺寸（4096 × 短邊）必然 < 16.77M px，符合 [upload_grayscale_texture](src/slic3r/GUI/SLASlice2DCanvas.cpp#L549-L587) 上限。降採樣用 `cv::INTER_AREA`（面積平均，對 AA 灰階邊緣表現最佳）。
**替代方案（否決）**：`INTER_LINEAR` 在大倍率縮小時 aliasing 明顯；固定 dsize 不等比會破壞 letterbox 比例。

### D2.5：thumb 編碼格式 —— 自訂輕量灰階 RLE（不用 OpenCV imgcodecs/PNG）
thumb **不**經 `cv::imencode`/`cv::imdecode`。原因:使用 imgcodecs 會把 `opencv_world` 內建的 **第二份 libjpeg-turbo** 拉進連結，與 deps/JPEG 既有的 `jpeg-static.lib`(亦為 libjpeg-turbo)的 `jpeg_stdio_dest` 撞符號 → **LNK2005**。改在 `RasterCache` 新增一對對稱的輕量灰階 RLE 編解碼器:
- 格式:`[u32 w LE][u32 h LE]` + 連續的 `(value:u8, run:u8 ∈ 1..255)` 對。
- `rle_encode_gray(data, w, h, out)`:worker 端編碼,填入 TLS `out`(重用容量,零 per-layer realloc)。
- `rle_decode_gray(in, out, w, h)`:UI 端 ~30 行解碼回灰階 buffer。
**替代方案（否決）**：(a) CMake `/NODEFAULTLIB` 或 `/FORCE:MULTIPLE` — 對命令列明確列入的庫可能無效,且兩份 libjpeg-turbo 版本不一致有 ABI 當機風險;(b) 直接用 libpng — opencv_world 同樣內建 libpng,恐重演 png 符號衝突;(c) raw 未壓縮 — 磁碟過大(~9MB/層)。輕量 RLE 同時避開連結衝突、保持小檔、encode/decode 同源。

### D3：TLSData 記憶體重用與「清空」機制
擴充既有 `TLSData`（[line 1526-1531](src/libslic3r/SLAPrintSteps.cpp#L1526-L1531)）：

```cpp
struct TLSData {
    cv::Mat            mat;          // 既有：全解析度 (panel)，跨層重用
    cv::Mat            mat_rotated;  // 既有：PRZ landscape，跨層重用
    std::vector<char>  rle_buf;      // 既有：RLE buffer，跨層重用
    cv::Mat            thumb;        // 新增：降採樣目的地，跨層重用
    std::vector<uchar> thumb_rle;    // 新增：灰階 RLE 編碼輸出，跨層重用
};
```

**生命週期規則（與既有一致，杜絕 CRT heap lock 與 OOM）：**
- 全解析度 `mat` / `mat_rotated` **不銷毀**，續供下一層重用（沿用既有契約，收回先前「立即釋放」的構想）。
- `thumb`：以 `cv::resize(src, tls.thumb, ...)` 為**目的地**——OpenCV 在 size/type 相符時**原地重用既有配置**，僅首層或尺寸改變時才重新配置（各機型逐層尺寸固定，等同只配一次）。
- `thumb_rle`：`rle_encode_gray(..., tls.thumb_rle)` 內部先 `clear()`（保留容量）再 `push_back`，跨層重用既有配置、零 per-layer realloc。所謂「清空」指**邏輯覆寫**，**不做釋放配置**。
- 記憶體峰值：`8 threads × (65MB mat + 65MB rotated + rle_buf + ~thumb 9MB + thumb_rle)`，為常數上限，**不隨 N 成長**。

### D4：存活性綁定（Liveness Binding）
不改 cache-hit 早退邏輯。改採：
- `RasterCache::write_thumb()` 比照 [`write_layer`](src/libslic3r/SLA/RasterCache.cpp#L71-L85)，開檔/寫檔失敗即 `throw std::runtime_error`。例外傳出 `tbb::parallel_for` → `mark_complete()`（[line 1678](src/libslic3r/SLAPrintSteps.cpp#L1678)）不被呼叫 → 整份快取 `is_valid()==false`。
- 故 sentinel 存在 ⟺「全部層的 `.rle` **與** `_preview.rle` 皆成功落地」，確立不變量「RLE valid ⟹ thumb 必存在」。
- 遞增 `CACHE_VERSION`（5 → 6），舊版（無 thumb）快取因 key 不符整體失效並重建，免去 cache-hit 路徑加裝第二套檢查。
**替代方案（否決）**：thumb 自帶獨立 sentinel + 改寫 line 1488 雙檢查——增加複雜度且 export 與 preview 失效耦合更難推理。

### D5：UI 端 cache_key 節流（成員變數快取）
於 `SLASlice2DCanvas` 新增成員：
```cpp
std::optional<sla::RasterCacheKey> m_cache_key;   // 已快取的 key
const SLAPrint*                    m_print = ...;  // 既有
```
- 失效時機：`set_sla_print()` / `reset_print()` 內 **`m_cache_key.reset()`**（標記失效，不在此計算）。
- 計算時機：**綁定後於 `render()` 內惰性計算一次，並以 emptiness guard 鎖定**——
  `if (!m_cache_key && is_step_done(slapsRasterize) && raster_params().has_value()) m_cache_key = compute_key(...)`。
  整個綁定週期內 CRC32 **最多執行一次**（rasterize 完成後第一次重繪時），**嚴禁每幀重複執行**；重切片時 `set_sla_print` 再次被呼叫 → `reset()` → 下次 render 再算一次。
- `render()` / `on_gl_mouse_motion()` / 滑 bar 在 key 已存在後 **只使用** `m_cache_key`，以 `m_layer_idx` 組出 `layer_{idx:04d}_preview.rle` 路徑讀檔；除了上述「空值才算一次」的 guard 外，**絕不**呼叫 `compute_key`。
- 維持既有 `m_cached_layer` 單層 texture 快取：同層不重複解碼/上傳。
**理由**：`compute_key` 對全模型幾何跑 CRC32，若每幀執行會引入新卡頓。原「只在 `set_sla_print` 算」不可行——`load_print_as_sla()`（GUI_Preview.cpp）每個 print 僅執行一次，且觸發於 `slaposSliceSupports`（物件切片），早於 `slapsRasterize`，故 `set_sla_print` 當下 `raster_params` 多半為空。emptiness-guarded 惰性求值同時滿足「絕不每幀」與「時序正確 / 重切片可重算」。

### D6：預覽讀取路徑
`render()` 改為：
```
slice_geometry_ready (is_step_done(slapsRasterize) && raster_params().has_value())
  且 m_cache_key 有值 且 0 <= m_layer_idx < N
    → 若 m_cached_layer != m_layer_idx：
        path = key.dir / layer_{idx:04d}_preview.rle
        若檔存在：read_thumb → rle_decode_gray → cv::Mat → upload_grayscale_texture（必 < 上限）
        若檔不存在/解碼失敗：不上傳；m_tex_id 維持 0 → drew=false  ← 退向量
    → m_tex_id != 0：render_texture_letterbox（含既有 portrait 旋轉）
未就緒 / thumb 缺席 → render_vector_fallback()
```
**移除**：render thread 上的 `expolygons_to_cvmat` 與 [line 806 `apply_picture_grayscale_lut`](src/slic3r/GUI/SLASlice2DCanvas.cpp#L806)（thumb 已烙入，重套會偏暗）。

### D7：預覽顯示方位實證與 portrait UV 修正（Y-flip）—— 補充 D5/D6 顯示路徑前提
**背景（被 3.9 視覺驗證揭露）**：D1/D6 假設「thumb 取自 panel 方位 → 是既有 `render_texture_letterbox` 的 drop-in」，其隱含前提是「既有顯示路徑方位正確」。實證發現此前提**從未被驗證過**：所有現役 Phrozen 機型（8K=23M、16K=94M px）均超過 GL texture 上限（16.7M）→ 舊路徑一律退向量,**從未顯示過點陣預覽**。thumb 是這些機型史上第一次顯示點陣預覽,因而首次觸發 `render_texture_letterbox` 的方位處理。

**症狀（兩輪攔截）**：(1) 寫死 naive rot90 → Revo 16K Y 顛倒。(2) 改寫死成 V-flip → 16K 正確但 Mega 8K S/V2 Y 顛倒。典型「顧此失彼」。

**根因**：portrait 分支用**全域寫死單一 UV 常數**,完全未讀當前機型 mirror。三台機型唯一本質差異為 `display_mirror_x`(16K=0 → `trafo.mirror_x=true`;8K=1 → `trafo.mirror_x=false`;三台 `display_mirror_y` 皆 0 → `trafo.mirror_y=true`)。在 portrait 的 90° 旋轉下,mat 欄軸(↔世界 Y、由 `trafo.mirror_x` 翻轉)對映到**螢幕 Y**,列軸(↔世界 X、由 `trafo.mirror_y` 翻轉)對映到**螢幕 X**(軸交叉耦合)。故 `trafo.mirror_x` 差異 → 螢幕 Y 相反,單一寫死 UV 數學上不可能相容全系列。codec 為恆等轉換、PRZ 走獨立方位管線,皆**非肇因**。

**決策（動態 UV）**：還原寫死常數,於 [render_texture_letterbox](src/slic3r/GUI/SLASlice2DCanvas.cpp) **依 `m_print->raster_params()->trafo` 動態組 UV**(trafo 為 mat 實際套用的方位,單一真相來源),使預覽一律呈 canonical bed 方位(完全無鏡射)、與 LCD `display_mirror_*` 脫鉤:

| 分支 | base | 翻轉條件 |
|---|---|---|
| portrait | rot90 CW `{lb{1,1},rb{1,0},rt{0,0},lt{0,1}}`(對 `mirror_x=false,mirror_y=true` 正確) | `trafo.mirror_x` → 螢幕垂直翻轉;`!trafo.mirror_y` → 螢幕水平翻轉 |
| landscape | `FullTextureUVs` | `trafo.mirror_x` → 水平;`!trafo.mirror_y` → 垂直 |

在顯示層(共享路徑、僅 SLA 2D 預覽使用)修正,而非 codec/thumb 端硬翻,避免與「drop-in」原則衝突。
**實證錨點**：8K(`trafo.mirror_x=false`)用 base 正確;16K(`trafo.mirror_x=true`)需垂直翻轉。
**Known limitation**：(a) `trafo.mirror_y` 的水平翻轉條件由對稱性推得,目前無 `display_mirror_y=1` 機型可實證;(b) landscape 分支無現役機型,翻轉條件未經驗證。兩者皆 PRZ 不受影響,留待未來出現對應機型時複驗。

## Risks / Trade-offs

- **[thumb 寫檔失敗使整份快取失效，連帶 export 須重切]** → 可接受：磁碟滿屬極端情境；存活性綁定換來「RLE valid ⟹ thumb 必存在」的強不變量，避免 UI 退回 AGG。
- **[worker 每層多一次 `cv::resize` + 灰階 RLE 編碼，略增 rasterize 時間]** → thumb 僅 ≤4096 長邊，`INTER_AREA` + RLE 編碼(純 C++、O(pixels))成本遠小於該層 AGG；在 8-thread 平行下淨增可忽略。
- **[RLE 編碼緩衝配置]** → `rle_encode_gray` 寫入 TLS `thumb_rle`(`clear()` 保留容量再填),跨層零 realloc；無 65MB 級競爭。
- **[thumb 為降採樣近似，與列印非逐像素一致]** → 預覽用途可接受，且 Non-Goal 已明列；列印走 `.rle` 不受影響。
- **[磁碟多出 N 張 RLE 預覽圖]** → 數百 KB/張量級；沿用 `cleanup_old()` 既有清理；`CACHE_VERSION` 遞增使舊目錄自然淘汰。
- **[快速拖曳 bar 觸發大量小 RLE 解碼/上傳]** → RLE 解碼為 O(pixels) memset,每次 ms 級且 `m_cached_layer` 去重；遠優於 AGG。如仍需，未來可加 slider 落定節流（本變更不納入）。
- **[改用自訂 RLE 而非 PNG/imgcodecs]** → 動機是避開第二份 libjpeg-turbo 的 LNK2005 與 ABI 風險(見 D2.5)；代價是放棄 PNG 的高壓縮比,但 SLA 層多為大面積同色,RLE 壓縮已足夠。

## Migration Plan

1. 遞增 `CACHE_VERSION`（5 → 6）：升級後首次切片必為 cache miss，重建含 thumb 的新快取；舊目錄由 `cleanup_old()` 逐步清除。
2. 無資料遷移、無設定變更；行為對使用者透明（除預覽變流暢）。
3. 回退策略：本變更為純新增 + 預覽路徑替換；若需回退，還原 `SLASlice2DCanvas::render()` 與 Phase 5 thumb 區塊，並回退 `CACHE_VERSION` 即可，`.rle` 格式與下游 PRZ 不受影響。

## Open Questions

- thumb 長邊上限定 4096 是否對所有現役/未來機型皆足夠清晰？（目前 8K/16K 皆適用；若日後預覽視窗 DPI 放大需求增加，可調此常數，屬參數而非架構。）
- thumb 是否需與 `.rle` 同 sentinel 之外另存「thumb 尺寸/版本」中繼資料以利除錯？（暫不需要，`CACHE_VERSION` 已涵蓋格式版本。）