## Why

編輯模式下選定某個支撐點並編輯它的 Top 參數時，**其他不相干的 auto 生成支撐點**（沒有 explicit geometry 的點）外觀會跟著即時變化，直到取消選取才恢復原狀。這會讓使用者誤以為自己正在同時修改多顆點的參數。

根因是兩個各自獨立、各自正確的既有機制疊在一起產生的副作用：

**機制一（`perf-sla-support-points-preview-render`，已 archive）**：`render_points()` 每幀呼叫一次 [`read_preview_top_params_live()`](src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp#L237-247)，取得的 `PreviewTopParams` 套用給所有**沒有 explicit geometry** 的點（即全部 auto 點）。它內部呼叫 [`process_top_float_live()`](src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp#L130-158) 與 `process_contact_type_is_sphere()`，兩者都是先嘗試 `Tab::get_field(key)->get_value()`——直接讀 Process tab 那組 widget**目前顯示的文字**，若欄位不存在才退回 `sla_process_config()`。這是刻意設計：讓使用者在 Process tab 打字打到一半、還沒失焦時，auto 點的 preview 就能即時反應。

**機制二（`fix-sla-support-top-config-enum-set`，已 archive）**：選定一顆支撐點時，[`begin_support_point_top_field_display()`](src/slic3r/GUI/Tab.cpp) 會把**同一組** Top widget 的顯示文字，暫時改寫成「這顆被選中的點自己的參數值」，供使用者檢視／編輯該點專屬的設定。

兩個機制單獨看都沒問題。疊在一起時：機制一讀值**不知道、也不檢查**現在 widget 上顯示的文字是「Process tab 的即時 preset 值」還是「機制二暫時借用來顯示某顆點的值」——它一律當作前者使用。於是選定一顆點、看到 Top 欄位顯示那顆點的參數時，同一幀 `read_preview_top_params_live()` 讀到的正是這些「借來顯示」的數字，並把它們套用到所有 auto 點身上，讓它們的 preview 尺寸跟著臨時飄移。取消選取後，widget 顯示的文字變回真正的 preset 值，auto 點的外觀也就跟著變回來。

本 change 於驗收 `fix-sla-support-preview-stored-geometry-in-auto-mode` 期間，透過 `opsx:explore` 討論發現並確認根因，屬 `fix-sla-support-point-issues` 分支的一部分，與該分支其他 change 彼此獨立。

## What Changes

- 在 `process_top_float_live()` 與 `process_contact_type_is_sphere()` 的 widget 讀值路徑前，新增一道判斷：若目前有支撐點被選取（`GLGizmoSlaSupports::has_selected_support_points() == true`，即 Top widget 目前正被機制二借用），跳過 widget 讀值，直接使用 `sla_process_config()` 的實際 preset 值。
- 沒有點被選取時，行為完全不變——維持「讀 widget 目前顯示的文字，讓使用者打字到一半就能看到 auto 點即時反應」的既有語意。

### Non-goals

- 不改變 `begin_support_point_top_field_display()` / `end_support_point_top_field_display()`（機制二本身）的行為，本 change 只改變機制一在「機制二正在借用 widget」期間該讀哪裡的值。
- 不改變支撐點的幾何尺寸解析規則（per-point vs preset 的判定邏輯）——那是 `fix-sla-support-preview-geometry-source-semantics` 的範圍，且該問題與本問題是不同軸線：那邊問「這顆點自己該用哪組參數」，本 change 問「畫**其他點**時，讀值機制有沒有被別的點的顯示狀態污染」。
- 不改變無選取狀態下「打字到一半就即時反應」的 live 語意，這是本 change 明確要保留的既有行為。
- 不處理支撐點 undo/redo、`sla_trafo` 快取失效、視覺呈現統一（形狀/光影/顏色）等其他已知問題，各自有獨立的 change。

## Capabilities

### New Capabilities

<!-- 無。本 change 為既有 capability 增補 requirement。 -->

### Modified Capabilities

- `sla-support-points-preview`：新增 requirement，規範「選定支撐點期間，其餘點的 preview 幾何來源不得被選中點目前顯示的值污染」。該 capability 現有的 requirement（anchor 位置、尺寸 mm 不變、picking 一致性、幾何來源解析、視覺呈現）皆不變更。

## Impact

- **Primary**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — `process_top_float_live()`、`process_contact_type_is_sphere()` 兩個 widget 讀值函式
- **Reference（不預期修改）**：`read_preview_top_params_live()`、`update_point_raycasters_for_picking_transform()` — 呼叫端邏輯不變，只是它們呼叫的底層函式行為在「有點被選取」時改變
- 不影響切片輸出、檔案格式或 profile
- 無 public API 變更
