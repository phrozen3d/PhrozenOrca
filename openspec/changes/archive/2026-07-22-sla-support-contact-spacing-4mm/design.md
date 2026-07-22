## Context

SLA 自動支撐的接觸點間距全部源自 `SampleConfigFactory::create(float head_diameter)`
([SampleConfigFactory.cpp:55-98](../../../src/libslic3r/SLA/SupportIslands/SampleConfigFactory.cpp#L55))。
該函式依 Prusa 的支撐頭面積物理方程式,先算出「單點島最長路徑」
`max_length_for_one_support_point`(下稱 **L1**),再由 L1 級聯推導其餘所有間距欄位。

以預設頭徑 0.4mm 計算,`thin_max_distance ≈ 5.19mm`,對應密度 100%(`apply_density`
在 density=1 時原樣回傳)。需求是把此基準校準到 ~4mm。

關鍵結構觀察(決定了最乾淨的注入點):

```
create() 內的欄位依賴圖
  head_diameter ─┬─▶ head_radius                       (物理,獨立)
                 │      └─▶ minimal_distance_from_outline (= head_radius,物理)
                 └─▶ head_area ─▶ L1 = head_area·2.9 + 1.3   ← 唯一含加法常數的根
                                    │
                                    ├─▶ L2 = L1·3.9
                                    │     ├─▶ thin_max_distance      = L2·0.8
                                    │     ├─▶ thick_inner_max_distance = L2
                                    │     ├─▶ thick_outline_max_distance = L2·0.75
                                    │     └─▶ max_align_distance      = L2/2
                                    ├─▶ thin_max_width  = L1·2.5
                                    └─▶ thick_min_width = L1·2.15
  thin_max_distance ─▶ maximal_distance_from_outline = thin/3
                    └─▶ min_part_length              = thin
```

所有「間距/幾何長度」欄位都是 L1 的純倍數;`head_radius` 與
`minimal_distance_from_outline` 則自 head_diameter 獨立算出,**不經過 L1**。

## Goals / Non-Goals

**Goals:**
- 讓 `create(0.4mm).thin_max_distance` 由 ~5.19mm 校準為 ~4.0mm(密度 100%)。
- 整條幾何鏈自相似縮放,保持既有 thin:inner:outline 及所有下游比例不變。
- 頭部物理欄位(`head_radius`、`minimal_distance_from_outline`)維持真實尺寸不縮。
- 保留頭徑連動:4mm 僅錨在 0.4mm 參考頭,其他頭徑等比例。
- 單一真相源:切片路徑、LCD overhang gizmo、預設成員三條路徑一致。
- 係數以具名常數反推,對未來 Prusa 常數(2.9/1.3/3.9/0.8)調整具韌性。

**Non-Goals:**
- 不改 `apply_density()`,density 語意維持「100% = 新基準、200% ≈ 再除 2」。
- 不改任何 UI(密度滑桿仍 50–200%、tooltip、參數模型)。
- 不新增 mm 直接輸入或即時間距顯示(屬另一獨立 change)。
- 不改頭徑脫鉤或非均勻(√律)縮放。

## Decisions

### D1. 注入點:在 L1 的計算處乘上係數 k(而非事後乘 10 個欄位)
因為所有間距欄位都是 L1 的倍數,只要在 [line 66](../../../src/libslic3r/SLA/SupportIslands/SampleConfigFactory.cpp#L66)
把 `k` 乘進 L1,整條鏈自動連動;`head_radius`、`minimal_distance_from_outline`
不經 L1,天然不受影響。這比「逐一縮放間距欄位」更少出錯、更符合「單一根」語意。

建議形式(k 套在 scale_ 之前,避免二次取整):
```cpp
result.max_length_for_one_support_point =
    static_cast<coord_t>(scale_((head_area * 2.9 + 1.3) * k));
```

- **考慮過的替代**:(a) 事後對所有間距欄位逐一乘 k——正確但冗長且易漏欄位;
  (b) 改基礎常數 2.9/1.3/3.9/0.8——摧毀 Prusa 物理方程式可追溯性,且 1.3 是加法項難均勻;
  (c) 折進 `apply_density` 把 100% 基準設為 density 1.298——會漏掉 LCD gizmo、
  破壞「100%=恆等」語意,且 inner 走 √律不等比。皆劣於 D1。

### D2. 具名常數反推,不硬寫 0.770
k 不寫死魔數,而由「參考頭徑下、未縮放的 thin_max_distance」反推:
```cpp
constexpr double kRefHeadDiameter   = 0.4; // mm,校準基準頭徑
constexpr double kTargetThinSpacing = 4.0; // mm,基準頭徑下目標細長區間距

// 以與 create() 相同公式算出「未縮放」的 thin_max_distance@ref:
//   L1_ref   = (π·(kRefHeadDiameter/2)² )·2.9 + 1.3
//   thin_ref = L1_ref · 3.9 · 0.8
// k = kTargetThinSpacing / thin_ref   ≈ 4.0 / 5.193 ≈ 0.7702
```
建議以一個小 helper / constexpr 於編譯期算出 `thin_ref`(而非填入常數 5.193),
如此若日後有人調整 2.9/1.3/3.9/0.8,k 會自動維持「4mm@0.4mm 頭」的不變式。

### D3. 全系統單一真相源
k 注入在 `create()` 唯一源頭,以下三條路徑自動一致,無需個別改動:
- 生產切片:[SLAPrintSteps.cpp:895](../../../src/libslic3r/SLAPrintSteps.cpp#L895)
  `create()` → `apply_density()`,density 正常疊加於新基準。
- LCD overhang 手動偵測:[GLGizmoLcdOverhangDetection.cpp:1515](../../../src/slic3r/GUI/Gizmos/GLGizmoLcdOverhangDetection.cpp#L1515)
  直接呼叫 `create()`。
- `create_default_island_configuration()` 預設成員:同源。

### D4. `verify()` 安全性以代數證明,而非僅端點抽測
在「間距欄位縮 k、頭部欄位不縮」下,`verify()` 六條不等式對整個頭徑域恆成立
(兩條混合下限檢查的判別式皆為負):
- ④ `L1' ≥ 4·hr`:`1.754d² − 2d + 1.001 ≥ 0`,判別式 −3.02 < 0。
- ⑥ `thin_max_width' ≥ 4·hr`:`4.385d² − 2d + 2.503 ≥ 0`,判別式 −39.9 < 0。
- 其餘四條為上限檢查,縮小欄位僅出現在寬鬆側,恆過。

## Risks / Trade-offs

- **[頭徑非 0.4mm 時實測間距不是 4mm]** → 這是 D-路線 α 的預期行為(頭越大越疏);
  以文件/註解說明「4mm 是 0.4mm 參考頭下的名目值」,避免使用者誤解。
- **[coord_t 取整誤差]** → k 套在 `scale_()` 之前對 double 運算,只取整一次;
  ~4mm 容差以 ±數 µm 計,測試用近似比較(如 `Approx`)而非精確等值。
- **[LCD overhang gizmo 行為改變]** → 屬預期的「一致性」需求(proposal 已確認 Path 2 必須同步);
  非回歸,但需在手動驗證清單納入該工具的目視檢查。
- **[既有 tests/ 無回歸網]** → 新增測試鎖定 `thin(0.4,100%)≈4mm` 與關鍵
  `verify()` 不等式(0.2/0.4/0.8mm),防止未來常數漂移悄悄破壞不變式。
- **[測試置放位置]** → 測試置於 `tests/libslic3r/sla_contact_spacing_tests.cpp`
  併入 `libslic3r_tests` target,**非** `tests/sla_print/`。因 `tests/sla_print` 目錄
  已於 `tests/CMakeLists.txt:34` 被停用(既有 `sla_print_tests.cpp` 對 `RasterBase`
  API 漂移編譯失敗)。本測試僅依賴 `SampleConfigFactory`(libslic3r),置於已啟用的
  libslic3r 測試可實際建置且不連累其他建置。此為建置基礎設施現況的結果,非本變更引入。
- **[與 density 疊加的連續性]** → `apply_density` 未改,200% 應約再減半(~2mm);
  手動驗證納入 density 疊加檢查以確認行為連續。

## Migration Plan

- 無資料/設定遷移:不改參數 schema、不改序列化、不改 UI;既有專案檔與預設照常載入,
  density% 數值語意不變(僅「100% 對應的實際間距」由 ~5.19mm 變 ~4mm)。
- **Rollback**:單點改動,回退 = 移除 k(或設 `kTargetThinSpacing` 等於未縮放值)即回復原行為。

## Open Questions

- k / `thin_ref` 的實作表述:採「編譯期 helper 計算 thin_ref」還是「以少量 constexpr
  就地展開公式」——兩者等價,屬實作細節,將於 tasks 階段擇一(傾向前者,韌性較佳)。
- 目標 4mm 是否需可由設定或 profile 覆寫:本次範圍為固定基準(硬編常數);
  若日後需 per-profile 覆寫,另開 change。
