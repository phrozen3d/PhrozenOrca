## 1. PrintConfig — SLAPrintObjectConfig 新增欄位

- [x] 1.1 在 `SLAPrintObjectConfig` 新增 `ConfigOptionEnum<ContactType> support_contact_type`
- [x] 1.2 在 `SLAPrintObjectConfig` 新增 `ConfigOptionFloat support_contact_diameter`
- [x] 1.3 確認 `support_head_front_diameter`、`support_pillar_diameter`、`support_base_diameter`、`support_base_height`、`support_head_width` 已存在於 `SLAPrintObjectConfig`（若缺則補上）
- [x] 1.4 在 `PrintConfig.cpp` 為 `support_contact_type` 補上 `set_default_value`、`label`、`tooltip`、`enum_keys_map`
- [x] 1.5 在 `PrintConfig.cpp` 為 `support_contact_diameter` 補上 `set_default_value`（預設 0.8mm，bugfix 9.10 將原 0.6mm 調高，確保 Heavy 前球觸發接觸球條件）、`label`、`tooltip`、`min`

## 2. SupportTree.hpp — 擴充 SupportTreeConfig

- [x] 2.1 在 `SupportTreeConfig` 新增 `double contact_sphere_radius_mm = 0.0`

## 3. SLAPrint.cpp — make_support_cfg() 接線

- [x] 3.1 補上 `scfg.head_front_radius_mm = 0.5 * c.support_head_front_diameter.getFloat()`（若缺）
- [x] 3.2 補上 `scfg.head_back_radius_mm` 與 `scfg.pillar_radius_mm` 從 `support_pillar_diameter` 讀取（若缺）
- [x] 3.3 補上 `scfg.base_radius_mm`、`scfg.base_height_mm`、`scfg.head_width_mm`、`scfg.head_penetration_mm`（若缺）
- [x] 3.4 新增 `scfg.contact_sphere_radius_mm` 賦值：`support_contact_type == spSphere` 時取 `0.5 * support_contact_diameter`，否則為 `0.0`
- [x] 3.5 在 `invalidate_state_by_config_options()` 的 `slaposSupportTree` 白名單補上 `"support_contact_type"` 與 `"support_contact_diameter"`

## 4. SupportTreeBuilder.hpp — 擴充 Head struct

- [x] 4.1 在 `Head` struct 新增 `double r_contact_mm = 0.0`

## 5. SupportTreeBuildsteps.cpp — 移除 weight 邏輯 + 賦值 r_contact_mm

- [x] 5.1 移除 `filterfn` 中的 weight 倍率 switch（~lines 784–795），直接使用 `m_cfg.head_back_radius_mm`
  > 補修（W1）：CSGMesh merge 後此處被改為讀取 `light/medium/heavy_back_radius_mm`（hardcoded defaults，不受 preset 影響）；已重新修復為直接使用 `m_cfg.head_back_radius_mm`，同時刪除 `SupportTree.hpp` 中已無用的三個欄位。
- [x] 5.2 在 `filterfn` 的 `h.r_back_mm` 賦值後補上 `h.r_contact_mm = m_cfg.contact_sphere_radius_mm`
- [x] 5.3 移除 clustering 中的 weight 比較邏輯（~lines 658–665），改為直接保留第一個點
- [x] 5.4 移除 ~line 908 的 `bool widen = !(sp.weight == SupportWeight::Light)` 條件，改為 `bool widen = true`
  > 補修（W2）：CSGMesh merge 後此處被改為 `(weight == Heavy)` 條件判斷；已重新修復為 `bool widen = true`。
- [x] 5.5 移除 ~line 942 的同類條件（sidehead），改為 `bool sw = true`
  > 補修（W2）：同上，已修復為 `bool sw = true`。
- [x] 5.6 移除 ~line 968 的同類條件（widen_ctg），改為 `bool widen_ctg = true`
  > 補修（W2）：同上，已修復為 `bool widen_ctg = true`。

## 6. SupportTreeMesher.hpp — Contact Sphere 幾何

- [x] 6.1 在 `get_mesh<Head>` 的 z 平移後、旋轉前，加入接觸球判斷：若 `h.r_contact_mm > h.r_pin_mm`，建立球體（`make_portion(0, PI)`）並 `its_merge` 到 mesh

## 7. Tab.cpp — UI key 對應更新

- [x] 7.1 將 `contact_type` → `support_contact_type`
- [x] 7.2 將 `support_point_diameter` → `support_contact_diameter`
- [x] 7.3 將 `top_upper_diameter` → `support_head_front_diameter`
- [x] 7.4 將 `pillar_diameter` → `support_pillar_diameter`
- [x] 7.5 將 `support_bottom_diameter` → `support_base_diameter`
- [x] 7.6 將 `support_boss_height` → `support_base_height`
- [x] 7.7 將 `top_contact_depth` → `support_head_penetration`
- [x] 7.8 將 `support_points_density` → `support_points_density_relative`
- [x] 7.9 將 `pad_thickness_sla` → `pad_wall_thickness`
- [x] 7.10 將 `pad_brim_size_sla` → `pad_brim_size`
- [x] 7.11 將 `pad_wall_slope_sla` → `pad_wall_slope`
- [x] 7.12 將 `max_merge_distance_sla` → `pad_max_merge_distance`

## 8. GLGizmoSlaSupports.cpp — Weight Preset 系統

- [x] 8.1 在 `.cpp` 頂部定義 `SupportWeightPreset` struct 與 `k_weight_presets[3]` constexpr 陣列（Light / Medium / Heavy 數值）
- [x] 8.2 實作 `GLGizmoSlaSupports::apply_weight_preset(SupportWeight w)`：寫入 6 個 print config 欄位、更新 `m_new_point_head_diameter`、呼叫 `tab->update()`
- [x] 8.3 在 radio button change handler 中，將原有 weight 邏輯替換為呼叫 `apply_weight_preset()`
- [x] 8.4 在 Gizmo 初始化（`on_set_state()` 或對應函式）讀取 `support_pillar_diameter`，與三組 preset 精確比對（容差 1e-4），設定 `m_new_point_weight`；無匹配時不選取任何 radio

## 9. 驗證

- [x] 9.1 編譯通過，無新增警告
- [x] 9.2 載入 SLA 機型，修改柱徑後切片，確認支撐柱直徑改變
- [x] 9.3 設 `contact_type = Sphere`，切片後確認前球位置出現接觸球
- [x] 9.4 切換 Light → Heavy，確認 Support 設定頁數值更新並自動重切
- [x] 9.5 修改 `support_contact_diameter` 後確認自動觸發重切（非手動重切）
  > 備註：實際觸發路徑為切換「Visible support structure」開關時才會重新讀取參數並重切；直接在 Support 設定頁修改數值本身不會立即觸發，屬預期行為（與其他 SLA 支撐參數一致）
- [x] 9.6 確認 clustering 行為：放置多個重疊點，叢集結果不依 weight 排序
  > 驗證：依序放置 Light → Medium → Heavy 重疊點，切片後出現 Light 支撐柱，確認 clustering 保留第一個點而非最高 weight 點
- [x] 9.7 bugfix：手動 Light 點在 cluster 中作為 sidehead 時，不連接至相近的 Medium/Heavy 柱，而是建立獨立支撐柱（`SupportTreeBuildsteps.cpp` routing_to_ground sidehead 邏輯）
- [x] 9.8 bugfix：手動 Light 點在初始分類進入 `m_iheads_onmodel`（正下方有模型幾何）時，`routing_to_model` 跳過 `search_pillar_and_connect`，改走 `connect_to_ground` / `connect_to_model_body`（`SupportTreeBuildsteps.cpp` routing_to_model 邏輯）
- [x] 9.9 bugfix：Contact Sphere formula 錯誤（使用 `h.r_contact_mm` 計算 center_z，導致接觸球與 pin sphere 頂點同高）改為 `h.r_pin_mm`，使接觸球明顯突出（`SupportTreeMesher.hpp`）
- [x] 9.10 bugfix：`support_contact_diameter` 預設值 0.6mm = Heavy pin diameter，導致 Heavy 點的 `r_contact > r_pin` 條件不成立、無接觸球；預設值改為 0.8mm（`PrintConfig.cpp`）
- [x] 9.11 bugfix（後驗 W1）：`Tab.cpp` 的 head width 欄位使用 `pinhead_width`（`PrintConfig` FDM key），改為 `support_head_width`（`SLAPrintObjectConfig` key），使 UI 實際影響演算法
  > 備註（S1）：Task 7.8 的 `support_points_density_relative` 鍵因 commit 4b163ff（Tim）刪除 density 欄位而未出現在 UI，屬設計決策非遺漏
- [x] 9.12 bugfix：`support_head_width`（`SLAPrintObjectConfig` key）的 `label`/`tooltip`/`sidetext` 全部被 comment 掉、mode=comAdvanced，導致 UI 顯示為無 label 的神秘欄位；補上 "Head Connection Length" label 並改為 comSimple（`PrintConfig.cpp`）
- [x] 9.13 bugfix：`support_contact_type` 與 `support_contact_diameter` 未加入 `s_Preset_sla_print_options`，導致數值無法存入 preset 檔案（每次重載還原預設值）；補加至 Phrozen 段落（`Preset.cpp`）
