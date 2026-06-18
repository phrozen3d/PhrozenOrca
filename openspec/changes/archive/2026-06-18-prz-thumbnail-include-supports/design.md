## Context

PRZ 匯出縮圖由 `Plater::export_prz()`（`src/slic3r/GUI/Plater.cpp:12458`）以 OpenGL 離屏渲染 Prepare 視圖（view3D canvas）的 `m_volumes` 產生，再轉 RGB565 寫入 header。三輪質詢確立的關鍵事實：

- **過濾條件**（`GLCanvas3D.cpp:5969`）：
  ```cpp
  if (!vol->is_modifier && !vol->is_wipe_tower &&
      (!thumbnail_params.parts_only || vol->composite_id.volume_id >= 0))
  ```
  SLA 支撐／pad 為 aux volume，`composite_id.volume_id < 0`（`is_sla_support()` 定義為 `== -slaposSupportTree`），在 `parts_only=true` 下被排除。`export_prz()` 目前傳入的 `tparams` 第二個 bool 為 `true`（`Plater.cpp:12536`）。

- **支撐入場條件**（`GLCanvas3D.cpp:2410-2435`）：`reload_scene()` 內就地計算
  ```cpp
  const bool sla_gizmo_active = m_gizmos.get_current_type() == GLGizmosManager::EType::SlaSupports;
  const bool load_sla_support_pad_in_scene = m_canvas_type != ECanvasType::CanvasView3D || !sla_gizmo_active;
  ```
  當 SLA 支撐 gizmo 啟用時，支撐／pad 網格**不會載入** `m_volumes`。其唯一理由（見原始碼註解 2402-2404）是避免「螢幕上」與 gizmo 自繪點預覽雙重渲染——此理由對離屏縮圖 framebuffer **不成立**。

- **同步性**：`reload_scene()`（`GLCanvas3D.cpp:2312`）為同步函式，回傳前已 inline 重建 `m_volumes`（`refresh_immediately` 只控制重建後是否立即重畫螢幕，不影響重建本身）。`generate_thumbnail()` → `render_thumbnail_framebuffer()` 亦為同步（FBO + `glReadPixels`）。但「切換 gizmo」只做 `set_as_dirty()` + 非同步 paint event，**不會**同步觸發 reload，故不可依賴它。

## Goals / Non-Goals

**Goals:**
- PRZ 縮圖納入實際會列印的 SLA 支撐與 pad。
- 改動範圍嚴格限縮於 SLA／PRZ 匯出路徑，零波及 3MF／FDM／板件縮圖。
- 渲染前以**同步**方式保證支撐已載入場景，杜絕 race condition。
- 匯出結束後不破壞使用者當前的 Prepare 視圖 / gizmo / 選取狀態。

**Non-Goals:**
- 不修改全域縮圖過濾邏輯（`GLCanvas3D.cpp:5969`）。
- 不修改 PRZ 格式、RGB565 轉換、`preview_image_path` fallback。
- 不從 `SLAPrintObject` 自建 GLVolume 另闢離屏渲染管線（先前質詢已否決的高成本「後者」路線）。
- 不提供使用者自訂縮圖來源的 UI。

## Decisions

### D1：放寬過濾僅限 PRZ 匯出路徑

在 `Plater::export_prz()` 的本地 `tparams`（`Plater.cpp:12536`）將 `parts_only` 由 `true` 改為 `false`，**不動** `GLCanvas3D.cpp:5969`。

理由：`parts_only` 是逐次呼叫參數而非全域開關；PRZ 匯出按鈕只在 SLA 模式存在（`MainFrame.cpp:2280` 的 `if (bIsFDMMode) {…} else {…PRZ…}`），且 `export_prz()` 內部即依賴 `sla_print()`，本質 SLA-only。`!is_wipe_tower` 子句不受 `parts_only` 影響，即使理論上 FDM 誤入也不會帶入擦料塔。

連帶效果：`parts_only=false` 亦會帶入 pad（`slaposPad`），為預期行為。

### D2：保證支撐入場 —— (a) vs (b) 對比

問題核心：D1 放寬過濾後，若支撐 gizmo 啟用，支撐 volume 根本不在 `m_volumes`，放寬旗標也無效。需在渲染前同步確保支撐載入。兩條候選路：

#### 路線 (a)：暫時中性化 gizmo 狀態

```
export_prz() 內：
  auto prev = canvas->get_gizmos_manager().get_current_type();
  bool need_restore = (prev == GLGizmosManager::EType::SlaSupports);
  if (need_restore) {
      切換 gizmo 離開 SlaSupports（如 reset / open Undefined）;
      view3D->reload_scene(/*refresh_immediately=*/false);   // 同步，載入支撐
  }
  generate_thumbnail(...);
  if (need_restore) {
      還原 gizmo 為 prev;
      view3D->reload_scene(false);                            // 同步，回復原視覺
  }
```

- **修改位置**：集中於 `Plater.cpp::export_prz()`；需呼叫 `GLGizmosManager` 既有的切換/開關 API。
- **優點**：完全不動共用函式簽章；只用既有公開介面。
- **缺點**：
  - 動到 live UI 狀態（gizmo 切換可能連帶清空/變動 selection、相機焦點、imgui 面板），即使事後還原也較難保證 100% 無殘留副作用。
  - 需要兩次 `reload_scene()`（載入 + 還原），成本與閃爍風險較高。
  - gizmo 切換的副作用分散於多處（`GLGizmosManager` 內 `set_as_dirty` / post event），時序推理較脆弱。

#### 路線 (b)：為 reload_scene 增加 force_load_sla_support override

```cpp
// GLCanvas3D.hpp / .cpp
void reload_scene(bool refresh_immediately,
                  bool force_full_scene_refresh = false,
                  bool force_load_sla_support = false);   // 新增，預設 false

// GLCanvas3D.cpp:2412 改為：
const bool load_sla_support_pad_in_scene =
    force_load_sla_support ||
    m_canvas_type != ECanvasType::CanvasView3D || !sla_gizmo_active;
```

```
export_prz() 內：
  view3D->get_canvas3d()->reload_scene(/*refresh_immediately=*/false,
                                       /*force_full_scene_refresh=*/false,
                                       /*force_load_sla_support=*/true);  // 同步載入支撐
  generate_thumbnail(...);
  // gizmo 狀態從未被改動，無需還原；可選擇性再 reload 一次回復螢幕視覺
```

- **修改位置**：`GLCanvas3D.hpp`（簽章）+ `GLCanvas3D.cpp:2412`（一個 `||`）+ `Plater.cpp::export_prz()`（一次呼叫）+ `GUI_Preview.cpp` 的 `View3D::reload_scene` 轉發（若要從 view3D 層傳遞，需同步加可選參數）。
- **優點**：
  - **不碰使用者的 gizmo / selection / 相機狀態**——零 UI 副作用，正中「不去動使用者 UI 工具」的目標。
  - 語意精準：「為了離屏渲染強制載入支撐」直接表達在 reload_scene 介面上，符合該函式既有以 bool flag 控制載入行為的風格（已有 `force_full_scene_refresh`）。
  - 時序明確：單次同步 reload，緊接同步 render，無 race。
  - 因為 force-load 只發生在這次離屏渲染前，screen 上的 gizmo 雙重渲染問題不存在（縮圖畫到 FBO）。
- **缺點**：
  - 動到共用函式簽章 `reload_scene`，需同步更新轉發點（`GUI_Preview.cpp:224` 的 `View3D::reload_scene`）。屬小範圍、可控。
  - 預設參數需確保所有既有呼叫端維持原行為（預設 `false` 即可，行為不變）。

#### 對比小結

| 面向 | (a) 中性化 gizmo | (b) force_load override |
|---|---|---|
| 改動共用簽章 | 否 | 是（reload_scene + View3D 轉發） |
| 動到 live UI 狀態 | 是（gizmo/selection/相機） | 否 |
| reload 次數 | 2（載入+還原） | 1（+可選還原） |
| race / 時序風險 | 較高（依賴 gizmo 切換副作用） | 低（單次同步） |
| 還原複雜度 | 需記錄並還原 gizmo | 幾乎無 |
| 語意清晰度 | 較隱晦 | 直接表達意圖 |

### D3（推薦決策）：採用路線 (b)

推薦 **(b) 為 `reload_scene` 增加 `force_load_sla_support` 預設參數**。

理由：它以最小且語意明確的介面擴充，達成「同步、不碰使用者 UI 狀態、無 race」三項核心目標；副作用面（不動 gizmo/selection/相機）顯著優於 (a)，而代價僅是一個帶預設值的可選參數與一處轉發更新——既有呼叫端零行為變化。(a) 雖不改簽章，但以「擾動再還原 live 狀態」換取，時序脆弱且副作用面更大，與「不去動使用者 UI 工具」的初衷相違。

實作要點（細節留待 specs / tasks 定稿）：
- `reload_scene` 新增 `bool force_load_sla_support = false`，僅在 `load_sla_support_pad_in_scene` 計算處以 `||` 併入。
- `View3D::reload_scene`（`GUI_Preview.cpp:224`）同步加可選參數轉發。
- `export_prz()`：渲染縮圖前以 `force_load_sla_support=true` 同步 reload；`tparams.parts_only=false`；渲染後視需要再一次普通 reload 回復螢幕視覺（離屏渲染本身不改 `m_volumes` 結構，但 force-load 已改變場景內容，故收尾還原較保險）。

## Risks / Trade-offs

- **收尾還原**：force-load 後 `m_volumes` 含支撐，若使用者匯出前正開著支撐 gizmo，渲染後應再普通 reload 一次，使螢幕回到 gizmo 啟用時「不顯示完整支撐網格」的原狀，避免畫面殘留雙重渲染。屬可控的單次同步操作。
- **預設參數相容性**：`reload_scene` 既有呼叫點眾多（Plater/ObjectList/Emboss 等），新增參數必須預設 `false` 並僅在 PRZ 匯出顯式傳 `true`，確保零回歸。
- **stale 支撐**：縮圖反映的是最後一次切片（`slapsRasterize` 完成）的支撐幾何，與實際寫入 PRZ 的光柵內容一致；若使用者改了支撐點但未重切，縮圖與既有匯出資料同樣以最後切片為準，行為一致、無新風險。
- **驗證以視覺為主**：縮圖為 OpenGL 離屏渲染輸出，無法用單元測試精確斷言像素；依使用者指定，以手動匯出 PRZ + 目視檢查（含「開著 gizmo 匯出」邊際案例與 FDM 3MF 回歸）為主要驗證手段。