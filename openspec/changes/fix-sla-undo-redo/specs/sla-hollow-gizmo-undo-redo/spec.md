## ADDED Requirements

### Requirement: Hollow gizmo pending parameters are preserved across undo/redo

GLGizmoHollow 的 `on_save` SHALL 將 `m_pending_offset`、`m_pending_quality`、`m_pending_closing_d`、`m_enable_hollowing` 序列化至 cereal BinaryOutputArchive，`on_load` SHALL 從 archive 還原這四個欄位。序列化欄位數與型別順序 SHALL 與原始 stub 保持一致（4 個欄位），以維持 cereal stream 相容性。

#### Scenario: Undo after pressing Hollow restores previous pending parameters

- **WHEN** 使用者在 Hollow gizmo 中設定 offset=3.0、按下 Hollow 按鈕（建立 snapshot A）
- **WHEN** 接著設定 offset=5.0、再次按下 Hollow 按鈕（建立 snapshot B）
- **WHEN** 使用者在 Hollow gizmo 內執行 undo
- **THEN** `m_pending_offset` SHALL 還原為 3.0（snapshot A 的值）
- **THEN** `m_enable_hollowing` SHALL 還原為 snapshot A 時的狀態

#### Scenario: Undo in prepare state after leaving Hollow gizmo works correctly

- **WHEN** 使用者在 Hollow gizmo 中按下 Hollow 按鈕後離開 gizmo（回到 prepare state）
- **WHEN** 使用者執行 undo
- **THEN** ModelObject::config 的 `hollowing_min_thickness` SHALL 還原為按下 Hollow 前的值
- **THEN** SLA 後台 SHALL 重新計算以反映還原後的 config

### Requirement: Hollow gizmo on_save/on_load serialization contract is verifiable without GUI

`GLGizmoHollow::on_save` 與 `on_load` 的欄位格式 SHALL 與以下 stub struct 完全一致，使序列化合約可在 headless 測試環境（無 wxWidgets / OpenGL）下被驗證：

```cpp
// tests/slic3rutils/test_sla_gizmo_serialization.cpp
struct HollowSerialState {
    float pending_offset    = 0.f;  // maps to m_pending_offset
    float pending_quality   = 0.f;  // maps to m_pending_quality
    float pending_closing_d = 0.f;  // maps to m_pending_closing_d
    bool  enable_hollowing  = true; // maps to m_enable_hollowing
    template<class Archive>
    void save(Archive& ar) const { ar(pending_offset, pending_quality, pending_closing_d, enable_hollowing); }
    template<class Archive>
    void load(Archive& ar) { ar(pending_offset, pending_quality, pending_closing_d, enable_hollowing); }
};
```

#### Scenario: HollowSerialState round-trips non-default values correctly

- **WHEN** `HollowSerialState` 以 `{offset=5.0, quality=0.8, closing_d=1.5, enable=false}` 序列化至 BinaryOutputArchive
- **WHEN** 從同一 archive 反序列化至新的 `HollowSerialState`
- **THEN** 還原後的 `pending_offset` SHALL 等於 5.0
- **THEN** 還原後的 `pending_quality` SHALL 等於 0.8
- **THEN** 還原後的 `pending_closing_d` SHALL 等於 1.5
- **THEN** 還原後的 `enable_hollowing` SHALL 等於 false

#### Scenario: Serialization stream position is preserved with exactly 4 fields

- **WHEN** `HollowSerialState::save` 寫入 archive
- **WHEN** `HollowSerialState::load` 從同一 archive 讀取
- **THEN** archive 的讀取位置 SHALL 在 load 後完全消耗（無殘餘位元組）
- **THEN** REQUIRE_NOTHROW 確認 cereal 無拋出 exception

### Requirement: Hollow gizmo pending owner is safely invalidated on undo

`on_load` 執行完畢時，`m_pending_owner` SHALL 被設為 `nullptr`，以強制 `data_changed()` 在下次呼叫時從還原後的 ModelObject config 重新初始化 pending 參數，避免使用已失效的 ModelObject 指標。

#### Scenario: Re-entering Hollow gizmo after undo does not use stale pointer

- **WHEN** 使用者進入 Hollow gizmo（`m_pending_owner` 指向 ModelObject A）
- **WHEN** 使用者執行 undo（ModelObject A 被 undo stack 替換為 ModelObject B）
- **WHEN** GLGizmosManager 呼叫 `data_changed()` 傳入 ModelObject B 的指標
- **THEN** `m_pending_owner` SHALL 為 nullptr（由 on_load 重置）
- **THEN** `data_changed()` 中 `mo != m_pending_owner` 條件 SHALL 成立
- **THEN** pending 參數 SHALL 從 ModelObject B 的 config 重新讀取
- **THEN** `m_pending_owner` SHALL 更新為 ModelObject B 的指標
