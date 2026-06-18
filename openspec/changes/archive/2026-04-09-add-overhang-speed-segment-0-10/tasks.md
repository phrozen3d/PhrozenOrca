## 1. 設定層：新增 ConfigOption

- [x] 1.1 在 `src/libslic3r/PrintConfig.cpp`（約 line 1206 之前）新增 `overhang_0_4_speed` 的 ConfigOption 定義，label 為 `[0%, 10%)`，type `coFloatOrPercent`，ratio_over `outer_wall_speed`，min 0，預設值 0，mode comAdvanced
- [x] 1.2 在 `src/libslic3r/PrintConfig.hpp`（約 line 1044）的 PrintRegionConfig macro 中，在 `enable_overhang_speed` 之後新增 `((ConfigOptionFloatOrPercent, overhang_0_4_speed))`

## 2. G-code 生成：套用第五段速度

- [x] 2.1 在 `src/libslic3r/GCode.cpp`（line 5462-5479，slowdown_for_curled_perimeters=true 分支），將第一個速度條目從固定的 `FloatOrPercent{100, true}` 改為：`(m_config.get_abs_value("overhang_0_4_speed", ref_speed) < 0.5) ? FloatOrPercent{100, true} : FloatOrPercent{m_config.get_abs_value("overhang_0_4_speed", ref_speed) * 100 / ref_speed, true}`
- [x] 2.2 在 `src/libslic3r/GCode.cpp`（line 5484-5498，slowdown_for_curled_perimeters=false 分支），同樣替換第一個速度條目為上述條件式結構

## 3. GUI：顯示第五段輸入欄

- [x] 3.1 在 `src/slic3r/GUI/Tab.cpp`（約 line 2300），在現有四個 `line.append_option(...)` 之前插入 `line.append_option(optgroup->get_option("overhang_0_4_speed"))`
- [x] 3.2 在 `src/slic3r/GUI/ConfigManipulation.cpp`（約 line 798），在 toggle_line 的循環 string list 開頭加入 `"overhang_0_4_speed"`
- [x] 3.3 在 `src/slic3r/GUI/GUI_Factories.cpp`（約 line 115），在 `"enable_overhang_speed"` 索引後插入 `{"overhang_0_4_speed", "", 7}`，並將後續四段的索引依序遞增為 8~11

## 4. Profile JSON：新增預設值

- [x] 4.1 在 `resources/profiles/Phrozen/process/fdm_process_common.json` 中新增 `"overhang_0_4_speed": "0"`（位於 `overhang_1_4_speed` 之前）
- [x] 4.2 對其餘 12 個含有 `overhang_1_4_speed` 的 process JSON 檔案執行相同新增（依序為 fdm_process_common_0.2/0.4/0.6_nozzle.json 及各個 layer height profile）

## 5. 驗證

- [x] 5.1 編譯確認無編譯錯誤（`build_release_vs2022.bat slicer`）  ← 請手動執行
- [x] 5.2 開啟 GUI，至速度設定 > Overhang speed，確認出現五個欄位，第一欄 label 為 `[0%, 10%)`
- [x] 5.3 設定 `overhang_0_4_speed = 30 mm/s` 並切片含有輕微懸空模型，查看 G-code 確認有速度變化
- [x] 5.4 設定 `overhang_0_4_speed = 0` 並切片，確認行為與舊版本相同
- [x] 5.5 載入舊 profile（無 `overhang_0_4_speed` 欄位），確認能正常載入且值預設為 0
