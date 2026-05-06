#include "LayerPrintTimeCompensationDialog.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>

#include "I18N.hpp"
#include "Widgets/SpinInput.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/DialogButtons.hpp"

namespace {

void set_tree_white_bg(wxWindow *w)
{
    if (!w)
        return;
    w->SetBackgroundStyle(wxBG_STYLE_SYSTEM);
    w->SetBackgroundColour(*wxWHITE);
    for (wxWindow *c : w->GetChildren())
        set_tree_white_bg(c);
}

} // namespace

namespace Slic3r::GUI {

LayerPrintTimeCompensationDialog::PersistedState LayerPrintTimeCompensationDialog::s_state{};

LayerPrintTimeCompensationDialog::LayerPrintTimeCompensationDialog(wxWindow *parent, double initial_layer_compensation_s)
    : wxDialog(parent, wxID_ANY, _L("Layer Print Time Compensation Settings"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);
    SetOwnBackgroundColour(*wxWHITE);
    SetBackgroundStyle(wxBG_STYLE_SYSTEM);

    m_result_layer_s = initial_layer_compensation_s;

    auto *root = new wxBoxSizer(wxVERTICAL);
    const int kDialogW    = FromDIP(724);
    const int kDialogH    = FromDIP(234);
    const int kLeftPad    = FromDIP(48);
    const int kRightPad   = FromDIP(48);
    const int kTopPad     = FromDIP(16);
    const int kBottomPad  = FromDIP(16);
    const int kFieldWidth = FromDIP(120);
    const int kFieldGap   = FromDIP(20);
    const int kLabelGap   = FromDIP(8);
    const int kContentW   = kDialogW - kLeftPad - kRightPad;
    const int kInputsW    = kFieldWidth * 3 + kFieldGap * 2;
    const int kLabelWidth = std::max(FromDIP(120), kContentW - kLabelGap - kInputsW);
    auto *content = new wxBoxSizer(wxVERTICAL);

    const StateColor text_c3c3c3(std::make_pair(0xC3C3C3, (int)StateColor::Disabled),
                                 std::make_pair(0xC3C3C3, (int)StateColor::Normal));

    auto make_spin = [this, &text_c3c3c3](int minv, int maxv, int initv, int width, const wxString &unit) -> SpinInput * {
        auto *sp = new SpinInput(this, wxString::Format("%d", initv), unit, wxDefaultPosition, wxSize(width, -1), 0, minv,
                                 maxv, initv, 1);
        sp->SetTextColor(text_c3c3c3);
        sp->SetLabelColor(text_c3c3c3);
        sp->SetBackgroundColour(*wxWHITE);
        return sp;
    };

    auto add_hms_row = [this, content, &make_spin, kFieldWidth, kFieldGap, kLabelWidth, kLabelGap](const wxString &title, SpinInput **h, SpinInput **m, SpinInput **s) {
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        auto *lbl = new wxStaticText(this, wxID_ANY, title);
        lbl->SetBackgroundStyle(wxBG_STYLE_SYSTEM);
        lbl->SetBackgroundColour(*wxWHITE);
        lbl->SetMinSize(wxSize(kLabelWidth, -1));
        row->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kLabelGap);
        *h = make_spin(0, 9999, 0, kFieldWidth, _L("h"));
        *m = make_spin(0, 59, 0, kFieldWidth, _L("m"));
        *s = make_spin(0, 59, 0, kFieldWidth, _L("s"));
        row->Add(*h, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kFieldGap);
        row->Add(*m, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kFieldGap);
        row->Add(*s, 0, wxALIGN_CENTER_VERTICAL);
        content->Add(row, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
        (*h)->Bind(wxEVT_SPINCTRL, &LayerPrintTimeCompensationDialog::on_spin, this);
        (*m)->Bind(wxEVT_SPINCTRL, &LayerPrintTimeCompensationDialog::on_spin, this);
        (*s)->Bind(wxEVT_SPINCTRL, &LayerPrintTimeCompensationDialog::on_spin, this);
    };

    add_hms_row(_L("Software predicts print time"), &m_pred_h, &m_pred_m, &m_pred_s);
    add_hms_row(_L("Actual print time"), &m_act_h, &m_act_m, &m_act_s);

    {
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        auto *lc_lbl = new wxStaticText(this, wxID_ANY, _L("Layer count"));
        lc_lbl->SetBackgroundStyle(wxBG_STYLE_SYSTEM);
        lc_lbl->SetBackgroundColour(*wxWHITE);
        lc_lbl->SetMinSize(wxSize(kLabelWidth, -1));
        row->Add(lc_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kLabelGap);
        m_layers = make_spin(0, 1000000, 0, kFieldWidth, wxEmptyString);
        row->Add(m_layers, 0, wxALIGN_CENTER_VERTICAL);
        content->Add(row, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
        m_layers->Bind(wxEVT_SPINCTRL, &LayerPrintTimeCompensationDialog::on_spin, this);
    }

    {
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        m_readout_title = new wxStaticText(this, wxID_ANY, _L("Layer print time compensation"));
        m_readout_title->SetBackgroundStyle(wxBG_STYLE_SYSTEM);
        m_readout_title->SetBackgroundColour(*wxWHITE);
        m_readout_title->SetForegroundColour(wxColour(195, 195, 195));
        m_readout_title->SetMinSize(wxSize(kLabelWidth, -1));
        row->Add(m_readout_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, kLabelGap);
        m_readout = new TextInput(this, wxString::FromDouble(initial_layer_compensation_s, 2), wxEmptyString, wxEmptyString,
                                  wxDefaultPosition, wxSize(kFieldWidth, -1), wxTE_READONLY | wxTE_RIGHT);
        m_readout->SetTextColor(text_c3c3c3);
        if (m_readout->GetTextCtrl())
            m_readout->GetTextCtrl()->SetEditable(false);
        row->Add(m_readout, 0, wxALIGN_CENTER_VERTICAL);
        auto *s_lbl = new wxStaticText(this, wxID_ANY, _L("s"));
        s_lbl->SetBackgroundStyle(wxBG_STYLE_SYSTEM);
        s_lbl->SetBackgroundColour(*wxWHITE);
        s_lbl->SetForegroundColour(wxColour(195, 195, 195));
        row->Add(s_lbl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
        content->Add(row, 0, wxEXPAND, 0);
    }

    auto *dlg_btns = new DialogButtons(this, {wxString("Apply"), wxString("Cancel")});
    dlg_btns->SetPrimaryButton(_L("Apply"));
    auto *btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_row->AddStretchSpacer(1);
    btn_row->Add(dlg_btns, 0, wxALIGN_CENTER_VERTICAL);

    const int kSidePad = kLeftPad; // 與 kRightPad 相同 (48 DIP)，左右對稱留白
    root->AddSpacer(kTopPad);
    root->Add(content, 0, wxEXPAND | wxLEFT | wxRIGHT, kSidePad);
    root->AddStretchSpacer(1);
    root->Add(btn_row, 0, wxEXPAND | wxLEFT | wxRIGHT, kSidePad);
    root->AddSpacer(kBottomPad);

    SetSizer(root);
    root->SetSizeHints(this);
    SetMinSize(wxSize(kDialogW, kDialogH));
    SetMaxSize(wxSize(kDialogW, kDialogH));
    SetClientSize(wxSize(kDialogW, kDialogH));

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &e) {
        save_ui_state();
        EndModal(wxID_CANCEL);
        e.Skip();
    });

    Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { on_apply(); }, wxID_APPLY);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        save_ui_state();
        EndModal(wxID_CANCEL);
    }, wxID_CANCEL);

    if (s_state.initialized) {
        m_pred_h->SetValue(s_state.pred_h);
        m_pred_m->SetValue(s_state.pred_m);
        m_pred_s->SetValue(s_state.pred_s);
        m_act_h->SetValue(s_state.act_h);
        m_act_m->SetValue(s_state.act_m);
        m_act_s->SetValue(s_state.act_s);
        m_layers->SetValue(s_state.layers);
    }

    recalc();
    set_tree_white_bg(this);
    CentreOnScreen(wxBOTH);
}

void LayerPrintTimeCompensationDialog::recalc()
{
    const double pred = double(m_pred_h->GetValue()) * 3600.0 + double(m_pred_m->GetValue()) * 60.0 + double(m_pred_s->GetValue());
    const double act  = double(m_act_h->GetValue()) * 3600.0 + double(m_act_m->GetValue()) * 60.0 + double(m_act_s->GetValue());
    const int    N    = m_layers->GetValue();
    double       per_layer = 0.;
    if (N > 0)
        per_layer = (act - pred) / double(N);
    m_result_layer_s = per_layer;
    if (m_readout && m_readout->GetTextCtrl()) {
        wxTextCtrl *tc = m_readout->GetTextCtrl();
        tc->SetValue(wxString::Format("%.2f", per_layer));
        tc->SetForegroundColour(wxColour(195, 195, 195));
    }
}

void LayerPrintTimeCompensationDialog::on_spin(wxCommandEvent &evt)
{
    recalc();
    evt.Skip();
}

void LayerPrintTimeCompensationDialog::on_apply()
{
    recalc();
    save_ui_state();
    EndModal(wxID_OK);
}

void LayerPrintTimeCompensationDialog::save_ui_state()
{
    if (!m_pred_h || !m_pred_m || !m_pred_s || !m_act_h || !m_act_m || !m_act_s || !m_layers)
        return;
    s_state.pred_h = m_pred_h->GetValue();
    s_state.pred_m = m_pred_m->GetValue();
    s_state.pred_s = m_pred_s->GetValue();
    s_state.act_h  = m_act_h->GetValue();
    s_state.act_m  = m_act_m->GetValue();
    s_state.act_s  = m_act_s->GetValue();
    s_state.layers = m_layers->GetValue();
    s_state.initialized = true;
}

} // namespace Slic3r::GUI
