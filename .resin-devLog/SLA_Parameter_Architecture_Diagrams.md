# SLA 參數架構圖

**建立日期**: 2026-03-22
**說明**: 對照圖，顯示現況與建議架構差異

---

## 圖 1：目前 PhrozenOrca 參數放置現況與切片流程

```mermaid
flowchart TD
    subgraph UI["🖥️ UI 設定層"]
        TabFDM["TabPrint (FDM)"]
        TabSLAPrint["TabSLAPrint (SLA 列印)"]
        TabSLAMat["TabSLAMaterial (SLA 材料)"]
        TabSLAPrinter["TabSLAPrinter (SLA 印表機)"]
    end

    subgraph Configs["📦 Config Struct 層"]
        subgraph PrintConfig["PrintConfig ⚠️ FDM 主 Config"]
            FDM_params["FDM 參數\n(layer_height, infill, supports...)"]
            SLA_WRONG["❌ 誤置 SLA 參數 (51個)\n─────────────────\n曝光: exposure_time, bottom_exposure_time\n升降: lifting_speed, retract_speed...\n支撐: pillar_diameter, max_bridge_length_sla\n補償: shrinkage_compensation*\n補償: tolerance_compensation*\n顯示: anti_aliasing, light_pwm\n重複: area_fill (×3), anti_aliasing (×2)"]
        end

        subgraph SLAPrintObjCfg["SLAPrintObjectConfig ✅"]
            SLA_obj["列印設定參數\n─────────────────\ngenerate_support ✅\nlayer_height, faded_layers\nsupport_head_*, support_pillar_*\npad_enable, pad_wall_slope\nsupport_tree_type..."]
        end

        subgraph SLAMatCfg["SLAMaterialConfig ✅"]
            SLA_mat["材料參數\n─────────────────\nexposure_time ⚠️(重複)\ninitial_exposure_time\nmaterial_ow_*\ntilt_*"]
        end

        subgraph SLAPrinterCfg["SLAPrinterConfig ✅"]
            SLA_printer["印表機硬體參數\n─────────────────\ndisplay_width/height\ndisplay_pixels_x/y\nanti_aliasing ⚠️(重複)\nprintable_area"]
        end
    end

    subgraph SlicingPipeline["⚙️ 切片引擎"]
        FDMPrint["FDMPrint\n讀 FullPrintConfig"]
        SLAPrintCls["SLAPrint\n讀 SLAPrintObjectConfig\n(m_config.X 成員存取)"]
        SLAPrintSteps["SLAPrintSteps\n.slice_model()\n.support_points()\n.rasterize()"]
        AnycubicSLA["AnycubicSLA.cpp\n(Phrozen 格式匯出)\n直接讀 PrintConfig 的誤置參數"]
        PhrozenPRZ["PhrozenPRZ.cpp\n(PRZ 格式匯出)"]
    end

    subgraph Output["📄 輸出"]
        FDM_out["FDM Gcode"]
        SLA_out["SLA 切片結果\n(.sl1 / .pwmx)"]
        PRZ_out[".prz 檔案"]
    end

    TabFDM -->|設定值| PrintConfig
    TabSLAPrint -->|設定值| SLAPrintObjCfg
    TabSLAMat -->|設定值| SLAMatCfg
    TabSLAPrinter -->|設定值| SLAPrinterCfg

    PrintConfig --> FDMPrint
    PrintConfig -.->|誤置參數 切片引擎讀不到| SLAPrintCls
    SLAPrintObjCfg --> SLAPrintCls
    SLAMatCfg --> SLAPrintCls
    SLAPrinterCfg --> SLAPrintCls

    FDMPrint --> FDM_out
    SLAPrintCls --> SLAPrintSteps
    SLAPrintSteps --> SLA_out

    PrintConfig -->|誤置參數被直接讀取 bypass 正規流程| AnycubicSLA
    SLAMatCfg --> AnycubicSLA
    SLAPrintCls --> PhrozenPRZ
    AnycubicSLA --> SLA_out
    PhrozenPRZ --> PRZ_out

    style SLA_WRONG fill:#ffcccc,stroke:#cc0000,color:#000
    style PrintConfig fill:#fff3cd,stroke:#ff9900
    style AnycubicSLA fill:#ffe0b2,stroke:#e65100
    style SLA_WRONG color:#cc0000
```

### 問題摘要

| 問題 | 影響 |
|---|---|
| 51 個 SLA 參數放在 `PrintConfig`（FDM struct）| SLAPrint.cpp 無法透過 `m_config.X` 讀取 |
| AnycubicSLA.cpp 直接讀 PrintConfig 的誤置參數 | Bypass 正規切片流程，只有匯出時才生效 |
| `anti_aliasing` 重複定義（PrintConfig + SLAPrinterConfig）| 不確定哪個版本被使用 |
| `area_fill` 三重重複 | 同上 |
| `exposure_time` 兩個版本 | SLAMaterialConfig 版本正確，PrintConfig 版本是無用重影 |

---

## 圖 2：建議架構（SLA 參數移到正確位置後）

```mermaid
flowchart TD
    subgraph UI["🖥️ UI 設定層"]
        TabFDM["TabPrint (FDM)"]
        TabSLAPrint["TabSLAPrint (SLA 列印)"]
        TabSLAMat["TabSLAMaterial (SLA 材料)"]
        TabSLAPrinter["TabSLAPrinter (SLA 印表機)"]
    end

    subgraph Configs["📦 Config Struct 層（整理後）"]
        subgraph PrintConfig["PrintConfig ✅ FDM 專用"]
            FDM_params["FDM 參數只剩 FDM 內容\n(layer_height, infill, supports...)"]
        end

        subgraph SLAPrintObjCfg["SLAPrintObjectConfig ✅"]
            SLA_obj["列印設定參數\n─────────────────\ngenerate_support\nlayer_height, faded_layers ← bottom_layer_count 整合\nsupport_head_*, support_pillar_diameter ← pillar_diameter 整合\npad_enable, pad_wall_slope ← pad_wall_slope_sla 整合\nsupport_tree_type\nsupport_max_bridge_length ← max_bridge_length_sla 整合\nsupport_max_pillar_link_distance ← max_pillar_linking_distance 整合\nsupport_points_density_relative ← support_points_density 整合\ntransition_layer_count 🆕\ntransition_type 🆕\nwaiting_mode_during_printing 🆕\nshrinkage_compensation* 🆕\ntolerance_compensation* 🆕"]
        end

        subgraph SLAMatCfg["SLAMaterialConfig ✅"]
            SLA_mat["材料參數\n─────────────────\nexposure_time (僅此一份)\ninitial_exposure_time\nrest_time_before/after_lift 🆕\nrest_time_after_retract 🆕\nbottom_lift_distance / lifting_distance 🆕\nbottom_lift_speed / lifting_speed 🆕\nretract_distance / retract_speed 🆕\n二段式升降參數 (second_*) 🆕\nbottom_light_pwm / light_pwm 🆕\nmaterial_ow_*\ntilt_*"]
        end

        subgraph SLAPrinterCfg["SLAPrinterConfig ✅"]
            SLA_printer["印表機硬體參數\n─────────────────\ndisplay_width/height\ndisplay_pixels_x/y\nanti_aliasing (僅此一份)\narea_fill (僅此一份)\nprintable_area\npicture_grayscale 🆕\ngray_scale_level 🆕\nimage_blur_enable / image_blur_pixel 🆕\nanti_aliasing_level 🆕\nmax_print_height 🆕(新增)"]
        end
    end

    subgraph SlicingPipeline["⚙️ 切片引擎（正規化後）"]
        FDMPrint["FDMPrint\n讀 FullPrintConfig"]
        SLAPrintCls["SLAPrint\n讀 SLAPrintObjectConfig\n(m_config.X 成員存取)"]
        SLAPrintSteps["SLAPrintSteps\n.slice_model() ← 收縮補償在此讀取\n.support_points() ← 支撐密度/距離在此讀取\n.rasterize() ← 抗鋸齒/灰階在此讀取"]
        AnycubicSLA["AnycubicSLA.cpp\n(Phrozen 格式匯出)\n改讀正確的 SLAMaterialConfig"]
        PhrozenPRZ["PhrozenPRZ.cpp\n(PRZ 格式匯出)"]
    end

    subgraph Output["📄 輸出"]
        FDM_out["FDM Gcode"]
        SLA_out["SLA 切片結果\n(.sl1 / .pwmx)"]
        PRZ_out[".prz 檔案"]
    end

    TabFDM -->|設定值| PrintConfig
    TabSLAPrint -->|設定值| SLAPrintObjCfg
    TabSLAMat -->|設定值| SLAMatCfg
    TabSLAPrinter -->|設定值| SLAPrinterCfg

    PrintConfig --> FDMPrint
    SLAPrintObjCfg --> SLAPrintCls
    SLAMatCfg --> SLAPrintCls
    SLAPrinterCfg --> SLAPrintCls

    FDMPrint --> FDM_out
    SLAPrintCls --> SLAPrintSteps
    SLAPrintSteps --> SLA_out

    SLAMatCfg --> AnycubicSLA
    SLAPrintCls --> PhrozenPRZ
    AnycubicSLA --> SLA_out
    PhrozenPRZ --> PRZ_out

    style PrintConfig fill:#d4edda,stroke:#28a745
    style SLAPrintObjCfg fill:#d4edda,stroke:#28a745
    style SLAMatCfg fill:#d4edda,stroke:#28a745
    style SLAPrinterCfg fill:#d4edda,stroke:#28a745
    style AnycubicSLA fill:#d4edda,stroke:#28a745
```

### 整理後的改善點

| 改善項目 | 說明 |
|---|---|
| ✅ PrintConfig 只剩 FDM 參數 | SLA 參數全部遷移至對應的 SLA struct |
| ✅ SLAPrint.cpp 可透過 `m_config.X` 直接存取所有參數 | 不再需要繞路 |
| ✅ AnycubicSLA.cpp 改讀 SLAMaterialConfig | 與正規切片流程共用同一份資料來源 |
| ✅ 重複定義全部消除 | anti_aliasing、area_fill、exposure_time 各只有一份 |
| 🆕 新增 `max_print_height` | 印表機高度限制 |
| 🆕 收縮補償/公差補償連接 rasterize 步驟 | 切片時真正套用 |
| 🆕 二段式升降 / rest_time 移入 SLAMaterialConfig | 匯出時從正確位置讀取 |

---

## 遷移工作量速估

| 工作項目 | 影響範圍 | 估計複雜度 |
|---|---|---|
| 移除 PrintConfig 中的重複定義 | PrintConfig.hpp + .cpp | 低 |
| 51 個參數搬移至正確 struct | PrintConfig.hpp | 中 |
| AnycubicSLA.cpp 改讀正確 struct | Format/AnycubicSLA.cpp | 中 |
| SLAPrintSteps.cpp 新增讀取補償/灰階參數 | SLAPrintSteps.cpp | 高 |
| Profile JSON key 同步更新 | resources/profiles/ | 低 |
| Preset.cpp dirty check list 更新 | Preset.cpp | 低 |

---

## 圖 3：FDM 與 SLA 完整參數分佈對照

```mermaid
flowchart LR
    subgraph FDM["🖨️ FDM 體系"]
        direction TB

        subgraph FDM_UI["UI Tab"]
            TabPrinter["TabPrinter\n印表機設定"]
            TabFilament["TabFilament\n材料設定"]
            TabPrint["TabPrint\n列印設定"]
        end

        subgraph FDM_CFG["Config Struct"]
            subgraph PC["PrintConfig\n印表機硬體"]
                PC1["printable_area\nbed_custom_texture\nextruders_count\ngcode_flavor\nuse_firmware_retraction\nprinter_notes\n..."]
            end
            subgraph FC["PrintObjectConfig + PrintRegionConfig\n材料 / 區域設定"]
                FC1["filament_colour\nfilament_diameter\nnozzle_temperature\ncooling_fan_speed\nfilament_retraction_length\n..."]
            end
            subgraph OC["PrintObjectConfig + PrintConfig\n列印工藝設定"]
                OC1["layer_height\nperimeters\ninfill_density\nsupport_material\nbrim_width\nseam_position\n..."]
            end
        end

        subgraph FDM_ENGINE["切片引擎"]
            FDMPrint2["Print\n讀 FullPrintConfig\n(PrintObjectConfig + PrintRegionConfig + PrintConfig)"]
            FDMOut["GCode 輸出"]
        end

        TabPrinter -->|設定值| PC
        TabFilament -->|設定值| FC
        TabPrint -->|設定值| OC

        PC --> FDMPrint2
        FC --> FDMPrint2
        OC --> FDMPrint2
        FDMPrint2 --> FDMOut
    end

    subgraph SLA["🔦 SLA 體系"]
        direction TB

        subgraph SLA_UI["UI Tab"]
            TabPrinter2["TabPrinter\n印表機設定\n(FDM 共用，內部分支)"]
            TabSLAMat["TabSLAMaterial\n材料設定"]
            TabSLAPrint2["TabSLAPrint\n列印設定"]
        end

        subgraph SLA_CFG["Config Struct"]
            subgraph SPC["SLAPrinterConfig\n印表機硬體"]
                SPC1["display_width / display_height\ndisplay_pixels_x / display_pixels_y\nprintable_area\nanti_aliasing\narea_fill\n印表機型號 / 解析度..."]
            end
            subgraph SMC["SLAMaterialConfig\n材料設定"]
                SMC1["exposure_time\ninitial_exposure_time\nmaterial_colour\nmaterial_ow_*\ntilt_*\n升降速度 / 距離 (Phrozen)\nrest_time_* (Phrozen)\nlight_pwm (Phrozen)\n..."]
            end
            subgraph SOC["SLAPrintObjectConfig\n列印設定"]
                SOC1["layer_height\nfaded_layers\ngenerate_support\nsupport_tree_type\nsupport_head_*\nsupport_pillar_*\npad_enable / pad_*\nslice_closing_radius\n..."]
            end
            subgraph BAD["PrintConfig (誤置區)\n目前現況"]
                BAD1["exposure_time (重複)\nbottom_layer_count\nlifting_speed / retract_speed\nshrinkage_compensation*\ntolerance_compensation*\nanti_aliasing (重複)\narea_fill (重複)\n...共 51 個"]
            end
        end

        subgraph SLA_ENGINE["切片引擎"]
            SLAPrint2["SLAPrint\n讀 SLAPrintObjectConfig\n+ SLAMaterialConfig\n+ SLAPrinterConfig"]
            SLAPrintSteps2["SLAPrintSteps\nslice_model\nsupport_points\nrasterize"]
            AnycubicSLA2["AnycubicSLA.cpp\nPhrozen 格式匯出\n(目前直接讀 PrintConfig 誤置區)"]
            SLAOut["SLA 切片結果\n(.sl1 / .pwmx / .prz)"]
        end

        TabPrinter2 -->|設定值| SPC
        TabSLAMat -->|設定值| SMC
        TabSLAPrint2 -->|設定值| SOC

        SPC --> SLAPrint2
        SMC --> SLAPrint2
        SOC --> SLAPrint2
        SLAPrint2 --> SLAPrintSteps2
        SLAPrintSteps2 --> SLAOut
        SMC --> AnycubicSLA2
        BAD1 -.->|誤置參數\n直接讀取| AnycubicSLA2
        AnycubicSLA2 --> SLAOut
    end

    style BAD fill:#ffcccc,stroke:#cc0000
    style BAD1 fill:#ffcccc,stroke:#cc0000,color:#000
    style FDM fill:#e8f4fd,stroke:#2196F3
    style SLA fill:#fff8e1,stroke:#FF9800
```

### 關鍵差異對照

| | FDM | SLA |
|---|---|---|
| 印表機 Tab | `TabPrinter` | `TabPrinter`（共用，內部 branch）|
| 材料 Tab | `TabFilament` | `TabSLAMaterial` |
| 列印設定 Tab | `TabPrint` | `TabSLAPrint` |
| 印表機 Config Struct | `PrintConfig` | `SLAPrinterConfig` |
| 材料 Config Struct | `PrintObjectConfig` / `PrintRegionConfig` | `SLAMaterialConfig` |
| 列印設定 Config Struct | `PrintObjectConfig` / `PrintConfig` | `SLAPrintObjectConfig` |
| 切片引擎讀取 | `FullPrintConfig`（三者合一）| `SLAFullPrintConfig`（三者合一）|
| 現況問題 | 無 | 51 個 SLA 參數誤置在 FDM 的 `PrintConfig` |
