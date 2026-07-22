# Tasks — sla-support-contact-spacing-4mm

> 注意:編譯建置與測試由使用者手動執行。以下每階段的「驗證」以手動檢查與明確測試案例確認為主。

## 1. 基準數值前置確認(不改碼,先鎖定不變式)

- [x] 1.1 手算並記錄參考頭徑 0.4mm 下「未縮放」的 `thin_max_distance`:`L1_ref = π·(0.2)²·2.9 + 1.3 ≈ 1.664mm`;`thin_ref = L1_ref·3.9·0.8 ≈ 5.193mm`
- [x] 1.2 由此反推目標係數 `k = 4.0 / thin_ref ≈ 0.7702`,並記錄期望連動值:`inner ≈ 5.0mm`、`outline ≈ 3.75mm`
- [x] 1.3 **驗證**:確認 1.1/1.2 的手算與 design.md「Decisions D2」及 spec 場景一致(4.0 / 5.0 / 3.75)

## 2. 引入具名常數與係數推導(SampleConfigFactory.cpp)

- [x] 2.1 在 [SampleConfigFactory.cpp](../../../src/libslic3r/SLA/SupportIslands/SampleConfigFactory.cpp) 檔案範圍加入具名常數:`kRefHeadDiameter = 0.4`、`kTargetThinSpacing = 4.0`(mm)
- [x] 2.2 加入一個編譯期 helper,以與 `create()` 相同的公式算出「未縮放的 `thin_max_distance`@ref」(即 `(π·(kRefHeadDiameter/2)²·2.9 + 1.3)·3.9·0.8`),避免填入 5.193 魔數
- [x] 2.3 由 helper 定義 `k = kTargetThinSpacing / thin_ref`(供 `create()` 使用)
- [x] 2.4 **驗證**:於程式碼審視確認 `k` 完全由常數推導、無硬寫 0.770/5.193(對映 spec「Baseline…」之場景「Calibration factor is derived, not hard-coded」)

## 3. 於 L1 注入係數(單一根,整鏈連動)

- [x] 3.1 修改 [SampleConfigFactory.cpp](../../../src/libslic3r/SLA/SupportIslands/SampleConfigFactory.cpp) 之 L1,將 `k` 乘進 `max_length_for_one_support_point` 的 `scale_()` 引數內部(對 double 相乘後再 `scale_`,只取整一次):`scale_((head_area*2.9 + 1.3) * contact_spacing_scale())`
- [x] 3.2 確認**不**觸碰 `head_radius` 與 `minimal_distance_from_outline`:兩者由頭徑獨立算出,不經 L1,天然不縮
- [x] 3.3 確認其餘間距欄位(L2、thin/inner/outline、thin_max_width、thick_min_width、min_part_length、maximal_distance_from_outline、max_align_distance)皆為 L1/L2 倍數,無需個別修改即自動連動
- [x] 3.4 更新 `create()` 內相關註解(標示 4mm 校準基準與 k 來源),保留原 Prusa 物理方程式出處說明;移除 `contact_spacing_scale()` 的 `[[maybe_unused]]`
- [x] 3.5 **驗證(手動建置後)**:於除錯或臨時列印確認 `create(0.4)` 之 `thin≈4.0`、`inner≈5.0`、`outline≈3.75`mm(對映 spec 場景「Thin spacing…」「Inner and outline…」)— 使用者已確認正確

## 4. 新增回歸測試(實置於 tests/libslic3r/,見第 6 節備註)

- [x] 4.1 在 SLA 測試新增 case:`create(0.4).thin_max_distance` 以近似比較(`WithinAbs`,µm 容差;依 tests/CLAUDE.md 禁用 `Approx`)≈ 4.0mm
- [x] 4.2 新增 case:`create(0.4)` 的 `thick_inner_max_distance ≈ 5.0`、`thick_outline_max_distance ≈ 3.75`mm,並驗證比例保持
- [x] 4.3 新增 case:`head_radius`、`minimal_distance_from_outline` 與純頭徑公式(d/2)一致、不受 k 影響(對映 spec「Physical head fields…」)
- [x] 4.4 新增 case:頭徑連動——`create(0.8).thin > 4.0 > create(0.2).thin`(對映 spec「Head-diameter coupling…」)
- [x] 4.5 新增 case:對 `d ∈ {0.2, 0.4, 0.8}` 呼叫 `create(d)` 後,再次 `verify()` 回傳 true(不觸發 clamp;對映 spec「verify() consistency…」)
- [x] 4.6 新增 case:對校準後的 `create(0.4)` 套用 `apply_density(2.0)`,確認 `thin_max_distance ≈ 2.0mm`(對映 spec「Density semantics unchanged」)
- [x] 4.7 **驗證**:確認每條 spec 場景都有對映的測試案例(7 需求 → 測試覆蓋核對表)

## 5. 全系統一致性檢查(單一真相源)

- [x] 5.1 確認生產切片路徑 [SLAPrintSteps.cpp:895](../../../src/libslic3r/SLAPrintSteps.cpp#L895) 未改動即自動吃到新基準(`create()` → `apply_density()`)
- [x] 5.2 確認 LCD overhang 手動偵測工具 [GLGizmoLcdOverhangDetection.cpp:1515](../../../src/slic3r/GUI/Gizmos/GLGizmoLcdOverhangDetection.cpp#L1515) 直接呼叫 `create()`,自動一致
- [x] 5.3 確認 `create_default_island_configuration()`([SupportPointGenerator.cpp:1074](../../../src/libslic3r/SLA/SupportPointGenerator.cpp#L1074))回傳 `create()`,同源、無殘留舊基準
- [x] 5.4 **驗證**:全庫搜尋 `create()`/`SampleConfig` 消費點,確認無繞過路徑——`apply_density` 只縮放 `create()` 輸出、`UniformSupportIsland` 僅讀取傳入 config;結構預設 5.0mm 僅為 fallback,無生產路徑使用未經 `create()` 的 config

> **建置基礎設施備註**:`tests/CMakeLists.txt:34` 已將 `add_subdirectory(sla_print)` 註解停用
> (既有 `sla_print_tests.cpp` 對 `RasterBase` API 漂移編譯失敗)。為讓本測試能實際建置且不
> 連累 VS 建置,測試檔改置於**已啟用**的 `tests/libslic3r/`,併入 `libslic3r_tests` 聚合
> target(僅依賴 libslic3r 的 `SampleConfigFactory`,無 sla_print 相依)。

## 6. 端到端手動驗證(使用者手動執行建置後)

- [x] 6.1 建置測試 target(`libslic3r_tests`,RelWithDebInfo)編譯通過;slicer target 未受影響
- [x] 6.2 執行單元測試 `libslic3r_tests.exe "[ContactSpacing]"` → **6 test cases / 17 assertions 全數通過**
- [x] 6.3 GUI 手動:密度 100%、頭徑 0.4mm 自動生成支撐,接觸點確認變密(對映 ~5.2mm → ~4mm)
- [x] 6.4 邊際手動:切換頭徑後確認可正常生成、不崩、無 `verify` assert
- [x] 6.5 density 疊加手動:200% 密度結果正確(間距約再減半)
- [x] 6.6 一致性手動:LCD overhang 手動偵測工具生成點確認通過(同基準)
- [x] 6.7 UI 不變手動:確認密度滑桿仍為 50%–200%、參數模型無變化(U1 純後端)

## 7. 收尾

- [x] 7.1 `openspec validate "sla-support-contact-spacing-4mm"` 通過(valid)
- [x] 7.2 檢視 diff 僅涵蓋 `SampleConfigFactory.cpp` + `tests/libslic3r/CMakeLists.txt` + 新增測試檔,未動到 `tests/CMakeLists.txt` 或 `tests/sla_print/`,無意外改動
- [x] 7.3 確認所有 spec 場景均已對應滿足(7 需求:6 條單元測試 + 1 條靜態一致性檢查,另有 GUI 手動實機驗證),準備進入 `/opsx:archive`
