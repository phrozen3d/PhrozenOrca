# PhrozenOrca SLA Merge — 結果文件索引

**Branch**: `phrozen-resin-dev`
**最後更新**: 2026-03-04

---

## 快速導覽

| Phase | 目標 | Steps |
|-------|------|:-----:|
| [Phase 1](#phase-1-核心參數與演算法) | SLA 核心參數、Archive、UI 穩定性 | 1.1–1.6 + 修復 |
| [Phase 2](#phase-2-切片流程與-ui) | SLA 切片流程打通、Preview、Layer Slider | 2.1–2.6 |
| [Phase 3](#phase-3-支撐樹系統) | BranchingTree、SupportIslands、SupportPointGenerator | 3.1–3.5 |
| [Phase 4](#phase-4-sla-gizmo-強化) | GLGizmoSlaBase、Hollow、Drill、AnycubicSLA | 4.1–4.8 |
| [Phase B](#phase-b-sla-支撐演算法強化) | Voronoi 支撐演算法、密度修正、漸縮柱結構 | A–C |

---

## Phase 1: 核心參數與演算法

**目標**：將 PrusaSlicer SLA 核心參數、Archive 格式、Rasterization、SL1 Profile 整合至 PhrozenOrca，並修復所有 SLA 切換崩潰。

**總覽**：[Phase1_Merge_Result.md](Phase1_Merge_Result.md)

### Steps

| Step | 說明 | Commit | 結果文件 |
|------|------|--------|---------|
| 1.1 | **PrintConfig SLA 參數同步** — 從 PrusaSlicer 同步 ~54 個 SLA 參數（SLASupportTreeType、TowerSpeeds、TiltSpeeds Enum；branchingsupport_* 21 個物件參數；material_ow_* 等材料參數；tilt/tower 設定）| `755a195220` | [Step1_1](Phase1_Result/Step1_1_PrintConfig_Result.md) |
| 1.2 | **ZCorrection 模組移植** — 從 PrusaSlicer 直接複製 ZCorrection.hpp/cpp，整合至 `apply_printer_corrections()` pipeline；預設值 0 = 停用，對現有行為無影響 | `c7322bd82d` | [Step1_2](Phase1_Result/Step1_2_ZCorrection_Result.md) |
| 1.3 | **Archive 格式擴充** — 以 Factory + Registry 模式重構 SLAArchive 系統（`SLAArchiveWriter`/`SLAArchiveReader`/`SLAArchiveFormatRegistry`）；向後相容別名 `using SLAArchive = SLAArchiveWriter`；解決三輪 PCH 記憶體溢出 | `a22a37941b` | [Step1_3](Phase1_Result/Step1_3_Archive_Result.md) |
| 1.4 | **Rasterization 評估** — 比對確認光柵化演算法 100% 相同，無需修改（差異僅在型別名稱 Vec2i32 與 mutex 型別，均為 PhrozenOrca 刻意選擇） | 無修改 | [Step1_4](Phase1_Result/Step1_4_Rasterization_Result.md) |
| 1.5 | **SLA Tab 統一** — 移除 PhrozenLCDTab 特製分支，統一走 PrusaSlicer 的 TabSLA 架構 | `da2439d929` | — |
| 1.6 | **SL1 Profile 整合** — 新增 21 個 Original Prusa SL1 Printer/Process/Material profile JSON 及資源檔案；修改 PresetBundle 路由 SLA profile 至 sla_prints/sla_materials | `8bdbf4253d` | — |

### 修復（Phase 1 期間）

| Fix | 說明 | Commit | 結果文件 |
|-----|------|--------|---------|
| 啟動 Crash | `BackgroundSlicingProcess.cpp` 的 mainframe/plater 8 處 nullptr guard | `5b4b300aeb` | — |
| SLA Profile 載入 Crash | `PresetBundle.cpp` + material JSON 修正 | `94f0a13763` | — |
| SLA Printer 選擇 Crash | `MainFrame.cpp` + `Plater.cpp` 加 ptFFF guard，恢復 SLA preset combo 初始化 | `6e24ace708` | — |
| Profile 切換穩定性 | `Tab.cpp` nozzle_diameter/toggle_options FDM guard；`Plater.cpp` Sidebar SLA 面板初始化；`PresetBundle.cpp` filament_presets guard | `28e6472d4c` | [詳情](Phase1_Result/Phase1_SLA_UI_Stability_and_Gizmo_Result.md) |
| BrimEars Gizmo Crash | `GLGizmoBrimEars.cpp` — `on_init()` + `on_is_activable()` 加 ptFFF guard，SLA 下跳過 nozzle_diameter 存取 | `267fdb5afe` | [詳情](Phase1_Result/Phase1_SLA_UI_Stability_and_Gizmo_Result.md) |

---

## Phase 2: 切片流程與 UI

**目標**：打通 SLA 完整切片流程，修復模型消失問題，實作 SLA Layer Slider，補全 preset options。

### Steps

| Step | 說明 | Commit(s) | 結果文件 |
|------|------|-----------|---------|
| 2.1 | **啟用 SLA Gizmos** — 取消 GLGizmosManager.hpp 中 `SlaSupports` 和 `Hollow` 的 EType enum 注解，恢復 icon 和事件路由 | `26b60ff87b` | [Step2_1](Phase2_Slice_Flow_Merge_Result/Step2_1_EnableSLAGizmos_Result.md) |
| 2.2 | **3D Viewer SLA 驗證** — 10 項自動化預檢（gizmo activability、icon、SLAPrintObject API 等）全部通過；驗證過程發現並修復 5 個 crash | `26b60ff87b` | [Step2_2](Phase2_Slice_Flow_Merge_Result/Step2_2_3DViewerVerify_Result.md) |
| 2.3 | **SLAPrint 初始化** — 修復 `m_sla_print` 永遠為 nullptr 問題；PhrozenOrca 使用 PartPlate 系統，需在 `get_current_plate()` 切換時同步設定；新增 SLA gizmo crash guard | `26b5cb43bd` `d009f469eb` `70e4832db0` | [Step2_3](Phase2_Slice_Flow_Merge_Result/Step2_3_SLAPrint_Init_Result.md) |
| 2.4 | **SLA Preview 修復** — 修復切片後模型消失問題（根因：`load_print()` 缺少 ptSLA 分支，呼叫 `get_current_fff_print()` 造成場景清空）| `ea30256771` | [Step2_4](Phase2_Slice_Flow_Merge_Result/Step2_4_SLAPreview_Result.md) |
| 2.5 | **SLA Layer Slider** — 修改 `IMSlider` 加入 visibility flag + callback 機制，實作 SLA 逐層瀏覽（雙指頭範圍選取）；修復 6 個相關 bug（FDM shell 殘影、re-entrancy guard、FDM slider 誤顯示等）| `c91efc8759` `39f692bef5` `455e083fad` `0c634c3460` `a586644854` | [Step2_5](Phase2_Slice_Flow_Merge_Result/Step2_5_SLALayerSlider_Result.md) |
| 2.6 | **Preset Options 補全** — 補全 `s_Preset_sla_print_options` 白名單（原始版本缺少 PrusaSlicer 命名的支撐參數，導致 GLGizmoSlaSupports 存取 nullptr）；修復 thumbnail Access Violation crash | `b408318764` `88472c4fb4` | [Step2_6](Phase2_Slice_Flow_Merge_Result/Step2_6_SLAPrintOptions_Fix_Result.md) |

---

## Phase 3: 支撐樹系統

**目標**：移植 PrusaSlicer 支撐樹策略模式、BranchingTree 有機分枝演算法、SupportIslands Voronoi 子系統，升級 SupportPointGenerator。

### Steps

| Step | 說明 | Commit(s) | 結果文件 |
|------|------|-----------|---------|
| 3.1 | **SupportTreeStrategies 策略模式** — 新增 `sla::SupportTreeType` enum（Default/Branching/Organic），在 `SLAPrint.cpp::make_support_cfg()` 做 Config → 內部 enum 的一處轉換 | `d892bea222` | [Step3_1](Phase3_SupportTree_Merge_Result/Step3_1_SupportTreeStrategies_Result.md) |
| 3.2 | **BranchingTree 函式庫移植** — 從 PrusaSlicer 複製 `BranchingTree/` 4 個檔案；因 PhrozenOrca 無 `SupportTreeUtils.hpp`，將 `find_merge_pt` 改為 inline 實作；補充 Boost.Geometry traits | `3fc4a6d38f` `88dc84b72b` | [Step3_2](Phase3_SupportTree_Merge_Result/Step3_2_BranchingTree_Result.md) |
| 3.3 | **BranchingTreeSLA 有機分枝支撐** — 移植 `BranchingTreeSLA.cpp`（~280 行），啟用 `SupportTreeType::Branching` 策略，使 SLA 支撐可選擇有機分枝模式 | `5a626fc732` | [Step3_3](Phase3_SupportTree_Merge_Result/Step3_3_BranchingTreeSLA_Result.md) |
| 3.4 | **SupportIslands 子系統移植** — 從 PrusaSlicer 複製整個 `SupportIslands/` 目錄（32 個檔案，含 Voronoi Medial Axis、PolylineUtils、SampleConfig 等）為 Phase A 奠基 | `34db797a5a` | [Step3_4](Phase3_SupportTree_Merge_Result/Step3_4_SupportIslands_Result.md) |
| 3.5 | **SupportPointGenerator 整合** — 在 island case 以 `uniform_support_island()` 取代 `uniformly_cover()` Poisson disk sampling，改善支撐點均勻分布；但整體架構仍保留舊 class（Phase A 完成後徹底升級）| `c91ef53a78` | [Step3_5](Phase3_SupportTree_Merge_Result/Step3_5_SupportPointGenerator_Result.md) |

---

## Phase 4: SLA Gizmo 強化

**目標**：對齊 PrusaSlicer 的 SLA Gizmo 繼承架構（GLGizmoSlaBase），修復渲染顏色、Drill 功能、新增 AnycubicSLA 格式。

### Steps

| Step | 說明 | Commit(s) | 結果文件 |
|------|------|-----------|---------|
| 4.1 | **GLGizmoSlaBase 中間基類** — 新增 `GLGizmoSlaBase.hpp/cpp`，統一 SLA Gizmo 的 `render_volumes()` / `update_volumes()` / `data_changed()` 管道，為 Steps 4.2/4.3 的前置條件 | `11cc5c9486` | [Step4_1](Phase4_SLA_Gizmo_Enhance_Merge_Result/Step4_1_GLGizmoSlaBase_Result.md) |
| 4.2 | **GLGizmoSlaSupports 重構** — 從繼承 `GLGizmoBase` 改為繼承 `GLGizmoSlaBase`；使用 `data_changed()` 取代 `set_sla_support_data()`；整合 Volume Raycasters；移除裸 `m_cylinder` GLModel | `3346cd982c` | [Step4_2](Phase4_SLA_Gizmo_Enhance_Merge_Result/Step4_2_GLGizmoSlaSupports_Result.md) |
| 4.3 | **GLGizmoHollow 重構** — 從繼承 `GLGizmoBase` 改為繼承 `GLGizmoSlaBase`；升級 `GLModel` → `PickingModel`（加入 raycaster 支援）；整合 `set_hide_full_scene(true)` | `36773a4e75` | [Step4_3](Phase4_SLA_Gizmo_Enhance_Merge_Result/Step4_3_GLGizmoHollow_Result.md) |
| 4.4 | **InstancesHider set_hide_full_scene()** — 在 `InstancesHider` 新增 `set_hide_full_scene()` 方法，讓 SLA Gizmo 的 `data_changed()` 能隱藏全部模型，由 Gizmo 自行渲染（Steps 4.2/4.3 的硬依賴）| `d451e70d6d` | [Step4_4](Phase4_SLA_Gizmo_Enhance_Merge_Result/Step4_4_InstancesHider_Result.md) |
| 4.5 | **AnycubicSLA 格式支援** — 移植 PrusaSlicer `AnycubicSLA.hpp/cpp`，新增 Anycubic Photon Mono 系列 `.pwmo`/`.pwmx`/`.pwms` 輸出格式；Register 至 SLAArchiveFormatRegistry | `583b420250` | [Step4_5](Phase4_SLA_Gizmo_Enhance_Merge_Result/Step4_5_AnycubicSLA_Result.md) |
| 4.6 | **Support Gizmo 顏色修正** — 修正 `update_volumes()` 顏色邏輯，使進入 SLA Support Points Gizmo 時模型維持原始顏色（根因：BBS 注解了 `selected` 分支）；補充 no-step constructor 與支撐點統計 UI | `09971c0250` | [Step4_6](Phase4_SLA_Gizmo_Enhance_Merge_Result/Step4_6_SlaSupports_ColorFix_Result.md) |
| 4.7 | **Hollow Gizmo 最小步驟修正** — 修正 GLGizmoHollow constructor 使用錯誤的 `slaposSliceSupports`（應為 `slaposDrillHoles`），使進入 Hollow Gizmo 後模型立即正常顯示 | `c06d867448` | [Step4_7](Phase4_SLA_Gizmo_Enhance_Merge_Result/Step4_7_HollowDrill_MinStepFix_Result.md) |
| 4.8 | **drill_holes() 功能恢復** — 恢復 BambuStudio 以 `/* ... */` 整段封鎖的 `drill_holes()` 函式本體（no-op stub 導致「Inconsistent slice index」錯誤）；整合 Hollow + Drill 完整掏空流程 | `c06d867448` `2cb4893a41` | [Step4_8](Phase4_SLA_Gizmo_Enhance_Merge_Result/Step4_8_DrillHoles_Restore_Result.md) |

---

## Phase Extra: SLA 支撐演算法強化

**目標**：升級 SupportPointGenerator 至 PrusaSlicer 2024 新架構，修正支撐密度設定失效問題，對齊 Pillar 漸縮柱資料結構。

> **注意**：此 Phase 的 commit 在 Phase 4 完成後繼續進行，命名為 Phase B（以免與 Phase 4 的步驟編號混淆）。

### Steps

| Step | 說明 | Commit | 結果文件 |
|------|------|--------|---------|
| A | **Voronoi + NearPoints KD-tree 架構升級** — 完整重寫 `SupportPointGenerator.cpp`（681→~1618 行）；以自由函數 `prepare_generator_data()` + `generate_support_points()` + `move_on_mesh_surface()` 取代舊 class；加入 `NearPoints` KD-tree 實作；修復 KD-tree `build()` 呼叫缺失 bug（根本原因：支撐點聚集問題）| `8e010ec3de` | [StepA](SLA_Support_Algorithm_Enhance/StepA_SupportAlgorithmEnhance_Result.md) |
| B | **支撐密度修正** — 修復 `SLAPrintSteps.cpp` 兩個 bug：(1) `island_configuration` 未套用 `sla::SampleConfigFactory::apply_density()`，導致 density 設定對採樣間距無效；(2) `allowed_move` 使用 head 直徑（0.4mm）而非層高（0.025~0.1mm），造成誤投影 | `3a685fee5b` | [StepB](SLA_Support_Algorithm_Enhance/StepB_SupportDensityFix_Result.md) |
| C | **Tapered Pillar 結構對齊** — `Pillar` struct 欄位 `r` → `r_start + r_end`；加入 convenience constructor 保留所有現有呼叫點；`SupportTreeMesher` 改用 `halfcone()` 取代 `cylinder()`；更新 SupportTreeBuildsteps 15 處引用（PhrozenOrca widening_factor=0.0，視覺結果不變）| `44b41aedba` | [StepC](SLA_Support_Algorithm_Enhance/StepC_TaperedPillar_Result.md) |

---

## Commit 快速參照

| Commit | Phase / Step |
|--------|-------------|
| `755a195220` | Phase 1 — Step 1.1 PrintConfig 參數同步 |
| `c7322bd82d` | Phase 1 — Step 1.2 ZCorrection 移植 |
| `a22a37941b` | Phase 1 — Step 1.3 Archive 格式擴充 |
| `da2439d929` | Phase 1 — Step 1.5 SLA Tab 統一 |
| `8bdbf4253d` | Phase 1 — Step 1.6 SL1 Profile 整合 |
| `94f0a13763` | Phase 1 — Fix SLA profile 載入 crash |
| `6e24ace708` | Phase 1 — Fix SLA printer 選擇 crash |
| `28e6472d4c` | Phase 1 — Fix Profile 切換穩定性 |
| `267fdb5afe` | Phase 1 — Fix BrimEars Gizmo crash |
| `26b60ff87b` | Phase 2 — Step 2.1/2.2 啟用 SLA Gizmos |
| `26b5cb43bd` | Phase 2 — Step 2.3 SLAPrint 初始化（Connect）|
| `d009f469eb` | Phase 2 — Step 2.3 SLAPrint 初始化（Crash Guard）|
| `70e4832db0` | Phase 2 — Step 2.3 SLAPrint 初始化（Gizmo API）|
| `ea30256771` | Phase 2 — Step 2.4 SLA Preview 修復 |
| `c91efc8759` | Phase 2 — Step 2.5 Layer Slider 主體實作 |
| `a586644854` | Phase 2 — Step 2.5 Layer Slider 雙指頭升級（最終）|
| `b408318764` | Phase 2 — Step 2.6 Preset Options 補全 |
| `88472c4fb4` | Phase 2 — Step 2.6 Thumbnail AV Crash 修復 |
| `d892bea222` | Phase 3 — Step 3.1 SupportTreeStrategies |
| `3fc4a6d38f` | Phase 3 — Step 3.2 BranchingTree 移植 |
| `5a626fc732` | Phase 3 — Step 3.3 BranchingTreeSLA |
| `34db797a5a` | Phase 3 — Step 3.4 SupportIslands 移植 |
| `c91ef53a78` | Phase 3 — Step 3.5 SupportPointGenerator 整合 |
| `11cc5c9486` | Phase 4 — Step 4.1 GLGizmoSlaBase |
| `d451e70d6d` | Phase 4 — Step 4.4 InstancesHider |
| `3346cd982c` | Phase 4 — Step 4.2 GLGizmoSlaSupports 重構 |
| `36773a4e75` | Phase 4 — Step 4.3 GLGizmoHollow 重構 |
| `583b420250` | Phase 4 — Step 4.5 AnycubicSLA |
| `09971c0250` | Phase 4 — Step 4.6 Support Gizmo 顏色修正 |
| `c06d867448` | Phase 4 — Step 4.7 + 4.8 Hollow/Drill 修復 |
| `2cb4893a41` | Phase 4 — Step 4.8 Hollow 參數標籤補充 |
| `8e010ec3de` | Phase B — Step A Voronoi + KD-tree 架構升級 |
| `3a685fee5b` | Phase B — Step B 密度修正 |
| `44b41aedba` | Phase B — Step C Tapered Pillar 結構對齊 |
