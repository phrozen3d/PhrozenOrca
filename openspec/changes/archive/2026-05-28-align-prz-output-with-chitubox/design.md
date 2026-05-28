## Context

本變更聚焦於 PhrozenOrca 切片產出 `.prz` header 的數值單位與 Phrozen SLA Resin profile 預設值對齊 Chitubox 的事實標竿。motivation 已於 [proposal.md](proposal.md) 闡述；本文件針對「**HOW**」進行三個維度的技術定案：

1. PRZ writer 中 Volume 單位由 ml → mm³ 的最小擾動實作路徑
2. Speed 欄位（lift/retract 系列）的單位 audit 範圍與結論
3. Weight 計算公式在 Volume 變數單位變動下的代碼結構
4. resources/profiles 預設值對齊範圍與檔案清單

實作上的關鍵約束：
- **不修改 `SLAPrintStatistics`**：該結構體儲存的 `objects_used_material` / `support_used_material` 永遠是 mm³，UI 顯示路徑（`SLASlice2DCanvas.cpp:317`）會自行除以 1000 轉成 ml 顯示。PRZ writer 與 UI 顯示**讀同一個 mm³ 來源、各自做自己的單位轉換**，天然解耦。
- **不修改 [PrintConfig.cpp](src/libslic3r/PrintConfig.cpp) 的 global default**：避免影響非 Phrozen SLA 使用者。
- **不引入新 config key**：所有對齊均透過修改既有 JSON `default_value` / `values` 完成。

## Goals / Non-Goals

**Goals:**

- PRZ header 的 `TotalVolume` 欄位以 mm³（int 量級數值）寫入，與 Chitubox 一致。
- PRZ header 的 `TotalWeight` 欄位以克（grams）寫入，採密度公式（選項 X）。
- PRZ header 中所有 Speed 系列欄位確認以 mm/min 寫入（audit + 驗證，預期不需程式碼修改）。
- Phrozen SLA process / filament / machine JSON 預設值對齊 Chitubox 範例數值。
- UI 顯示行為（「Total resin: X.XXX ml」）完全不變。

**Non-Goals:**

- **不處理** volume 數值上的微小差距（1.148 vs 1.0038 之類）— 此源於切片演算法差異與支撐生成差異，非本變更範圍。
- **不處理** Phrozen 印表機韌體實際讀取 .prz 後的顯示行為與相容性（依使用者指示排除在外）。
- **不處理** 識別字串類欄位（`software`、`softwareVersion`、`fileTime`、`printernType`、`profileName`）— 使用者明確指示不修改，保留 PhrozenOrca 自身的識別字串作為可追溯性優點。
- **不處理** 既有客製化 user preset 的強制 migration — 沿用 Preset 系統既定行為（user preset 之 `inherits` 鏈結保留覆寫值）。
- **不新增** UI 控件或對話框；本變更不觸碰 [src/slic3r/GUI/](src/slic3r/GUI/) 任何檔案。
- **不修改** [PrintConfig.cpp](src/libslic3r/PrintConfig.cpp) 之 hardcoded default。

## Decisions

### Decision 1: Volume 單位轉換僅於 PRZ writer 單點生效

**選擇**：在 [PhrozenPRZ.cpp:529](src/libslic3r/Format/PhrozenPRZ.cpp#L529) 移除 `/ 1000.0`，直接以 mm³ 寫入 PRZ。

**Rationale**：
- 既有架構已天然解耦 — `SLAPrintStatistics` 儲存 mm³，UI 與 PRZ writer 各自轉換。
- 修改影響面最小（單點修改、不影響其他模組）。
- 不需要新增 override 機制、分流邏輯、雙來源變數，避免架構複雜化。

**Alternatives considered**：
- **A. 直接修改 `SLAPrintStatistics` 為 ml**：會破壞 UI 顯示、需同步改 UI 端的轉換、且其他內部使用者（statistics report、cost 計算）皆需 audit — 改動面大、風險高。
- **B. 在 `SLAPrintStatistics` 新增 `volume_for_prz_mm3` 欄位**：引入冗餘欄位、需在 SLA pipeline 內額外維護、無實質好處（資料源完全等同 `objects_used_material + support_used_material`）。
- **C. 在 PRZ writer 內新增 helper 函式 `to_mm3()`**：對單一處的 `/ 1000.0` 移除而言過度設計。

### Decision 2: Speed 欄位單位 Audit — 確認來源已是 mm/min，不需轉換

**Audit 範圍**：[PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 內所有與速度相關的 `cfg_f(cfg, "...")` 讀取點。

**具體 config key 清單（共 8 個）**：

| Config Key | PRZ Header 欄位 | 對應寫入點 (PhrozenPRZ.cpp) | PrintConfig.cpp sidetext |
|---|---|---|---|
| `bottom_lift_speed` | `BottomLiftSpeed` | line 456 (header), line 617 (per-layer) | `"mm/min"` (line 6763) |
| `bottom_lift_second_speed` | `BottomLift_second_Speed` | line 488 (header), line 631 (per-layer) | `"mm/min"` (line 6771) |
| `lifting_speed` | `LiftSpeed` | line 460 (header), line 617 (per-layer) | `"mm/min"` (line 6779) |
| `lift_second_speed` | `Lift_second_Speed` | line 496 (header), line 632 (per-layer) | `"mm/min"` (line 6787) |
| `bottom_retract_speed` | `BottomRetractSpeed` | line 471 (header), line 653 (per-layer) | `"mm/min"` (line 6795) |
| `bottom_retract_second_speed` | `BottomRetract_second_Speed` | line 504 (header) | `"mm/min"` (line 6803) |
| `retract_speed` | `RetractSpeed` | line 482 (header), line 654 (per-layer) | `"mm/min"` |
| `retract_second_speed` | `Retract_second_Speed` | line 512 (header), line 669 (per-layer) | `"mm/min"` |

**Audit 結論（預期）**：所有 8 個 config key 在 [PrintConfig.cpp:6759-6803](src/libslic3r/PrintConfig.cpp#L6759-L6803) 之 `def->sidetext` 皆已標記為 `"mm/min"`，內部儲存與 UI 顯示一致；[PhrozenPRZ.cpp:100-102](src/libslic3r/Format/PhrozenPRZ.cpp#L100-L102) 的 `motion_s` lambda 亦明確以 `speed_mm_min` 命名並用 `dist_mm / speed_mm_min * 60.f` 計算秒數，再次佐證內部單位即 mm/min。

**Rationale**：
- 預期不需要程式碼修改，僅做驗證型 audit。
- 若 audit 過程意外發現某個 config key sidetext 是 `mm/s` 或 writer 有 `* 60` / `/ 60` 換算，補上對應修正（防禦性條款）。
- 將 audit 流程文件化於 tasks.md，避免日後若新增 speed 欄位時遺漏單位確認。

**Alternatives considered**：
- **無條件加 `* 60` / `/ 60`**：在已知內部單位是 mm/min 的前提下，盲目轉換會造成數值錯誤。
- **以 `def->sidetext` 字串於 runtime 自動轉換**：過度設計、增加 runtime 成本、無實質效益。

### Decision 3: Weight 公式 — 維持密度公式（選項 X），變數作用域分離

**選擇**：維持 [PhrozenPRZ.cpp:531-534](src/libslic3r/Format/PhrozenPRZ.cpp#L531-L534) 既有的密度推算邏輯，但將「PRZ 輸出的 volume 值」與「weight 計算中介的 volume 值」做變數命名與作用域分離。

**修改後的代碼結構（pseudo-code，實作於 tasks 階段）**：

```cpp
{
    const SLAPrintStatistics &stats = print.print_statistics();
    
    // 雙形態 volume：mm³ 用於 PRZ 寫入；ml 僅供 weight / price 公式內部使用
    const double total_mm3 = stats.objects_used_material + stats.support_used_material;
    const float  volume_mm3 = static_cast<float>(total_mm3);            // PRZ 寫入用
    const float  volume_ml  = static_cast<float>(total_mm3 / 1000.0);   // weight/price 計算用
    
    // weight (grams) = volume_ml × density
    // density (g/ml) = bottle_weight (g) / bottle_volume (ml)
    const float bw_g    = cfg_f(cfg, "bottle_weight") * 1000.f;   // kg → g
    const float bv_ml   = cfg_f(cfg, "bottle_volume");            // ml
    const float density = (bv_ml > 0.f) ? bw_g / bv_ml : 1.f;     // 退化預設 1.0 g/ml
    const float weight  = volume_ml * density;                    // grams
    
    // price ($) = volume_ml × bottle_cost / bv_ml，與 weight 同樣以 ml 為基準
    const float bottle_cost = cfg_f(cfg, "bottle_cost");
    const float price = (bv_ml > 0.f) ? (volume_ml * bottle_cost) / bv_ml : 0.f;
    
    write_be(fh, volume_mm3); layerContent_position_offset += 4;  // PRZ: mm³
    write_be(fh, weight);     layerContent_position_offset += 4;  // PRZ: grams
    write_be(fh, price);      layerContent_position_offset += 4;  // PRZ: USD
}
```

**對照既有代碼的差異**：
- 既有 `volume_ml` 單一變數，被 PRZ 寫入與 weight/price 公式共用。
- 修改後拆成 `volume_mm3`（寫入用）與 `volume_ml`（計算用）兩個 local 變數，作用域局限於該 block，不外洩。
- 註解明確標示各欄位的單位語意。

**Rationale**：
- Chitubox 範例 `volume: 1003.8` mm³ + `weight: 1.10418` g 兩者單位不同（密度 ~1.1 g/cm³），維持 density 公式直接對齊。
- 避免「weight 數值 = volume 數值」的選項 Y（會破壞既有正確邏輯、量級錯誤）。
- 若樹脂 profile 之 `bottle_weight` / `bottle_volume` 未填寫，`density` 退化為 1.0（與既有行為一致），weight ≈ volume_ml 數值，符合「沒有密度資訊時 weight ≈ volume_ml」的合理預設。

**Alternatives considered**：
- **選項 Y（weight 數值 = volume mm³ 數值）**：已於前置討論否決 — 量級錯誤、跟 Chitubox 不一致、需移除既有正確邏輯。
- **選項 Z（density 寫死 1.0、忽略 bottle config）**：忽略 profile 已填寫的密度資訊，喪失準確度；若使用者要切換樹脂材料則完全無法反映。

### Decision 4: 預設值對齊範圍限定於 PhrozenSLA profile JSON

**選擇**：僅修改 [resources/profiles/PhrozenSLA/](resources/profiles/PhrozenSLA/) 下相關 JSON 之 `default_value` / 既有覆寫欄位，**不修改 [PrintConfig.cpp](src/libslic3r/PrintConfig.cpp) 之 hardcoded default**。

**預計修改檔案清單**：

**Process（列印程序）profile：**
- [resources/profiles/PhrozenSLA/process/sla_print_common.json](resources/profiles/PhrozenSLA/process/sla_print_common.json) — 共用基礎
- [resources/profiles/PhrozenSLA/process/Speed Plus - Black@Phrozen Sonic Mega 8K S.json](resources/profiles/PhrozenSLA/process/Speed%20Plus%20-%20Black@Phrozen%20Sonic%20Mega%208K%20S.json)
- [resources/profiles/PhrozenSLA/process/Speed Plus - Black@Phrozen Sonic Mega 8K V2.json](resources/profiles/PhrozenSLA/process/Speed%20Plus%20-%20Black@Phrozen%20Sonic%20Mega%208K%20V2.json)
- [resources/profiles/PhrozenSLA/process/Speed Plus - Black@Phrozen Sonic Mighty Revo 16K.json](resources/profiles/PhrozenSLA/process/Speed%20Plus%20-%20Black@Phrozen%20Sonic%20Mighty%20Revo%2016K.json)
- [resources/profiles/PhrozenSLA/process/Tough ABS-like+@Phrozen Sonic Mega 8K S.json](resources/profiles/PhrozenSLA/process/Tough%20ABS-like+@Phrozen%20Sonic%20Mega%208K%20S.json)
- [resources/profiles/PhrozenSLA/process/Tough ABS-like+@Phrozen Sonic Mega 8K V2.json](resources/profiles/PhrozenSLA/process/Tough%20ABS-like+@Phrozen%20Sonic%20Mega%208K%20V2.json)
- [resources/profiles/PhrozenSLA/process/Tough ABS-like+@Phrozen Sonic Mighty Revo 16K.json](resources/profiles/PhrozenSLA/process/Tough%20ABS-like+@Phrozen%20Sonic%20Mighty%20Revo%2016K.json)

**Filament（樹脂）profile：**
- [resources/profiles/PhrozenSLA/filament/sla_material_common.json](resources/profiles/PhrozenSLA/filament/sla_material_common.json) — 共用基礎
- [resources/profiles/PhrozenSLA/filament/Phrozen Speed Plus - Black.json](resources/profiles/PhrozenSLA/filament/Phrozen%20Speed%20Plus%20-%20Black.json)
- [resources/profiles/PhrozenSLA/filament/Phrozen Tough ABS-like+.json](resources/profiles/PhrozenSLA/filament/Phrozen%20Tough%20ABS-like+.json)

**Machine（機型）profile（依需要）：**
- [resources/profiles/PhrozenSLA/machine/Mega 8K S.json](resources/profiles/PhrozenSLA/machine/Mega%208K%20S.json)
- [resources/profiles/PhrozenSLA/machine/Mega 8K V2.json](resources/profiles/PhrozenSLA/machine/Mega%208K%20V2.json)
- [resources/profiles/PhrozenSLA/machine/Mighty Revo 16K.json](resources/profiles/PhrozenSLA/machine/Mighty%20Revo%2016K.json)
- [resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mega 8K S.json](resources/profiles/PhrozenSLA/machine/Phrozen%20Sonic%20Mega%208K%20S.json)
- [resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mega 8K V2.json](resources/profiles/PhrozenSLA/machine/Phrozen%20Sonic%20Mega%208K%20V2.json)
- [resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mighty Revo 16K.json](resources/profiles/PhrozenSLA/machine/Phrozen%20Sonic%20Mighty%20Revo%2016K.json)
- [resources/profiles/PhrozenSLA/machine/Sonic Mega 8K S.json](resources/profiles/PhrozenSLA/machine/Sonic%20Mega%208K%20S.json)
- [resources/profiles/PhrozenSLA/machine/Sonic Mega 8K V2.json](resources/profiles/PhrozenSLA/machine/Sonic%20Mega%208K%20V2.json)

**預計對齊的欄位（依 diff 檔列出之可對應項目）**：

| Config Key | Chitubox 範例值 | 適用機型/Profile | 修改位置 |
|---|---|---|---|
| `bottom_exposure_time` | 35 (Revo: 35) | 所有 Phrozen | process |
| `bottom_layer_count` (= `bottomLayers`) | 8 | 所有 Phrozen | process |
| `bottom_lift_distance` | 5 (Revo) | Revo 系列 | process |
| `bottom_lift_speed` | 80 (Revo) / 45 (Mega 8K S) | 機型相依 | process |
| `lifting_distance` (= `liftDist`) | 4 (Revo) / 7 (Mega 8K S) | 機型相依 | process |
| `lifting_speed` | 130 (Revo) / 45 (Mega 8K S) | 機型相依 | process |
| `bottom_retract_distance` 衍生欄位 | 6 (Revo) | Revo 系列 | process |
| `bottom_retract_speed` | 160 (Revo) | Revo 系列 | process |
| `retract_speed` | 160 (Revo) | Revo 系列 | process |
| `bottom_lift_second_distance` (`bottomLift_second_Dist`) | 3 (Revo) | Revo 系列 | process |
| `lift_second_distance` (`lift_second_Dist`) | 2 (Revo) | Revo 系列 | process |
| `bottom_retract_second_distance` (`bottomRetract_second_Dist`) | 2 (Revo) | Revo 系列 | process |
| `retract_second_distance` (`retract_second_Dist`) | 2 (Revo) | Revo 系列 | process |
| `blur_level` | 2 | 所有 Phrozen | machine 或 process |
| `aa_level` | 8 (Revo) / 4 (Mega) | 機型相依 | machine 或 process |
| `bottle_weight` / `bottle_volume` | （驗證填寫合理密度值） | 所有 filament | filament |

**Rationale**：
- 影響面限定於 Phrozen 使用者，不影響其他 SLA 廠牌。
- JSON 修改後 PRZ writer 透過 `cfg_i` / `cfg_f` 自動讀取新預設值，**不需修改 writer 讀取邏輯**。
- 已客製化的 user preset（帶 `inherits`）保留覆寫值，符合 Preset 系統既定行為。
- 共用基礎 profile（`sla_print_common.json` / `sla_material_common.json`）為對齊的主要修改入口，個別機型/樹脂 profile 僅補上機型差異化欄位。

**Alternatives considered**：
- **修改 PrintConfig.cpp 之 hardcoded global default**：影響所有 SLA 廠牌的非 Phrozen 使用者，副作用過大。
- **僅修改 PRZ writer 內 hardcode override 數值**：完全繞過 profile 系統，使用者改 profile 無效，等於「假裝」對齊；違反 QA 主動對齊的本意。

## Risks / Trade-offs

- **[Risk] Phrozen 印表機韌體讀取 mm³ 與 ml 的解讀差異** → Mitigation：使用者明確指示本次不考量韌體顯示行為，視為可接受的對齊代價。後續若發現韌體相容性問題，於另一個變更處理（不在本變更範圍）。
- **[Risk] Audit 階段意外發現某個 Speed config key 是 `mm/s` 而非 `mm/min`** → Mitigation：tasks 階段預留「audit + 修正」雙條款；若需修正，於 PhrozenPRZ.cpp 該寫入點補上 `* 60` 換算（並於 commit 訊息明確記錄）。實作前提是「預期不需修改」，但保留防禦性條款。
- **[Risk] JSON 預設值修改後，未客製化使用者下一次開啟軟體預設值將「無聲」變動** → Mitigation：CHANGELOG 與 release note 明示「Phrozen SLA profile 預設值對齊 Chitubox」並列出主要欄位變動；user preset 之 `inherits` 鏈結保留覆寫值，主動客製化者不受影響。
- **[Risk] `volume_mm3` 寫入 `float` 後精度損失（如極大模型 > 16,777,216 mm³ ≈ 16.8 L 時 float23-bit 尾數無法完整表達）** → Mitigation：實務上 Phrozen 機型樹脂槽容量遠小於 16 L，float 精度足以表達；既有 PRZ format 該欄位定義為 4-byte BE float，不可改變寬度。若日後機型擴增至超大容量再評估。
- **[Trade-off] 不修改 PrintConfig.cpp global default** → 代價：未來其他 SLA 廠牌若也想對齊 Chitubox 風格，須各自於其 profile JSON 重複定義；本變更不解決廠牌外的對齊問題。

## Migration Plan

1. 依序修改 [PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) 內 volume 寫入單位（單一 block，line 526-541）。
2. 對 8 個 Speed config key 進行 audit；如預期無需修改即僅於 tasks.md 紀錄結果。
3. 修改 PhrozenSLA profile JSON 預設值（process / filament / machine）。
4. 重新切片同一 STL，以 ReadPrz 工具 dump header 驗證 volume / weight / speed 欄位之單位與量級對齊 Chitubox。
5. UI 回歸測試：「Total resin」仍顯示 ml、數值正確。
6. **Rollback strategy**：本變更全部為文字檔（C++ 單點修改 + JSON 預設值），單一 commit 可完整還原；無 schema migration、無資料破壞性變更。
