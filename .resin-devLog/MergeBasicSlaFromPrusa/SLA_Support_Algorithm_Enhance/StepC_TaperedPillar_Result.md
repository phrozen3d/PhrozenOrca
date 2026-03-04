# Step C：SLA Pillar 漸縮柱（Tapered Pillar）同步 — 執行結果

**完成日期**: 2026-03-04
**分析依據**: Plan file `frolicking-munching-turing.md`
**狀態**: ✅ 已完成（編譯通過，執行驗證完成）

---

## 問題背景

Step B 完成後，確認 PhrozenOrca 與 PrusaSlicer 的支撐柱**數量和位置算法完全相同**（`routing_to_ground()` 邏輯一致），
但在資料結構上存在一個差異：

| 項目 | PhrozenOrca | PrusaSlicer |
|------|-------------|-------------|
| Pillar 半徑欄位 | `double r`（單一半徑，等半徑圓柱） | `double r_start, r_end`（漸縮柱） |
| Mesh 生成 | `cylinder(p.r, p.height, ...)` | `halfcone(p.height, p.r_end, p.r_start, ...)` |

**重要前提**：PhrozenOrca 的 `support_pillar_widening_factor = 0.0`（intentional 預設），
代表 r_start == r_end 始終成立，漸縮量為零。
→ 本次修改視覺結果**完全不變**，但 struct 結構正確對齊 PrusaSlicer。

---

## 修改內容

### 修改 1：SupportTreeBuilder.hpp — Pillar struct

**欄位變更**（line 129）：

```diff
-    double height, r;
+    double height, r_start, r_end;
```

**Constructor 重構**（lines 142-156）：

```diff
-    Pillar(const Vec3d &endp, double h, double radius = 1.):
-        height{h}, r(radius), endpt(endp), starts_from_head(false) {}
+    // 完整建構子（兩端不同半徑）
+    Pillar(const Vec3d &endp, double h, double start_radius, double end_radius)
+        : height{h}
+        , r_start(start_radius)
+        , r_end(end_radius)
+        , endpt(endp)
+        , starts_from_head(false)
+    {}
+
+    // Convenience constructor：等半徑（保留所有現有呼叫點不變）
+    Pillar(const Vec3d &endp, double h, double start_radius = 1.)
+        : Pillar(endp, h, start_radius, start_radius)
+    {}
```

**設計說明**：Convenience constructor 確保所有現有的
`m_pillars.emplace_back(hjp, length, head.r_back_mm)` 呼叫點**無需任何修改**。

---

### 修改 2：SupportTreeBuilder.cpp — add_pillar_base()

**位置**：line 119

```diff
     m_pedestals.emplace_back(pll.endpt, std::min(baseheight, pll.height),
-                             std::max(radius, pll.r), pll.r);
+                             std::max(radius, pll.r_start), pll.r_start);
```

---

### 修改 3：SupportTreeBuildsteps.cpp — 全部 15 處

所有 `pillar.r` / `pillar().r` / `neighborpillar.r` → 對應 `r_start` 版本：

| 行號 | 修改前 | 修改後 |
|------|--------|--------|
| 359 | `pillar.r` | `pillar.r_start` |
| 361 | `pillar.r` | `pillar.r_start` |
| 371 | `pillar.r` | `pillar.r_start` |
| 373 | `pillar.r` | `pillar.r_start` |
| 1023 | `m_builder.pillar(nearest_id).r` | `.r_start` |
| 1100 | `pillar.r` | `pillar.r_start` |
| 1128 | `neighborpillar.r < pillar.r` | `neighborpillar.r_start < pillar.r_start` |
| 1214 | `pillar().r` | `pillar().r_start` |
| 1215 | `pillar().r` | `pillar().r_start` |
| 1237 | `pillar().r`（Pillar 建構） | `pillar().r_start` |
| 1246 | `pillar().r` | `pillar().r_start` |
| 1248 | `pillar().r` | `pillar().r_start` |
| 1250 | `pillar().r` | `pillar().r_start` |
| 1252 | `pillar().r` | `pillar().r_start` |
| 1253 | `pillar().r` | `pillar().r_start` |

**驗證**：`grep pillar[()]*\.r[^_]` → 無殘留舊式 `pillar.r` 引用。

---

### 修改 4：SupportTreeMesher.hpp — get_mesh(Pillar)

**位置**：line ~70

```diff
 inline indexed_triangle_set get_mesh(const Pillar &p, size_t steps)
 {
     if(p.height > EPSILON) {
-        return cylinder(p.r, p.height, steps, p.endpoint());
+        return halfcone(p.height, p.r_end, p.r_start, p.endpt, steps);
     }
     return {};
 }
```

**halfcone 參數說明**：
- `r_bottom = p.r_end`（底部落地端）
- `r_top = p.r_start`（頂部接 head 端）
- widening=0 時 r_start==r_end → halfcone 退化為等半徑圓柱，視覺完全相同

---

## 修改統計

| 步驟 | 修改說明 | 修改檔案 |
|------|---------|---------|
| C1 | Pillar struct 欄位 + constructor 重構 | `SupportTreeBuilder.hpp` |
| C2 | add_pillar_base() r → r_start | `SupportTreeBuilder.cpp` |
| C3 | 15 處 pillar.r → pillar.r_start | `SupportTreeBuildsteps.cpp` |
| C4 | cylinder → halfcone | `SupportTreeMesher.hpp` |
| **合計** | 4 個檔案 | — |

---

## FDM 安全性確認

| 修改 | FDM 影響 |
|------|:--------:|
| SupportTreeBuilder.hpp Pillar struct | ✅ SLA-only struct，FDM 不使用 |
| SupportTreeBuilder.cpp add_pillar_base | ✅ SLA support tree builder，FDM 不執行 |
| SupportTreeBuildsteps.cpp 15 處替換 | ✅ SLA-only buildsteps |
| SupportTreeMesher.hpp halfcone | ✅ SLA mesh 生成，FDM 不執行 |

---

## 驗證結果

- ✅ 編譯通過（無錯誤）
- ✅ SLA 切片執行成功
- ✅ 支撐網格視覺與修前相同（widening_factor=0.0 → r_start==r_end）
- ✅ `grep pillar[()]*\.r[^_]` 確認無殘留舊式引用

---

## 未來擴展

若需啟用漸縮柱效果：
1. 將 `support_pillar_widening_factor` 設為 > 0（例如 PrusaSlicer 預設 0.5）
2. 在 `add_pillar()` 中計算 `r_end = r_start * (1 + widening_factor * height)`
3. halfcone mesh 將自動產生底部增粗效果

---

## Git Commit

```
[Phase B] Step C: SLA Pillar 漸縮柱結構對齊 PrusaSlicer (r_start/r_end)
```
