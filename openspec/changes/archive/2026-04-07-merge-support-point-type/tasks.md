## 1. 資料結構

- [x] 1.1 在 `SupportPoint.hpp` 中新增 `SupportPointType` 列舉（`manual_add=0`、`island=1`、`slope=2`）
- [x] 1.2 將 `SupportPoint` struct 中的 `bool is_new_island` 替換為 `SupportPointType type = SupportPointType::manual_add`
- [x] 1.3 更新所有 `SupportPoint` 建構子，改用 `type` 參數取代 `is_new_island`
- [x] 1.4 新增 `is_island()` const 方法：`return type == SupportPointType::island`
- [x] 1.5 更新 `operator==`，改比較 `type` 而非 `is_new_island`
- [x] 1.6 更新 cereal `serialize()` 方法：`ar(pos, head_front_radius, type)`

## 2. 支撐點生成器

- [x] 2.1 在 `SupportPointGenerator.cpp` 中：將懸臂/slope 呼叫位置的 `/* is_new_island */ false` 替換為 `SupportPointType::slope`（約第 240 行）
- [x] 2.2 在 `SupportPointGenerator.cpp` 中：將 island 呼叫位置的 `/* is_new_island */ true` 替換為 `SupportPointType::island`（約第 268 行）
- [x] 2.3 在 `SupportPointGenerator.cpp` 中：將半島呼叫位置的 `/* is_new_island */ true` 替換為 `SupportPointType::island`（約第 296 行）
- [x] 2.4 審查 PhrozenOrca SupportIslands 程式碼中所有其他 `SupportPoint{...}` 建構子對 `is_new_island` 的使用並更新
      → 確認無需修改（SupportIslands 程式碼不直接使用 is_new_island）

## 3. 檔案格式遷移

- [x] 3.1 在 `3mf.cpp` 寫入路徑：將 type 編碼為浮點數（`island=1.0`、`manual_add=2.0`、`slope=3.0`）
- [x] 3.2 在 `3mf.cpp` 讀取路徑：實作範圍型解碼（`0.999–1.001→island`、`1.999–2.001→manual_add`、`2.999–3.001→slope`，其餘→slope）
- [x] 3.3 對 `bbs_3mf.cpp` 套用相同的寫入/讀取變更
      → 同時解除所有 SLA 支撐點相關 comment block，並在 `Plater::export_3mf()` 加入存檔前後端同步邏輯
- [x] 3.4 對 `AMF.cpp` 套用相同的寫入/讀取變更

## 4. GLGizmo UI

- [x] 4.1 在 `GLGizmoSlaSupports.cpp` 中：新增三種渲染顏色（`manual_color`、`island_color`、`inactive_color/slope_color`）
- [x] 4.2 更新 `render_points()` 的顏色選擇，根據 `support_point.type` 分支（對應 PrusaSlicer 第 289–292 行）
- [x] 4.3 更新 `on_dragging()`：拖曳完成時設定 `support_point.type = SupportPointType::manual_add`
- [x] 4.4 更新刪除保護：在選取/刪除邏輯中以 `is_island()` 取代 `is_new_island`
- [x] 4.5 更新統計：分別計算 `manual_add` 和 `island` 數量，顯示 `"N(M manual) support points (I on islands)"` 字串
- [x] 4.6 更新 `m_normal_cache` 重建（約第 1212 行）：向 `SupportPoint` 建構子傳入 `type` 而非 `is_new_island`

## 5. 驗證

- [x] 5.1 編譯並確認與 `is_new_island` 相關的錯誤為零（所有引用皆已替換）
- [x] 5.2 載入含有 SLA 支撐的既有 3mf 檔案——確認點可正常載入且不崩潰，舊的 `0.0`/`1.0` 值對應到正確類型
- [x] 5.3 儲存含混合支撐類型的模型，重新載入，確認類型正確保留
- [x] 5.4 在 GLGizmo 中：放置手動點（青色），自動生成（觀察 island/slope 顏色），拖曳 island 點（確認轉為 manual）
- [x] 5.5 確認統計行顯示正確的手動/island 計數
- [x] 5.6 確認 FDM 印表機切片不受影響（開啟 FDM 設定檔，切片一個簡單模型）

## 相關 Commits

- `d01f74245c` feat: 實作 merge-support-point-type（SupportPointType enum 取代 bool is_new_island）
- `2d6cf87f42` feat: 啟用 bbs_3mf SLA 支撐點存取 + 存檔前同步後端資料
