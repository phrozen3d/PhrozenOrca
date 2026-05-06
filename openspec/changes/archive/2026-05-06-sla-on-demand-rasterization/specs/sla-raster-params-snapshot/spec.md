## ADDED Requirements

### Requirement: SLARasterParams 封裝光柵化參數快照

`SLAPrint.hpp` 中 SHALL 定義 `SLARasterParams` struct，包含所有重現任一切層 `cv::Mat` 所需的參數，且所有欄位均為值型別（無指標、無參考）。

結構定義：
```cpp
struct SLARasterParams {
    sla::Resolution         res;
    sla::PixelDim           pxdim;
    sla::RasterBase::Trafo  trafo;
    double                  gamma             = 1.0;
    int                     aa_steps          = 0;
    uint8_t                 gray_lo           = 0;
    uint8_t                 gray_hi           = 255;
    int                     blur_pixel        = 0;
    Point                   shift;
    uint8_t                 picture_grayscale = 255;
};
```

#### Scenario: 結構為純值型別
- **WHEN** `SLARasterParams` 被複製或移動
- **THEN** 複製/移動後的物件與原始物件完全獨立，無懸空指標風險

#### Scenario: shift 正確計算
- **WHEN** `rasterize()` 填入 `m_raster_params`
- **THEN** `shift` 欄位為 `display_center - bed_bounding_box_center`（以 `coord_t` 為單位），使 ExPolygon 座標可正確對應到顯示座標系

#### Scenario: picture_grayscale 來自 SLAPrinterConfig
- **WHEN** `rasterize()` 填入 `m_raster_params`
- **THEN** `picture_grayscale` 值從 `SLAPrinterConfig::picture_grayscale` 取得（而非 `SLAMaterialConfig`）

---

### Requirement: SLAPrint 持有並暴露 m_raster_params

`SLAPrint` SHALL 持有 `std::optional<SLARasterParams> m_raster_params`，並提供 const accessor `raster_params()`。

#### Scenario: rasterize 步驟完成後 raster_params 有值
- **WHEN** `slapsRasterize` 步驟成功完成
- **THEN** `raster_params().has_value()` 為 `true`

#### Scenario: invalidate_step 後 raster_params 被清除
- **WHEN** `invalidate_step(slapsRasterize)` 被呼叫
- **THEN** `raster_params().has_value()` 為 `false`，防止讀取到過期參數

#### Scenario: 切片前 raster_params 為空
- **WHEN** 尚未執行任何切片或 `rasterize()` 步驟
- **THEN** `raster_params().has_value()` 為 `false`
