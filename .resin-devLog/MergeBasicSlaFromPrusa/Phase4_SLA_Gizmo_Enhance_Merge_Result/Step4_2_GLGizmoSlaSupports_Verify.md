# Step 4.2: GLGizmoSlaSupports 功能驗證文件

**版本**: 1.0
**建立日期**: 2026-02-28
**對應 Result 文件**: `Step4_2_GLGizmoSlaSupports_Result.md`

---

## 驗證說明

本文件用於驗證 Step 4.2（GLGizmoSlaSupports 重構）完成後的功能正確性。
每個驗證項目包含：
- **目標**：該項目驗證的技術目標
- **操作步驟**：如何測試
- **預期結果**：應觀察到的現象
- **確認欄**：驗證通過後打勾

---

## 前置條件

- [ ] 使用 SL1 印表機（或其他 SLA 機種）
- [ ] 載入一個 STL 模型

---

## 1. Gizmo 開啟行為

**目標**：驗證 GLGizmoSlaBase 繼承後建構函式正常執行，Gizmo 可正常開啟。

| # | 操作步驟 | 預期結果 | 通過 |
|---|---------|---------|:----:|
| 1.1 | 點擊左側工具列的「SLA Support Points」圖示 | Gizmo 開啟，不崩潰 | [ ] |
| 1.2 | 觀察開啟後 3D 視窗中的模型外觀 | 模型本體被隱藏，改由 Gizmo 渲染半透明模型（set_hide_full_scene 效果） | [ ] |
| 1.3 | 觀察是否有支撐點（若模型已有） | 支撐點正常顯示在模型表面 | [ ] |

---

## 2. 支撐點渲染（render_points）

**目標**：驗證移除 `bool picking` 參數、移除 drain hole 渲染、加入 raycaster clipping 後，支撐點顯示正確。

| # | 操作步驟 | 預期結果 | 通過 |
|---|---------|---------|:----:|
| 2.1 | 在無支撐點的模型上開啟 Gizmo | 畫面乾淨，無多餘幾何體，不崩潰 | [ ] |
| 2.2 | 新增幾個手動支撐點後觀察顏色 | 手動點 = 青色（CYAN），自動生成點 = 藍色（BLUEISH） | [ ] |
| 2.3 | 滑鼠 hover 到支撐點上 | hover 點顏色變為淺灰（LIGHT_GRAY） | [ ] |
| 2.4 | 啟用裁切平面（Object Clipper），觀察被切到的支撐點 | 被裁切平面切到的支撐點消失（raycaster active 管理正常） | [ ] |
| 2.5 | 有排水孔的模型（先用 Hollow Gizmo 打洞）開啟 Supports Gizmo | 排水孔的圓柱不出現在 Supports 渲染中（drain holes 已移至 Hollow 負責） | [ ] |

---

## 3. 編輯模式（Editing Mode）

**目標**：驗證 `switch_to_editing_mode()` / `disable_editing_mode()` 中的 `show_sla_supports()` 呼叫正確。

| # | 操作步驟 | 預期結果 | 通過 |
|---|---------|---------|:----:|
| 3.1 | 點擊「Edit」按鈕進入編輯模式 | 成功進入，UI 顯示 editing 狀態 | [ ] |
| 3.2 | 進入編輯模式後觀察支撐結構 | 支撐結構（support tree）被隱藏（show_sla_supports(false) 效果） | [ ] |
| 3.3 | 點擊模型表面新增支撐點 | 新增點出現，顯示為青色 | [ ] |
| 3.4 | 右鍵點擊已有支撐點 | 支撐點被刪除 | [ ] |
| 3.5 | 拖曳滑鼠框選多個支撐點 | 矩形選框正常顯示，點擊後刪除被選中的點 | [ ] |
| 3.6 | 點擊「Confirm」退出編輯模式 | 退出成功，支撐結構重新顯示（show_sla_supports(m_show_support_structure) 效果） | [ ] |
| 3.7 | 重新進入編輯模式後再點「Cancel」退出 | 退出成功，變更取消，支撐點還原到進入前的狀態 | [ ] |

---

## 4. 輸入鎖定（is_input_enabled）

**目標**：驗證 `on_mouse()` 新增的 `is_input_enabled()` 防護正常運作。

| # | 操作步驟 | 預期結果 | 通過 |
|---|---------|---------|:----:|
| 4.1 | 在重切片進行中（進度條顯示）嘗試點擊模型新增支撐點 | 操作被忽略，不新增支撐點，不崩潰 | [ ] |

---

## 5. 自動生成（Auto Generate）

**目標**：驗證 `auto_generate()` 改用 `reslice_until_step(slaposSupportPoints)` 後正常觸發重切片。

| # | 操作步驟 | 預期結果 | 通過 |
|---|---------|---------|:----:|
| 5.1 | 點擊「Auto-generate points」按鈕 | 觸發重切片，進度條顯示 slaposSupportPoints 步驟進行中 | [ ] |
| 5.2 | 等待自動生成完成 | 支撐點自動填入模型，顯示為藍色（自動生成顏色） | [ ] |
| 5.3 | 自動生成後查看支撐點數量 | 數量合理（依模型複雜度，通常數十至數百個） | [ ] |

---

## 6. Gizmo 關閉行為

**目標**：驗證 `on_set_state()` 中 `set_hide_full_scene(false)` 在關閉時正確還原場景可見性。

| # | 操作步驟 | 預期結果 | 通過 |
|---|---------|---------|:----:|
| 6.1 | 點擊其他 Gizmo 圖示關閉 Support Points Gizmo | Gizmo 正常關閉，不崩潰 | [ ] |
| 6.2 | 關閉後觀察 3D 視窗的模型外觀 | 模型本體重新顯示（set_hide_full_scene(false) 效果） | [ ] |
| 6.3 | 按 Esc 鍵關閉 Gizmo | 同上，模型恢復正常顯示 | [ ] |
| 6.4 | 關閉後切換到 FDM 印表機 | FDM 模式正常，不崩潰，FDM 相關 UI 正常顯示 | [ ] |

---

## 7. Undo / Redo（包含 GLGizmosManager.cpp 修復）

**目標**：驗證 `update_after_undo_redo()` 改用 `reslice_until_step(slaposSupportPoints, true)` 後 Undo/Redo 正常。

| # | 操作步驟 | 預期結果 | 通過 |
|---|---------|---------|:----:|
| 7.1 | 手動新增幾個支撐點後按 Ctrl+Z | 支撐點被撤銷，畫面更新 | [ ] |
| 7.2 | Undo 後按 Ctrl+Y（Redo） | 支撐點重新出現 | [ ] |
| 7.3 | Undo/Redo 觸發重切片 | 底部進度條正常顯示，不崩潰 | [ ] |
| 7.4 | 連續多次 Undo/Redo | 每次狀態正確切換，支撐點數量正確，不崩潰 | [ ] |

---

## 8. 與 GLGizmoHollow 整合（Step 4.3 整合測試）

**目標**：驗證兩個 SLA Gizmo（均繼承 GLGizmoSlaBase）切換時無衝突。

| # | 操作步驟 | 預期結果 | 通過 |
|---|---------|---------|:----:|
| 8.1 | 先開啟 Hollow Gizmo，再切換到 Support Points Gizmo | 切換正常，不崩潰，模型正確顯示 | [ ] |
| 8.2 | 先開啟 Support Points Gizmo，再切換到 Hollow Gizmo | 切換正常，不崩潰 | [ ] |
| 8.3 | 在有排水孔的模型上使用 Support Points Gizmo | 排水孔幾何體不影響支撐點 Gizmo 的渲染 | [ ] |

---

## 驗證總結

| 章節 | 驗證項數 | 通過 | 備註 |
|------|:-------:|:----:|------|
| 1. Gizmo 開啟行為 | 3 | / | |
| 2. 支撐點渲染 | 5 | / | |
| 3. 編輯模式 | 7 | / | |
| 4. 輸入鎖定 | 1 | / | |
| 5. 自動生成 | 3 | / | |
| 6. Gizmo 關閉行為 | 4 | / | |
| 7. Undo / Redo | 4 | / | |
| 8. 與 Hollow 整合 | 3 | / | |
| **合計** | **30** | **/30** | |

---

**驗證人員**：_______________
**驗證日期**：_______________
**最終結論**：[ ] 全部通過，Step 4.2 驗證完成
