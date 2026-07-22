## Why

SLA 自動支撐的接觸點間距目前由 `SampleConfigFactory::create(head_diameter)` 依 Prusa 的頭面積物理方程式推導。預設 0.4mm 支撐頭、密度 100% 下,細長區相鄰支撐點的最大間距 `thin_max_distance ≈ 5.19mm`,比市場基準(CHITUBOX 概念的 ~4mm 基準間距)偏疏,使用者實務上傾向更密的接觸點。此變更將「100% 密度、預設 0.4mm 頭」下的細長區接觸點間距重新校準到約 4mm,使開箱預設更貼近業界慣用手感。

## What Changes

- 在 `SampleConfigFactory::create()` 回傳前、`verify()` 之前,對所有「間距/幾何長度」欄位統一乘上一個具名縮放係數 `k`,使 `thin_max_distance(0.4mm 頭)` 由 ~5.19mm 校準為 ~4.0mm。
  - `k = kTargetThinSpacing(4.0mm) / thin_max_distance(kRefHeadDiameter 0.4mm)` ≈ 0.770,以具名常數反推,不硬寫魔數。
- 縮放採「整條幾何鏈自相似縮放」:`L1`、`L2`、`thin_max_distance`、`thick_inner_max_distance`、`thick_outline_max_distance`、`thin_max_width`、`thick_min_width`、`maximal_distance_from_outline`、`min_part_length`、`max_align_distance` 全乘 `k`,維持既有比例。
- 物理頭部欄位 `head_radius`、`minimal_distance_from_outline` **不**乘 `k`,保持真實頭尺寸。
- 保留頭徑連動:4mm 僅錨定在 0.4mm 參考頭徑,其他頭徑等比例縮放(頭越大間距越大,符合物理)。
- 新增 SLA 回歸測試,鎖定 `create(0.4).thin_max_distance ≈ 4mm` 及關鍵 `verify()` 不等式在 0.2/0.4/0.8mm 頭下仍成立。
- **非** BREAKING:不改參數模型、不改 UI、不改 `apply_density()` 語意;密度 % 定義維持「100% = 新基準」,舊專案檔照常載入。

## Capabilities

### New Capabilities
- `sla-support-contact-spacing`: 定義 SLA 自動支撐接觸點間距的基準校準規則——在參考頭徑(0.4mm)、密度 100% 下細長區間距的目標值、整條幾何鏈的自相似縮放行為、頭部物理欄位不縮放的約束,以及跨全頭徑域的 `verify()` 自洽保證。

### Modified Capabilities
<!-- 無:此變更為新增基準校準行為,不改動任何既有 capability 的 spec 級需求。 -->

## Impact

- **主要修改**:`src/libslic3r/SLA/SupportIslands/SampleConfigFactory.cpp`(`create()` 內注入係數)。
- **連帶自動生效(無需額外改動,因注入在唯一源頭 `create()`)**:
  - 生產切片路徑 `src/libslic3r/SLAPrintSteps.cpp:895`(`create()` → `apply_density()`,新基準被 density 正常疊加:100%≈4mm、200%≈2mm)。
  - LCD overhang 手動偵測工具 `src/slic3r/GUI/Gizmos/GLGizmoLcdOverhangDetection.cpp:1515`(直接呼叫 `create()`,自動吃到新基準,維持全系統一致)。
  - `create_default_island_configuration()` 預設成員:同源,自動一致。
- **測試**:新增回歸 case(既有 tests 無此覆蓋,需補回歸網)。實際置於 `tests/libslic3r/sla_contact_spacing_tests.cpp` 並併入 `libslic3r_tests` target,**非** `tests/sla_print/`——因既有 `tests/sla_print` 目錄已被停用(`tests/CMakeLists.txt:34` 註解 `add_subdirectory(sla_print)`,起因於 `sla_print_tests.cpp` 對 `RasterBase` API 漂移編譯失敗);本測試僅依賴 `SampleConfigFactory`(libslic3r),置於已啟用的 libslic3r 測試最自然且不連累建置。
- **不受影響**:UI(密度滑桿仍 50–200%、tooltip、參數模型)、`apply_density()`、既有專案檔/預設相容性。
