## Why

在 Generate Support 模式下產生支撐點後，使用者**只能點選到 pinhead 頂端的小球，外露的錐體完全點不到**。

根因單一且明確：Points preview 的命中判定只由 sphere raycaster 承擔，而同時註冊的 cone raycaster 從未被接上。

```
                    畫面上畫的                     實際可點選的
        ● pin 球  r = r_pin ≈ 0.2mm              ● 只有這顆球
       ╱ ╲                                      （r = max(r_pin, r_contact)）
      ╱   ╲   robe 錐體      ┐
     ╱     ╲                 │ width_mm ≈ 2mm        （空）
    ╱       ╲                │ 完全點不到
   ●─────────●  back 球      ┘                        （空）
     r_back ≈ 0.5mm
```

`GLGizmoSlaSupports` 沿襲上游設計，為每點註冊兩個單位幾何 raycaster（`GLGizmoSlaSupports.cpp:535-544`）：

```cpp
m_sphere.mesh_raycaster = ... its_make_sphere(1.0, PI/12);    // 管球
m_cone.mesh_raycaster   = ... its_make_cone(1.0, 1.0, PI/12); // 管錐體
```

兩者皆於 `:2362-2364` 註冊，但 cone 那一半從未完成接線：

- **transform 從未設定**：註冊時給 `Transform3d::Identity()`，`update_point_raycasters_for_picking_transform()` 只對 `.first`（sphere）呼叫 `set_transform()`（`:2415`），`.second`（cone）始終停留在單位錐擺在原點的狀態。
- **永遠停用**：`:2412` 寫死 `m_point_raycasters[i].second->set_active(false)`，沒有任何路徑會打開它。

因此有效命中體積僅為半徑 `max(head.r_pin_mm, head.r_contact_mm)`（`:2409`）的球。以預設 `support_head_front_diameter = 0.4 mm` 計，半徑僅 0.2 mm，而視覺體長約 `width_mm = 2 mm`——可點面積不到視覺體的十分之一。

此缺陷非由 `fix-sla-support-points-preview-mode-gate` 引入（cone raycaster 在其之前即未啟用），但該 change 建立的「錨點跟隨 instance scale、幾何維持 mm」拆解是本次修復的基礎。其歸檔後產生的 spec requirement **Points-preview picking is consistent with the rendered cone** 目前僅描述 sphere，且其場景宣稱「no hover gap or false hit-test region between the cone and the picking sphere」——與實際行為不符，本 change 需一併修正該 requirement。

## What Changes

### 為 cone raycaster 補上 transform 並啟用

- `update_point_raycasters_for_picking_transform()` 為 `.second`（cone）計算並設定 transform，使單位錐（`its_make_cone(1.0, 1.0)`）對齊視覺 pinhead 的 robe 段：以支撐點錨點為頂、沿表面法線反向延伸至 back 球，半徑由 `r_pin` 過渡至 `r_back`。
- 依現行 clipping 規則同步 `.second` 的 active 狀態（與 `.first` 一致），取代目前寫死的 `set_active(false)`。
- Sphere raycaster 維持現狀，繼續涵蓋 pin 球與 contact 球；兩者聯集構成完整命中體積。

### 擺放旋轉引入 picking 路徑

Cone 具方向性，sphere 為各向同性——這是現行 picking 路徑不需要旋轉的原因（`:2405-2407` 註解）。本 change 需將擺放旋轉 `Quaternion::FromTwoVectors(-UnitZ, scaled_normal)` 納入 cone 的 picking transform，並沿用 render 路徑既有的 `normal_xform` inverse-transpose 修正，確保非均勻 scale 下 picking 與視覺一致。

### 修正 spec 中與實際不符的 picking requirement

現行 requirement 只規範 sphere，並宣稱不存在 hover gap。改為規範「命中體積由 sphere 與 cone 的聯集構成，且涵蓋整個可見 pinhead」。

### Non-goals

- 不改變 Points preview 的視覺外觀、顏色規則或 clipping 行為。
- 不改變 `sla-support-points-preview` 既有的錨點／尺寸拆解約定；本 change 建立在其之上。
- 不以「放大 sphere 半徑至整體包絡」替代——該作法會讓錐體周圍空白區域誤觸，以誤觸換漏觸。
- 不改動 Structure 模式 picking、支撐生成 backend、Hollow / Drill 路徑。
- 不處理 Points preview 每幀渲染成本（由 `perf-sla-support-points-preview-render` 承接）。

## Capabilities

### New Capabilities

<!-- 無。本 change 修正既有 capability 的行為，不引入新的 capability。 -->

### Modified Capabilities

- `sla-support-points-preview`：修改 requirement **Points-preview picking is consistent with the rendered cone**。原內容僅規範 sphere raycaster 的中心與半徑，並宣稱視覺 cone 與 picking 之間不存在 hover gap；實際上錐體段完全無法命中。改為規範命中體積由 sphere 與 cone raycaster 的聯集構成，涵蓋整個可見 pinhead，並於各 instance scale 下與視覺一致。

## Impact

- **Primary**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp`
  - `update_point_raycasters_for_picking_transform()`（`:2378`）— 為 `.second` 計算 cone transform、納入擺放旋轉、依 clipping 同步 active 狀態
  - `render_points()`（`:651-653`）— 既有的 raycaster active 管理維持一致
- 不需修改 `register_point_raycasters_for_picking()` 的註冊結構（cone raycaster 已在註冊中）
- 不影響 `GLGizmoSlaBase`、`SceneRaycaster`、支撐生成 backend
- 無 public API、檔案格式或 profile 變更

### 與 `perf-sla-support-points-preview-render` 的相依關係

**建議在該 change 之後實施。** 其 design D2 會建立並驗證擺放矩陣 `M_ns · Translation(S · sp.pos) · Rotation(q)`，本 change 的 cone picking transform 所需的正是同一個矩陣（再疊上單位錐至實際尺寸的縮放）。順序實施可直接複用已驗證等價性的推導；反序則需在 picking 路徑先行寫一套旋轉，待 render 路徑改動後再同步驗證兩邊一致。

該 change 已於其 proposal 的 Non-goals 與 design D7 明令**不得移除** cone picking raycaster，即為保全本 change 的修復基礎。
