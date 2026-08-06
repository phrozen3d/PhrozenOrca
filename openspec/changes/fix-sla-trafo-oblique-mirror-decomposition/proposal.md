## Why

> **狀態：暫緩，不在 `fix-sla-support-point-issues` 分支的處理範圍內。** 本 change 原名 `fix-sla-support-points-invalidate-on-trafo-change`，起因是「自動生成支撐點後鏡像/旋轉物件，支撐點位置跑掉」，一開始以為是 GUI 前端快取沒跟著失效。診斷到底發現根因其實是 `SLAPrint::sla_trafo()`（核心引擎程式碼，`SLAPrint.cpp:235`）本身在特定情況下算錯，不是 gizmo 前端的問題——已超出本分支「support point GUI 問題」的範圍，且核心引擎的修正牽涉面比 GUI 修正大，決定等 `fix-sla-support-point-issues` 分支合併回 `resin-dev` 主分支後，再另外評估處理時機與優先度。以下記錄已確認的根因與證據，供之後撿回來時使用。

自動生成支撐點後，對物件執行**任意角度旋轉**（非 90° 整數倍）＋**任一次鏡像**，支撐點的位置、方向、間距會被嚴重扭曲；不鏡射、或鏡射偶數次（互相抵消）則正常。

## 已確認的根因

`SLAPrint::sla_trafo()`（`SLAPrint.cpp:235-269`）不是直接拿完整的 instance transform 矩陣去掉幾個分量，而是把它**拆開再重組**：

```cpp
Transform3d SLAPrint::sla_trafo(const ModelObject &model_object) const
{
    ...
    Vec3d offset   = model_instance.get_offset();
    Vec3d rotation = model_instance.get_rotation();       // 分解 #1
    offset(0) = 0.; offset(1) = 0.;
    rotation(2) = 0.;
    ...
    trafo.translate(offset);
    trafo.scale(corr);
    trafo.rotate(Eigen::AngleAxisd(rotation.z(), Vec3d::UnitZ()));
    trafo.rotate(Eigen::AngleAxisd(rotation.y(), Vec3d::UnitY()));
    trafo.rotate(Eigen::AngleAxisd(rotation.x(), Vec3d::UnitX()));
    trafo.scale(model_instance.get_scaling_factor());     // 分解 #2
    trafo.scale(model_instance.get_mirror());              // 分解 #3

    if (model_instance.is_left_handed())
        trafo = Eigen::Scaling(Vec3d(-1., 1., 1.)) * trafo;

    return trafo;
}
```

`get_rotation()` / `get_scaling_factor()` / `get_mirror()`（`Geometry.cpp:471-562`）三者都呼叫 Eigen 的 `computeRotationScaling()`——一種極分解（polar decomposition）：把矩陣拆成「純旋轉（正交、行列式 +1）」乘上「一個對稱矩陣（scale 部分）」。

**這個對稱 scale 矩陣，只有在旋轉是軸對齊（90° 整數倍）時才會剛好落在對角線上。** 一旦旋轉是任意角度，scale 矩陣會產生非對角線的 skew（歪斜）項——但 `get_mirror()` 與 `get_scaling_factor()` 只讀 `scale(0,0)/scale(1,1)/scale(2,2)` 這三個對角線元素，把它們當成 X/Y/Z 各軸獨立的縮放/鏡射直接使用，**完全忽略非對角線的 skew 項**。`sla_trafo()` 再用這三個（可能已經錯誤的）分解結果重新組裝矩陣，重組出來的矩陣因此可能跟原始的 instance transform 不一致。

值得注意：這個 codebase 本身就有 `has_skew()` / `contains_skew()`（`Geometry.cpp:444-469`）專門偵測這種情況，代表這個陷阱是已知的，只是 `sla_trafo()` 的重組邏輯沒有檢查就直接使用分解結果。

### 實測證據（四組測試，詳見下方）

| 情境 | `is_left_handed()` | 是否有 skew（旋轉是否軸對齊） | 結果 |
|---|---|---|---|
| 任意角度旋轉 + 鏡射一次（任一軸） | true | 有（旋轉非軸對齊） | **壞**：位置/方向嚴重扭曲、間距被拉開、法向量歪斜、overhang 分類全部誤判為 island |
| 任意角度旋轉 + 鏡射兩次（抵消） | false | 無（等效無鏡射） | 正常 |
| 任意角度旋轉，不鏡射 | false | 無（純旋轉的 scale 分解恆為單位矩陣） | 正常 |
| 複合旋轉（不鏡射）+ 非均勻縮放 | false | 無 | 位置大致正常，但 Structure 模式下支撐樹底板歪斜（可能是獨立問題，見下方 Test 4 備註） |

**規律與旋轉軸、鏡射軸皆無關，只取決於「是否同時存在 `is_left_handed()`（鏡射奇數次）與非軸對齊旋轉」。**

#### 完整測試記錄

**測試 1**：Y 軸旋轉 80° → auto generate apply → 支撐點正確（法向量朝 -X,-Z，僅底部一排 island）。沿 X 軸鏡射 → 重新進支撐模式，前次支撐點消失（`SLAPrint::apply()` 的 `sla_trafo_differs` 正確觸發 `invalidate_all_steps()`，符合預期）。重新 Apply → 支撐點嚴重異常：三段式 pinhead 各自被拉長、全部變成 island、法向量與模型底面不垂直、位置分佈與預期不符（部分跑到模型邊緣），但拉長方向一致。再次沿 X 軸鏡射（抵消，等效無鏡射）→ 重新 Apply → 恢復正常。改沿 Y 軸或 Z 軸鏡射（同樣是奇數次、单一軸）→ 重新 Apply → 據回報「正常」，但這幾次測試的鏡射狀態疊加關係未逐一控制，不排除實際上落在偶數次鏡射的狀態（見測試 2 的乾淨對照）。

**測試 2**：X 軸旋轉 80° → auto apply → 正確。沿 Y 軸鏡射一次 → 重新 apply → 出現與測試 1 相同的扭曲。沿 X 軸鏡射 → 再沿 Y 軸鏡射（兩次，互相抵消 handedness）→ 重新 apply → 支撐點正常。這組是乾淨的單變數對照，確認「任意角度旋轉 + 奇數次鏡射」才是觸發條件，與鏡射的是哪一軸、旋轉的是哪一軸無關。

**測試 3**：純旋轉（X、Y、Z 軸皆試過，不鏡射）→ 離開再進支撐模式，點消失、重新 Apply → 正常。確認純旋轉（不鏡射）不觸發本症狀。

**測試 4**：複合旋轉（X、Y 各 80°，不鏡射）→ 非均勻縮放（Z 軸）→ 重新 Apply → Points 模式下位置大致正常（X 軸有輕微但不明顯的拉伸）。但切到 Structure 模式，**整個支撐樹底板沒有貼平在列印平面上**。這個情境沒有鏡射（`is_left_handed()` 為 false），不完全符合上述規律，可能是 `Pad.cpp` 底板生成邏輯在複合旋轉＋非均勻縮放下的另一個、獨立於本 change 主症狀的問題，待之後處理本 change 時一併確認是否為同一根因。

## 影響範圍評估（尚未驗證，恢復處理時第一步）

`sla_trafo()` 不只是前端 preview 在用，也是**後端實際切片**定位模型、計算支撐幾何的依據（`SLAPrint.cpp:642`）。如果這個矩陣本身算錯，理論上代表「任意角度旋轉 + 鏡射」的物件，**實際切出來的支撐位置/方向也可能是錯的**，不只是 preview 畫面的問題。

**這個嚴重度尚未經過實測確認**——恢復處理本 change 前，第一步應該是：實際對一個「Y 轉 80° + X 鏡射」的物件切片一次，比對切出來的支撐樹跟預期（例如同一個模型未鏡射、只是物理上翻面後應該長的樣子）是否吻合。這個結果會決定後續的優先度與處理方式（單純的視覺/preview 瑕疵、還是會影響實際列印結果的嚴重 bug）。

## What Changes

**本 change 目前不執行任何修正**，僅記錄診斷結論。待合併回 `resin-dev` 主分支、且完成上方「影響範圍評估」後，再依當時的優先度決定：

- 是否修正 `sla_trafo()` 的重組邏輯本身（例如改用 `has_skew()` 偵測到 skew 時採用不同的分解/重組策略，或改為直接對原始矩陣做代數操作移除 Z 旋轉與 XY 平移分量，而不經過分解-重組的往返）。
- 是否需要同步修正 `get_data_from_backend()` 的 `po->trafo().inverse()` 換算與前端快取的失效時機（原 change 的範圍）。
- 是否需要向上游 PrusaSlicer 回報或比對這是否為既有的上游問題。

### Non-goals（暫緩期間）

- 不在本分支（`fix-sla-support-point-issues`）內修正 `sla_trafo()`——核心引擎程式碼的修正風險與影響面比 GUI 修正大，需要獨立的分支與更完整的測試（含實際切片輸出比對），不適合跟一批 GUI 支撐點修正混在一起。
- 不修正原 `fix-sla-support-points-invalidate-on-trafo-change` 設定的「前端快取失效時機」範圍——那個範圍本身沒有錯，只是不是本症狀的根因，是否仍需要獨立處理待恢復時重新評估（`sla_trafo()` 修好後，原本規劃的「快取依 `sla_trafo` 指紋失效」仍然是必要的收尾，只是不是最優先的部分）。

## Capabilities

### New Capabilities

<!-- 暫緩，不在本次範圍內產出。恢復處理時依當時決定的修正範圍重新評估。 -->

### Modified Capabilities

<!-- 暫緩。原提案的 `sla-support-points-trafo-invalidation` capability spec 保留在 specs/ 目錄供參考，但其內容假設的根因（前端快取失效時機）已被本次診斷推翻，恢復處理時需要重寫，不能直接沿用。 -->

## Impact

- **Primary（恢復處理時）**：`src/libslic3r/SLAPrint.cpp` — `sla_trafo()`（`:235`）的矩陣分解/重組邏輯；可能需要 `src/libslic3r/Geometry.cpp` 的 `Transformation::get_rotation()` / `get_scaling_factor()` / `get_mirror()` 一併檢視
- **Reference**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — `get_data_from_backend()` 的換算、`data_changed()` 的失效判定（原 change 的範圍，待 `sla_trafo()` 修好後視情況並入）
- **嚴重度未知**：可能影響實際切片輸出（`sla_trafo()` 同時是後端定位依據），不只是 preview——見上方「影響範圍評估」
- 不影響檔案格式或 profile
- 無 public API 變更
