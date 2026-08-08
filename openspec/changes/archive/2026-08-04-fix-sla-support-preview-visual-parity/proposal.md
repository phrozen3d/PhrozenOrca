## Why

手動放置的支撐點（`manual_add`）與自動生成的支撐點，在 Points preview 上外觀不對等，且這個不對等在使用者切換檢視模式時會造成困惑。

`render_points()` 目前依 `support_point.type` 決定畫哪一種幾何、用哪一種 shader：

```cpp
const bool manual_preview = support_point.type == sla::SupportPointType::manual_add;
use_shader(manual_preview ? flat_shader : gouraud_shader);
```

- **形狀**：auto 點用 `pinhead()`（pin 球 + back 球，兩者以相切圓平滑過渡，形成有弧度的端頭）；manual 點用 `pinhead_preview()`（只有 pin 球，後段是直筒接一個平面圓盤收尾，程式碼註解明寫「no back-sphere bulge」）。
- **光影**：auto 點用 `gouraud_light` shader（有法線光照）；manual 點用 `flat` shader，其 fragment shader 只有一行 `gl_FragColor = uniform_color;`——完全不讀法線，旋轉視角顏色不會有任何變化。

且顏色分流（`manual_add` → CYAN、`island` → ORANGE、`slope` → LIGHT_GRAY）目前整段被包在 `m_editing_mode` 底下：

```cpp
else if (m_editing_mode && support_point.type == sla::SupportPointType::manual_add) render_color = CYAN;
else if (m_editing_mode && support_point.type == sla::SupportPointType::island) render_color = ORANGE;
else if (m_editing_mode) render_color = LIGHT_GRAY;
else render_color = {0.5f, 0.5f, 0.5f, 1.f};   // 非編輯模式：type 資訊被完全丟棄
```

非編輯模式（Points 檢視 / 自動模式）下，所有點一律顯示同一種灰色，使用者無法從顏色分辨哪些是自動生成、哪些是手動放置。

### 為什麼現在的樣子不是刻意設計

`pinhead_preview()` 的簡化外觀源自 PrusaSlicer 原始設計：PrusaSlicer 的 auto 模式只顯示一顆球（表示「位置」），manual 模式顯示球+錐（表示「位置＋法向量方向」）——兩者原本用途不同，形狀本該不同，且 PrusaSlicer 原版 manual 模式的球+錐本來就是完整 polygon、有正確光影。

但 PhrozenOrca-resin 這個 fork 新增了「支撐點可逐點分別設定參數」的功能（PrusaSlicer 原版全部點共用同一組參數）。manual 點因此需要跟 auto 點一樣顯示明確的尺寸細節才有意義，「manual 只需要簡化圖示表示方向」這個用途分工的前提已經不存在。光影缺失則是移植到 PhrozenOrca 時的疏漏——因為不影響測試使用而被暫時忽略，但兩者都應該要有光影才對。

### 效能不是理由

`perf-sla-support-points-preview-render`（已於 2026-07-29 archive）已把 pinhead 幾何依 `(r_pin, r_back, width, penetration, r_contact, preview_flag)` 快取——每種尺寸組合的網格只建一次，之後全部共用同一份 GPU buffer draw。統一畫法後 `preview_flag` 這個 key 維度可以拿掉，auto 點與 manual 點只要尺寸參數相同甚至能共用同一筆快取，不會有效能落差，反而可能減少快取筆數。

本 change 於驗收 `fix-sla-support-preview-stored-geometry-in-auto-mode` 期間，透過 explore 討論發現並確認要做，屬 `fix-sla-support-point-issues` 分支的一部分，但與該分支既有五個 change 彼此獨立，不影響現有的順序依賴關係。

## What Changes

- **形狀統一**：manual 點改用 `pinhead()`（與 auto 點相同），移除 `pinhead_preview()` 簡化版的呼叫。
- **光影統一**：manual 點改用 `gouraud_shader`（與 auto 點相同），移除 `flat_shader` 呼叫。
- **移除死碼**：`get_mesh_preview()`（`SupportTreeMesher.hpp`）查證確認全專案僅有 `render_points()` 這一個呼叫端，移除呼叫後成為無呼叫者的死碼，一併移除。`pinhead_preview()` **不移除**——實作時發現它被 `head_mesh_body()` 的 `preview=true` 分支內部呼叫，兩者是同一條呼叫鏈而非各自獨立的死碼；`head_mesh_body()` 同時服務切片管線的 `get_mesh()`，收斂其雙分支簽章是更大範圍的重構，不在本 change 範圍內（詳見 design.md D5）。
- **顏色跨模式生效**：`manual_add` / `island` / `slope` 三種顏色分流移出 `m_editing_mode` 限定，非編輯模式（Points 檢視）也依 `support_point.type` 顯示對應顏色。孤島鎖定提示（`BLUEISH`，牽涉 `m_lock_unique_islands`）**維持**只在編輯模式生效——鎖定操作本身只在編輯模式下有意義，不隨本 change 變動。

### Non-goals

- 不改變支撐點的幾何尺寸解析規則（per-point vs preset 的判定邏輯）——那是 `fix-sla-support-preview-geometry-source-semantics` 的範圍，本 change 只改變畫出來的網格形狀與著色方式，不改變決定尺寸大小的規則。
- 不改變 hover / selected 的既有顏色規則（CYAN hover、REDISH selected），這兩者維持原樣。
- 不改變 picking 命中判定——本 change 不影響 `update_point_raycasters_for_picking_transform()`。
- 不處理「選定支撐點編輯參數時其他 auto 點外觀被連動」的問題（Top 欄位讀值機制的副作用），那是另一個獨立問題。
- 不處理 auto 點的 `type` 是否該在編輯後轉換為 `manual_add`——那併入 `fix-sla-support-preview-geometry-source-semantics` 的決策範圍。

## Capabilities

### New Capabilities

<!-- 無。本 change 為既有 capability 增補 requirement。 -->

### Modified Capabilities

- `sla-support-points-preview`：新增 requirement，規範 Points preview 錐體的**幾何形狀與光影**不得依 `support_point.type` 而異（manual 點與 auto 點使用相同的 mesh 函式與 shader），以及**顏色分流**（manual/island/slope）不得依 `m_editing_mode` 而異，但孤島鎖定提示維持編輯模式限定。該 capability 現有的 requirement（anchor 位置、尺寸 mm 不變、picking 一致性、幾何來源解析）皆不變更。

## Impact

- **Primary**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — `render_points()` 的 mesh 函式選擇、shader 選擇、color 判斷邏輯
- **Reference（不預期修改）**：`src/libslic3r/SLA/SupportTreeMesher.cpp`/`.hpp` — `pinhead()` / `pinhead_preview()` 函式定義本身不變，只改變呼叫端
- **與既有快取機制的互動**：`m_head_model_cache` 的 `HeadGeomKey`（`perf-sla-support-points-preview-render` 建立）含 `preview_flag` 維度，本 change 後該維度恆定或可移除，需確認快取邏輯無需連帶修改
- 不影響切片輸出、檔案格式或 profile
- 無 public API 變更
