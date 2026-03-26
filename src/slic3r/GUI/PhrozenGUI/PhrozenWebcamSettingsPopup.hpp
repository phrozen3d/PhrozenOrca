#ifndef slic3r_GUI_PhrozenWebcamSettingsPopup_hpp_
#define slic3r_GUI_PhrozenWebcamSettingsPopup_hpp_

#include <wx/popupwin.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>
#include <functional>
#include "slic3r/Utils/Phrozen/PhrozenMachineDatas.hpp"
#include "../Widgets/SwitchButton.hpp"

namespace Slic3r { namespace GUI {

/// 非獨佔的 webcam 顯示設定浮動視窗（點視窗外自動關閉）
/// 提供水平翻轉、垂直翻轉、旋轉（0/90/180/270°）三組控制
class PhrozenWebcamSettingsPopup : public wxPopupTransientWindow
{
public:
    explicit PhrozenWebcamSettingsPopup(wxWindow* parent);

    /// 以外部設定值更新所有 control 的顯示狀態
    void SyncFromConfig(const PhrozenWebcamDisplayConfig& cfg);

    /// 任一 control 切換後呼叫；在 UI thread 執行
    std::function<void(const PhrozenWebcamDisplayConfig&)> OnConfigChanged;

private:
    SwitchButton*   m_flip_h_btn{nullptr};  // 水平翻轉
    SwitchButton*   m_flip_v_btn{nullptr};  // 垂直翻轉
    wxToggleButton* m_rot_btns[4]{};        // 0° / 90° / 180° / 270°（互斥）

    void on_flip_changed();
    void on_rotation_changed(int deg);
    PhrozenWebcamDisplayConfig GetCurrentConfig() const;
};

}} // namespace Slic3r::GUI
#endif // slic3r_GUI_PhrozenWebcamSettingsPopup_hpp_
