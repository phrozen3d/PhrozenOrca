#include "PhrozenWebcamSettingsPopup.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"

namespace Slic3r { namespace GUI {

// ---- 版面常數 ----
static constexpr int kPaddingOuter  = 12; // 左右外邊距 (DIP)
static constexpr int kPaddingRow    =  8; // 行間距     (DIP)
static constexpr int kRotBtnW       = 46; // 旋轉按鈕寬 (DIP)
static constexpr int kRotBtnH       = 24; // 旋轉按鈕高 (DIP)
static constexpr int kRotBtnGap     =  4; // 旋轉按鈕間距 (DIP)

static const wxColour kBgColour(45, 45, 48);
static const wxColour kTextColour(230, 230, 230);

PhrozenWebcamSettingsPopup::PhrozenWebcamSettingsPopup(wxWindow* parent)
    : wxPopupTransientWindow(parent, wxBORDER_SIMPLE)
{
    SetBackgroundColour(kBgColour);

    auto* panel = new wxPanel(this);
    panel->SetBackgroundColour(kBgColour);

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    // ---- 水平翻轉 ----
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* lbl = new wxStaticText(panel, wxID_ANY, _L("Flip Horizontal"));
        lbl->SetForegroundColour(kTextColour);
        m_flip_h_btn = new SwitchButton(panel);
        row->Add(lbl,          1, wxALIGN_CENTER_VERTICAL);
        row->Add(m_flip_h_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        main_sizer->AddSpacer(FromDIP(kPaddingRow));
        main_sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(kPaddingOuter));
    }

    // ---- 垂直翻轉 ----
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* lbl = new wxStaticText(panel, wxID_ANY, _L("Flip Vertical"));
        lbl->SetForegroundColour(kTextColour);
        m_flip_v_btn = new SwitchButton(panel);
        row->Add(lbl,          1, wxALIGN_CENTER_VERTICAL);
        row->Add(m_flip_v_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        main_sizer->AddSpacer(FromDIP(kPaddingRow));
        main_sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(kPaddingOuter));
    }

    // ---- 旋轉 ----
    {
        auto* lbl = new wxStaticText(panel, wxID_ANY, _L("Rotation"));
        lbl->SetForegroundColour(kTextColour);
        main_sizer->AddSpacer(FromDIP(kPaddingRow));
        main_sizer->Add(lbl, 0, wxLEFT | wxRIGHT, FromDIP(kPaddingOuter));
        main_sizer->AddSpacer(FromDIP(4));

        auto* rot_sizer = new wxBoxSizer(wxHORIZONTAL);
        const int rot_values[4]     = {0, 90, 180, 270};
        const wxString rot_labels[4] = {"0\u00B0", "90\u00B0", "180\u00B0", "270\u00B0"};
        for (int i = 0; i < 4; i++) {
            m_rot_btns[i] = new wxToggleButton(
                panel, wxID_ANY, rot_labels[i],
                wxDefaultPosition, FromDIP(wxSize(kRotBtnW, kRotBtnH)));
            const int deg = rot_values[i];
            m_rot_btns[i]->Bind(wxEVT_TOGGLEBUTTON, [this, deg](wxCommandEvent&) {
                on_rotation_changed(deg);
            });
            rot_sizer->Add(m_rot_btns[i], 0, i == 0 ? 0 : wxLEFT, FromDIP(kRotBtnGap));
        }
        main_sizer->Add(rot_sizer, 0, wxLEFT | wxRIGHT, FromDIP(kPaddingOuter));
    }

    main_sizer->AddSpacer(FromDIP(kPaddingRow));

    panel->SetSizer(main_sizer);

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(panel, 1, wxEXPAND);
    SetSizer(outer);

    // 預設 0° 選中
    m_rot_btns[0]->SetValue(true);

    // 綁定 flip 事件
    m_flip_h_btn->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { on_flip_changed(); });
    m_flip_v_btn->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { on_flip_changed(); });

    Layout();
    Fit();
}

void PhrozenWebcamSettingsPopup::SyncFromConfig(const PhrozenWebcamDisplayConfig& cfg)
{
    if (m_flip_h_btn) m_flip_h_btn->SetValue(cfg.flip_horizontal);
    if (m_flip_v_btn) m_flip_v_btn->SetValue(cfg.flip_vertical);

    const int rot_values[4] = {0, 90, 180, 270};
    for (int i = 0; i < 4; i++) {
        if (m_rot_btns[i])
            m_rot_btns[i]->SetValue(cfg.rotation_deg == rot_values[i]);
    }
    // 若 rotation_deg 不在合法值內，預設選 0°
    bool any = false;
    for (int i = 0; i < 4; i++) any |= (m_rot_btns[i] && m_rot_btns[i]->GetValue());
    if (!any && m_rot_btns[0]) m_rot_btns[0]->SetValue(true);
}

PhrozenWebcamDisplayConfig PhrozenWebcamSettingsPopup::GetCurrentConfig() const
{
    PhrozenWebcamDisplayConfig cfg;
    cfg.flip_horizontal = m_flip_h_btn && m_flip_h_btn->GetValue();
    cfg.flip_vertical   = m_flip_v_btn && m_flip_v_btn->GetValue();
    const int rot_values[4] = {0, 90, 180, 270};
    for (int i = 0; i < 4; i++) {
        if (m_rot_btns[i] && m_rot_btns[i]->GetValue()) {
            cfg.rotation_deg = rot_values[i];
            break;
        }
    }
    return cfg;
}

void PhrozenWebcamSettingsPopup::on_flip_changed()
{
    if (OnConfigChanged) OnConfigChanged(GetCurrentConfig());
}

void PhrozenWebcamSettingsPopup::on_rotation_changed(int deg)
{
    // 互斥：只有一個旋轉按鈕可以選中
    const int rot_values[4] = {0, 90, 180, 270};
    for (int i = 0; i < 4; i++) {
        if (m_rot_btns[i])
            m_rot_btns[i]->SetValue(rot_values[i] == deg);
    }
    if (OnConfigChanged) OnConfigChanged(GetCurrentConfig());
}

}} // namespace Slic3r::GUI
