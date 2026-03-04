# Step 4.8: 修復 `drill_holes()` — 恢復 BBS 封鎖的掏空鑽孔功能

## 概述

修復切片時拋出 `"Inconsistent slice index"` 的錯誤。根因為 `SLAPrint::Steps::drill_holes()`
的整個函式本體被 BambuStudio 以 `/* ... */` 封鎖成 no-op stub，導致掏空流程中間缺失關鍵資料。

---

## 問題根因分析

### 錯誤觸發條件

1. 進入 Hollow and Drill Gizmo
2. 勾選 **Hollow this object**
3. 按下 **Preview hollowed and drilled model**（觸發 `slaposDrillHoles` reslice）
4. 離開 Gizmo → 切換到 Preview 頁面
5. 執行完整切片 → 拋出 `"Inconsistent slice index"`

### SLA Pipeline 執行順序

```
slaposHollowing   (step 0) — 建立 m_hollowing_data->interior ✅
slaposDrillHoles  (step 1) — 應填充 hollow_mesh_with_holes ❌ (stub 無效)
slaposObjectSlice (step 2) — 呼叫 slice_model() → get_mesh_to_slice()
...
```

### 錯誤鏈

```
get_mesh_to_slice() {
    if (m_hollowing_data && is_step_done(slaposDrillHoles))
        return m_hollowing_data->hollow_mesh_with_holes;  // ← 空 TriangleMesh（never filled）
    return transformed_mesh();
}

slice_model()
  → mesh = get_mesh_to_slice()           // 空 mesh
  → mesh.bounding_box()                  // 無效 bounding box
  → closest_slice_record() → end()       // lower_bound 找不到對應 record
  → throw RuntimeError("Inconsistent slice index")  // ← 此錯誤
```

### BBS 封鎖的原因

`drill_holes()` 整個函式本體被 BambuStudio 用 `/* ... */` 封鎖：

```cpp
void SLAPrint::Steps::drill_holes(SLAPrintObject &po)
{
    /*                    ← BBS 封鎖開始
    bool needs_drilling = ...;
    bool is_hollowed = ...;
    // ... 完整的掏空 + 鑽孔邏輯 ...
    if (hole_fail)
        po.active_step_add_warning(...);
     */                   ← BBS 封鎖結束（行 476）
}
```

OrcaSlicer 繼承此 stub，PhrozenOrca 繼承 OrcaSlicer，因此同樣受影響。

---

## 修改內容

### `SLAPrintSteps.cpp` — 恢復 `drill_holes()` 函式本體

**移除** 第 345 行的 `/*` 開頭（替換為說明註解）：

```cpp
// Step 4.8: Restored from BBS block comment. drill_holes() was a no-op stub:
// hollow_mesh_with_holes was never populated → get_mesh_to_slice() returned empty mesh
// when hollowing was enabled → slice_model() threw "Inconsistent slice index".
bool needs_drilling = ! po.m_model_object->sla_drain_holes.empty();
```

**移除** 第 476 行的 ` */` 結尾。

**AABBTree 最佳化保持停用**（`//BBS` 單行註解，已由前人停用）：

```cpp
//BBS: AABBTree optimization disabled — part_to_drill.indices is cleared each iteration
//BBS: and never refilled (traverse is commented out below), so cgal_meshpart is always
//BBS: an empty mesh. The self-intersection pre-check is effectively skipped; all holes
//BBS: are accumulated into holes_mesh_cgal and subtracted from the hollowed mesh.
//auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
//    hollowed_mesh.its.vertices,
//    hollowed_mesh.its.indices
//);
```

**`auto bb` / `Eigen::AlignedBox ebb`** 也轉為 `//BBS` 註解（僅在停用的 traverse 中使用，
避免未使用變數編譯警告）：

```cpp
//BBS: bb/ebb only used in the disabled AABBTree traversal.
//auto bb = bounding_box(m);
//Eigen::AlignedBox<float, 3> ebb{bb.min.cast<float>(),
//                                bb.max.cast<float>()};
```

---

## AABBTree 停用後的行為說明

| 項目 | AABBTree 啟用時（PrusaSlicer） | AABBTree 停用時（PhrozenOrca） |
|------|-------------------------------|-------------------------------|
| `part_to_drill.indices` | 填入鄰近面（bounding box 篩選）| 每次 `clear()` 後為空 |
| `cgal_meshpart` | 包含鄰近面的子網格 | 始終為空 mesh |
| `does_self_intersect(cgal_meshpart)` | 對鄰近面做自相交檢測 | 空 mesh → 始終 false |
| 各孔的處理 | 局部面子集做 CGAL boolean | 跳過自相交檢測，直接累積到 `holes_mesh_cgal` |
| 最終 CGAL minus | 正確鑽孔 | 正確鑽孔（功能相同，少一道效率最佳化）|

**結論**：停用 AABBTree 不影響鑽孔正確性，僅少了「先用 AABB 過濾鄰近面」的效率最佳化。

---

## 修改的檔案

| 檔案 | 修改內容 |
|------|---------|
| `src/libslic3r/SLAPrintSteps.cpp` | 移除 `/* ... */` 封鎖，恢復 `drill_holes()` 本體；`tree`/`bb`/`ebb` 轉為 `//BBS` 註解 |
| `src/libslic3r/PrintConfig.cpp` | 恢復 hollowing 四個參數的 label/category/tooltip；修正 mode 對應 |

---

## 修正二：Hollow and Drill Gizmo UI 參數標籤缺失

### 問題現象

進入 Hollow and Drill Gizmo 後，UI 與 PrusaSlicer 相比缺少三個參數：
- Wall thickness（壁厚）
- Accuracy（精度）
- Closing distance（閉合距離）

### 根因

`PrintConfig.cpp` 中四個 hollowing 參數的 `label`/`category`/`tooltip` 全部被 BBS 注解掉。
`on_render_input_window()` 動態從 config def 取得標籤：

```cpp
m_desc["offset"]           = _(opts[0].second->label) + ":";  // label 為空 → 只顯示 ":"
m_desc["quality"]          = _(opts[1].second->label) + ":";  // label 為空 → 只顯示 ":"
m_desc["closing_distance"] = _(opts[2].second->label) + ":";  // label 為空 → 只顯示 ":"
```

此外，`hollowing_quality` 和 `hollowing_closing_distance` 的 `mode` 為 `comAdvanced`，
但 PhrozenOrca 的 `comDevelop`（= 2，對應 PrusaSlicer `comExpert`）是開發者隱藏模式，
一般用戶無法進入，正確對應應為 `comAdvanced`。

### PrintConfig.cpp 修改

**修改前**（BBS 全部注解）：

```cpp
def = this->add("hollowing_min_thickness", coFloat);
//def->label = L("");
//def->category = L("");
//def->tooltip = L("");
//def->sidetext = "";
def->mode = comSimple;

def = this->add("hollowing_quality", coFloat);
//def->label = L("");
//def->mode = comAdvanced;   ← 錯誤：PhrozenOrca comDevelop 是開發者模式

def = this->add("hollowing_closing_distance", coFloat);
//def->label = L("");
//def->mode = comAdvanced;   ← 同上
```

**修改後**（對齊 PrusaSlicer）：

```cpp
def = this->add("hollowing_enable", coBool);
def->label = L("Enable hollowing");
def->category = L("Hollowing");
def->tooltip = L("Hollow out a model to have an empty interior");
def->mode = comSimple;

def = this->add("hollowing_min_thickness", coFloat);
def->label = L("Wall thickness");
def->category = L("Hollowing");
def->tooltip = L("Minimum wall thickness of a hollowed model.");
def->sidetext = L("mm");
def->mode = comSimple;

def = this->add("hollowing_quality", coFloat);
def->label = L("Accuracy");
def->category = L("Hollowing");
def->tooltip = L("Performance vs accuracy of calculation. Lower values may produce unwanted artifacts.");
def->mode = comAdvanced; // PrusaSlicer: comExpert (PhrozenOrca has no comExpert; comAdvanced is the highest user-facing mode)

def = this->add("hollowing_closing_distance", coFloat);
def->label = L("Closing distance");
def->category = L("Hollowing");
def->tooltip = L("Hollowing is done in two steps: first, an imaginary interior is "
    "calculated deeper (offset plus the closing distance) in the object and "
    "then it's inflated back to the specified offset. A greater closing "
    "distance makes the interior more rounded. At zero, the interior will "
    "resemble the exterior the most.");
def->sidetext = L("mm");
def->mode = comAdvanced; // PrusaSlicer: comExpert
```

### PhrozenOrca Mode 體系對照

| PrusaSlicer | 值 | PhrozenOrca | 值 | 說明 |
|-------------|---|-------------|---|------|
| `comSimple` | 0 | `comSimple` | 0 | 基本模式 |
| `comAdvanced` | 1 | `comAdvanced` | 1 | 進階模式（右側面板 "Advance" 切換開關）|
| `comExpert` | 2 | `comDevelop` | 2 | 開發者模式（隱藏，一般用戶不使用）|

**切換 Advanced 模式的方法**：右側 Process 面板頂部的 **"Advance"** 切換開關 → 開啟

### Hollow Gizmo UI 顯示邏輯

| 模式 | Wall thickness | Accuracy | Closing distance |
|------|:---:|:---:|:---:|
| Simple | ✅ | ❌ | ❌ |
| Advanced | ✅ | ✅ | ✅ |

---

## 驗證結果

| 測試項目 | 結果 |
|----------|------|
| 編譯 | ✅ 成功 |
| 勾選 Hollow + Preview | ✅ 正確顯示中空模型 |
| Preview 模式下的中空模型 | ✅ 正常顯示 |
| 完整切片（不再拋錯）| ✅ `"Inconsistent slice index"` 不再出現 |
| 無 drain hole 的純掏空 | ✅ 正常（`needs_drilling = false` 分支正確 return）|
| Wall thickness 參數標籤 | ✅ 正確顯示（Simple 模式可見）|
| Accuracy 參數標籤 | ✅ 正確顯示（Advanced 模式可見）|
| Closing distance 參數標籤 | ✅ 正確顯示（Advanced 模式可見）|
| FDM 路徑 | ✅ 不受影響（SLA-only code path）|

---

## 技術備注

### `closest_slice_record()` 為何會失敗

```cpp
// SLAPrint.hpp（line 190）
template<class Container>
auto closest_slice_record(const Container &cont, float lvl, float merr = 5e-3) {
    auto it = std::lower_bound(cont.begin(), cont.end(), SliceRecord::UndefID,
        [lvl](const SliceRecord &sr, SliceRecord::Key) {
            return sr.print_z() < lvl - SliceRecord::epsSlow;
        });
    if (it == cont.end() || std::abs(it->print_z() - lvl) > merr)
        return cont.end();   // ← 空 mesh → 空 cont → 一定回傳 end()
    return it;
}
```

空 mesh 的 bounding box 導致 `lvl` 為無效值，`lower_bound` 永遠找不到對應記錄。

---

## 與 Step 4.7 的關係

Step 4.7 修正了 `GLGizmoHollow` 進入時的 UI 狀態（no-step constructor），使「Preview
hollowed and drilled model」按鈕可以正確點擊並觸發 `slaposDrillHoles`。

Step 4.8 修正了 `drill_holes()` 本身的 no-op 問題，使 `slaposDrillHoles` 實際填充
`hollow_mesh_with_holes`，讓後續的完整切片不再失敗。

兩個步驟合力完成 Hollow and Drill Gizmo 的完整功能修復。

---

## 後續步驟

- Step 2.5：SLA Layer Slider 實作（計畫中）
