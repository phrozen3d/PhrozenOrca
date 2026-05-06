## Context

### 現況

SLA 切片管線的最後一步 `slapsRasterize` 在 `SLAPrintSteps.cpp::rasterize()` 中原本包含兩條並行的管線：

- **Pipeline 1**（已移除）：呼叫 `draw_layers()` → `SLAArchiveWriter::m_layers`（SL1 格式 PNG 編碼），影像存入 `SL1Archive::m_layers`。經查明 Phrozen Orca 所有匯出均透過 `generate_prz()` 直接完成，`SL1Archive::export_print()` 在 Phrozen Orca 的匯出流程中從不被呼叫，因此 `m_layers` 永遠為死資料；Pipeline 1 的 AGG 繪圖與 `PNGRasterEncoder` 壓縮屬於無效工作，已整體移除
- **Pipeline 2**（已移除）：收集所有切層的 ExPolygons → 呼叫 `expolygons_layers_to_cvmat()`（TBB 平行）→ 結果存入 `m_layer_images`，套用 `picture_grayscale` LUT → **常駐記憶體**

`m_layer_images` 為 `std::vector<cv::Mat>`（每張約 23 MB，Mega 8K 解析度 6480×3600，CV_8UC1），100 層即超過 2.3 GB，直到下次重切或關閉應用程式前不會釋放。

目前有兩個讀取端：
1. `generate_prz()`：匯出 PRZ 時循序讀取每層像素，進行 RLE 編碼
2. `SLASlice2DCanvas::render()`：GUI 切層預覽，以 `m_layer_idx` 索引存取單層

### 約束

- `m_printer_input`（`std::vector<PrintLayer>`）在 `slapsRasterize` 之後持續有效，持有 ExPolygon 資料，為 on-demand 光柵化的唯一資料來源
- `expolygons_to_cvmat()`（單層 API）已存在於 `RasterToCvMat.hpp`，無需新增 API
- TBB `parallel_for` 效能必須在批次大小合理的情況下保留

---

## Goals / Non-Goals

**Goals:**

- 將匯出時的記憶體峰值由 ≥2.3 GB 降至 ≤200 MB（批次 8 層）
- 移除 `slapsRasterize` 的雙重光柵化延遲，使匯出按鈕在 Pipeline 1 完成後即可點擊
- `SLARasterParams` 封裝所有光柵化參數，使任意呼叫端可獨立重現任何切層的影像
- GUI 預覽改為 Strategy A（同步單層快取），記憶體恆為 ≤1 張影像

**Non-Goals:**

- BATCH_SZ 不作為 UI 可設定項（硬編碼 8 作為初始值）
- 異步 GUI 預覽（Strategy B）不在此版本範疇
- `m_layer_images` 的宣告不立即刪除（保留為空向量以避免編譯斷層，後續清理）
- Pipeline 1 對 SL1／Anycubic 等非 PRZ 格式的相容性（Phrozen Orca 不支援這些匯出路徑）

---

## Decisions

### Decision 1：SLARasterParams 為純值型別 struct，置於 SLAPrint.hpp

**做法：** 在 `SLAPrint.hpp` 內（`SLAPrint` class 宣告之前）新增：

```cpp
struct SLARasterParams {
    sla::Resolution         res;
    sla::PixelDim           pxdim;
    sla::RasterBase::Trafo  trafo;
    double                  gamma        = 1.0;
    int                     aa_steps     = 0;
    uint8_t                 gray_lo      = 0;
    uint8_t                 gray_hi      = 255;
    int                     blur_pixel   = 0;
    Point                   shift;         // bed→display 座標平移（coord_t × 2，8 bytes）
    uint8_t                 picture_grayscale = 255;  // LUT 量化（SLAPrinterConfig）
};
```

**理由：**
- `sla::Resolution`、`sla::PixelDim`、`sla::RasterBase::Trafo` 均為 POD struct（宣告於 `RasterBase.hpp`），無指標、無參考
- `Point`（`Vec2crd`，`Eigen::Matrix<coord_t, 2, 1>`）為值型別，8 bytes，完全安全複製
- 所有欄位皆為原始型別或值型別 → **零生命週期風險、零懸空指標風險**
- 置於 `SLAPrint.hpp` 使 `PhrozenPRZ.cpp` 及 `SLASlice2DCanvas.cpp` 均可直接 include 使用

**替代方案：** 僅傳遞獨立參數給 `generate_prz()` → 耦合性高、不易擴展、GUI on-demand 重用困難

---

### Decision 2：批次並行（Batched On-Demand）策略用於 PRZ 匯出

**做法：** 在 `generate_prz()` 中，以固定批次大小 `BATCH_SZ = 8` 迭代所有切層：

```
const SLARasterParams& p = *print.raster_params();
const size_t N = print.print_layers().size();
constexpr size_t BATCH_SZ = 8;

for (size_t batch_start = 0; batch_start < N; batch_start += BATCH_SZ) {
    size_t batch_end = std::min(batch_start + BATCH_SZ, N);
    size_t batch_len = batch_end - batch_start;

    // [A] 收集批次 ExPolygons（需 translate(p.shift) 座標轉換）
    std::vector<ExPolygons> batch_polys(batch_len);
    for (size_t i = 0; i < batch_len; ++i) {
        const auto& pl = print.print_layers()[batch_start + i];
        batch_polys[i] = pl.transformed_slices();        // ExPolygons (value copy)
        for (auto& ep : batch_polys[i])
            ep.translate(p.shift);
    }

    // [B] TBB 平行光柵化
    std::vector<cv::Mat> batch_mats(batch_len);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, batch_len),
        [&](const tbb::blocked_range<size_t>& r) {
            for (size_t i = r.begin(); i < r.end(); ++i) {
                batch_mats[i] = sla::expolygons_to_cvmat(
                    batch_polys[i], p.res, p.pxdim, p.trafo,
                    p.gamma, p.aa_steps, p.gray_lo, p.gray_hi, p.blur_pixel);
                // 套用 picture_grayscale LUT
                apply_picture_grayscale_lut(batch_mats[i], p.picture_grayscale);
            }
        });

    // [C] Sequential：RLE 編碼 + 即時釋放
    for (size_t i = 0; i < batch_len; ++i) {
        size_t lid = batch_start + i;
        // prz_layer_content（metadata header）
        buf += prz_layer_content(print, lid);
        // RLE 編碼像素
        buf += rle_encode_mat(batch_mats[i]);
        batch_mats[i].release();   // 立即釋放，不等待批次結束
    }
}
```

**理由：**
- TBB `parallel_for` 在批次內保留多核心效能（8 層並行光柵化 vs 逐層序列）
- 批次結束後記憶體立即釋放，峰值 ≈ `BATCH_SZ × 23 MB ≈ 184 MB`
- 原始程式碼的序列 RLE 迴圈結構完整保留，diff 清晰

**替代方案（全序列 One-by-One）：** 峰值記憶體降至 1 層（~23 MB），但喪失 TBB 效能，匯出速度下降 4~8×

**替代方案（保留全批次再釋放）：** `batch_mats[i].release()` 移至批次外 → 峰值仍為 `BATCH_SZ × 23 MB`，差異不大，不如即時釋放清晰

---

### Decision 3：GUI 預覽採 Strategy A（同步單層快取）

**做法：** 修改 `SLASlice2DCanvas::render()`：

```cpp
// 舊：讀 m_print->layer_images()[m_layer_idx]
// 新：
if (m_print->is_step_done(slapsRasterize) && m_print->raster_params().has_value()) {
    if (m_cached_layer != m_layer_idx) {
        const auto& p = *m_print->raster_params();
        const auto& pl = m_print->print_layers()[size_t(m_layer_idx)];
        ExPolygons polys = pl.transformed_slices();
        for (auto& ep : polys) ep.translate(p.shift);
        cv::Mat mat = sla::expolygons_to_cvmat(
            polys, p.res, p.pxdim, p.trafo,
            p.gamma, p.aa_steps, p.gray_lo, p.gray_hi, p.blur_pixel);
        apply_picture_grayscale_lut(mat, p.picture_grayscale);
        // 上傳 texture，mat 離開 scope 即釋放
        destroy_texture();
        m_cached_layer = m_layer_idx;
        if (sla_layer_mat_has_visible_ink(mat))
            upload_grayscale_texture(mat.ptr<unsigned char>(), mat.cols, mat.rows);
    }
    // render texture...
}
```

**理由：**
- 實作最簡，layer slider 滑動時每層約耗 30~100 ms（單層光柵化），對互動式預覽可接受
- `m_cached_layer` 快取確保同一層不重複計算
- 記憶體永遠 ≤ 1 張影像（~23 MB）

**替代方案（Strategy B 異步）：** 背景執行緒光柵化，layer_images 向量作 fallback → 實作複雜、增加 race condition 風險，非此版本目標

---

### Decision 4：`apply_picture_grayscale_lut()` 集中管理於 `RasterToCvMat`

`picture_grayscale` LUT 套用邏輯（原先在 `rasterize()` 的 Pipeline 2）改為共用的函式，宣告於 `src/libslic3r/SLA/RasterToCvMat.hpp` 並實作於 `RasterToCvMat.cpp`：

```cpp
// RasterToCvMat.hpp
namespace sla {
void apply_picture_grayscale_lut(cv::Mat& mat, uint8_t level);
}

// RasterToCvMat.cpp
namespace sla {
void apply_picture_grayscale_lut(cv::Mat& mat, uint8_t level) {
    if (level == 255) return;   // fast path：無需處理
    // 建立 256 項 LUT，將 255 映射至 level，線性縮放
    uchar lut_data[256];
    for (int i = 0; i < 256; ++i)
        lut_data[i] = static_cast<uchar>(i * level / 255);
    cv::Mat lut(1, 256, CV_8UC1, lut_data);
    cv::LUT(mat, lut, mat);
}
}
```

由於此 helper 在 `PhrozenPRZ.cpp` 及 `SLASlice2DCanvas.cpp` 中均需使用，**將其集中管理**可避免維護上的風險（例如兩邊邏輯不同步），並防止因各自宣告 `static` 而造成的二進制檔案膨脹（Binary Bloat）。

---

### Decision 5：徹底移除 Pipeline 1（`draw_layers()` + `PNGRasterEncoder`）

**背景：** 實作 on-demand 架構後，手動測試發現 `slapsRasterize` 仍持續執行「Rasterizing Layers」（呼叫 `PNGRasterEncoder::operator()`），切片尚未完成但 GUI 已能顯示預覽圖。

**調查結論：**
- `BackgroundSlicingProcess` 固定使用 `SL1Archive` 作為 `m_printer`，其 `draw_layers()` 會 PNG 編碼全部切層並存入 `SL1Archive::m_layers`
- Phrozen Orca 的 PRZ 匯出完全透過 UI 觸發 `generate_prz()`，**從不呼叫** `SL1Archive::export_print()`
- 因此 `m_layers` 在整個 session 中是死資料，Pipeline 1 屬於 100% 無效工作

**做法：** 在 `rasterize()` 中移除以下程式碼：
- `lvlfn` lambda（AGG 繪圖 + `PNGRasterEncoder` encode 呼叫）
- `m_print->m_printer->draw_layers(...)` 呼叫
- `slck` spinlock、`Lock` typedef、`increment` / `dstatus` / `pst` 等進度追蹤變數

保留：
- `raster_shift` 計算（`SLARasterParams.shift` 欄位需要）
- `m_raster_params` 填充邏輯（PRZ 匯出與 GUI 預覽共用）

**Impact：** `slapsRasterize` 步驟從「對每層做 AGG 向量繪圖 + PNG 壓縮（數秒~數十秒）」縮短為「純參數數學計算（毫秒級）」，徹底消除切片過程的效能瓶頸，記憶體峰值不再於切片期間出現。

---

### Decision 6：`generate_prz()` 改為串流寫檔 API（方向 A）

**問題：** Batched On-Demand 實作後，`cv::Mat` 的記憶體峰值已降至 ~184 MB，但匯出期間仍有 ~1 GB 記憶體飆升。原因是 `generate_prz()` 將所有層的 RLE 資料逐一追加至 `std::string out`（初始 reserve 64 MB，最終 grow 至 ~1 GB），完成後才整批 `ofs.write()` 寫入磁碟。

**做法：**
1. 將 `write_be` 增加 `std::ostream &` 的 overload（不改動原 `std::string &` 版本）
2. `generate_prz()` 簽章改為 `void generate_prz(std::ostream &out, ...)`（`PhrozenPRZ.hpp` / `.cpp`）
3. `prz_header()` 與 `prz_layer_content()` 保持 `std::string &` 介面（靜態 helper，僅在 .cpp 內使用），在 `generate_prz()` 中以小型本地 string 接住後立即 `out.write()`
4. `przByte`（per-layer RLE buffer）建構完畢後立即 `out.write()` 寫入磁碟，不再 `out += przByte`
5. `Plater::export_prz()` 改為先開啟 `std::ofstream ofs`，再呼叫 `generate_prz(ofs, ...)`

**效果：** 匯出期間記憶體恆為 ≤184 MB（cv::Mat 批次）+ 每層 przByte（~1–5 MB），合計 <200 MB

**替代方案（將 prz_header / prz_layer_content 也改為 ostream）：** 改動行數更多、diff 更大，但收益相同（header 本身僅 ~1.2 KB，layer content ~60 bytes）；不值得

---

### Decision 7：背景執行緒（方向 B，尚未實作）

**問題：** `export_prz()` 在 UI 主執行緒同步執行，整個匯出期間介面凍結。

**計畫做法：**
- 新增 `ExportPRZJob : public Job`（繼承 `src/slic3r/GUI/Jobs/Job.hpp`），於 `process(Ctl &ctl)` 中呼叫 `generate_prz(ofs, ...)`，透過 `ctl.update_status()` 回報每批次進度（progress = batch_start * 100 / N）
- 修改 `Plater::export_prz()`：建構 `ExportPRZJob`，透過現有 `PlaterWorker` / `BoostThreadWorker` 提交，解除主執行緒封鎖
- 在 Job 的 `finalize()` 回 UI thread 顯示成功訊息或錯誤

**尚未實作原因：** 方向 A 串流寫檔已解決記憶體問題；UI 凍結問題待下一輪處理

---

## Risks / Trade-offs

| 風險 | 緩解策略 |
|------|----------|
| GUI layer slider 快速滑動時每層觸發光柵化，主執行緒可能卡頓 | `m_cached_layer` 快取避免重複計算；單層光柵化 ≤100 ms 在多數情況下可接受；若仍不滿意可後續升級至 Strategy B |
| `print_layers()[i].transformed_slices()` 傳回 ExPolygons 副本，每批次有記憶體分配開銷 | 批次大小 8 層，副本量有限；ExPolygons 相較影像資料量小得多 |
| `prz_header()` 改用 `print_layers().size()` 可能與實際光柵化層數不一致（若有 empty layer 被跳過的邏輯） | 需確認原始 Pipeline 2 的 `all_layers` 建構是否與 `print_layers()` 一一對應；若有過濾邏輯需同步保留 |
| `m_raster_params` 被 `invalidate_step(slapsRasterize)` 重設時機不對 | 在 `SLAPrint::invalidate_step()` 的 `slapsRasterize` 分支中重設 `m_raster_params = std::nullopt` |
| BATCH_SZ 硬編碼為 8，不同機器 TBB 執行緒數可能有差異 | 初期可接受；後續可改為 `std::max(1u, tbb::this_task_arena::max_concurrency())` 動態決定 |

---

## Migration Plan

1. 新增 `SLARasterParams` struct 及 `m_raster_params`（header + invalidation）
2. 在 `RasterToCvMat.hpp/cpp` 中新增並實作 `sla::apply_picture_grayscale_lut()`
3. 修改 `rasterize()`：移除 Pipeline 2，填入 `m_raster_params`
4. 修改 `generate_prz()` 及 `prz_header()`：Batched On-Demand 邏輯
5. 修改 `Plater.cpp`：匯出按鈕 guard
6. 修改 `SLASlice2DCanvas::render()`：Strategy A
7. 編譯確認（無 `m_layer_images` 殘留讀取端）
8. **移除 Pipeline 1**：從 `rasterize()` 中刪除 `draw_layers()` 呼叫、`lvlfn` lambda 及相關進度追蹤變數（Decision 5）
9. 手動測試：PRZ 匯出正確性、GUI 預覽正確性、記憶體峰值確認、切片速度確認

無需資料庫遷移或網路協定變更。Rollback 為 Git revert。

## Open Questions

1. `print_layers()[i].transformed_slices()` 的 layer ordering 是否與原始 `all_layers` 建構完全一致？（需確認 `SLAPrintSteps.cpp::rasterize()` 的 `all_layers` 建構邏輯）
