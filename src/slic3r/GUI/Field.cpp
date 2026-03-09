#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Field.hpp"
#include "libslic3r/GCode/Thumbnails.hpp"
#include "wxExtensions.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"

#include "libslic3r/PrintConfig.hpp"

#include <regex>
#include <wx/numformatter.h>
#include <wx/tooltip.h>
#include <wx/notebook.h>
#include <wx/listbook.h>
#include <wx/tokenzr.h>
#include <boost/algorithm/string/predicate.hpp>
#include "OG_CustomCtrl.hpp"
#include "MsgDialog.hpp"
#include "BitmapComboBox.hpp"

// BBS
#include "Notebook.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/SpinInput.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextCtrl.h"

#include "../Utils/ColorSpaceConvert.hpp"
#ifdef __WXOSX__
#define wxOSX true
#else
#define wxOSX false
#endif

namespace Slic3r { namespace GUI {

wxString double_to_string(double const value, const int max_precision /*= 4*/)
{
// Style_NoTrailingZeroes does not work on OSX. It also does not work correctly with some locales on Windows.
//	return wxNumberFormatter::ToString(value, max_precision, wxNumberFormatter::Style_NoTrailingZeroes);

	wxString s = wxNumberFormatter::ToString(value, max_precision, wxNumberFormatter::Style_None);

	// The following code comes from wxNumberFormatter::RemoveTrailingZeroes(wxString& s)
	// with the exception that here one sets the decimal separator explicitely to dot.
    // If number is in scientific format, trailing zeroes belong to the exponent and cannot be removed.
    if (s.find_first_of("eE") == wxString::npos) {
        char dec_sep = is_decimal_separator_point() ? '.' : ',';
        const size_t posDecSep = s.find(dec_sep);
	    // No decimal point => removing trailing zeroes irrelevant for integer number.
	    if (posDecSep != wxString::npos) {
		    // Find the last character to keep.
		    size_t posLastNonZero = s.find_last_not_of("0");
            // If it's the decimal separator itself, don't keep it either.
		    if (posLastNonZero == posDecSep)
		        -- posLastNonZero;
		    s.erase(posLastNonZero + 1);
		    // Remove sign from orphaned zero.
		    if (s.compare("-0") == 0)
		        s = "0";
		}
	}

    return s;
}

wxString get_thumbnail_string(const Vec2d& value)
{
    wxString ret_str = wxString::Format("%.2fx%.2f", value[0], value[1]);
    return ret_str;
}

wxString get_thumbnails_string(const std::vector<Vec2d>& values)
{
    wxString ret_str;
	for (size_t i = 0; i < values.size(); ++ i) {
		const Vec2d& el = values[i];
		ret_str += wxString::Format((i == 0) ? "%ix%i" : ", %ix%i", int(el[0]), int(el[1]));
	}
    return ret_str;
}

ThumbnailErrors validate_thumbnails_string(wxString& str, const wxString& def_ext = "PNG")
{
    std::string input_string = into_u8(str);

    str.Clear();

    auto [thumbnails_list, errors] = GCodeThumbnails::make_and_check_thumbnail_list(input_string);
    if (!thumbnails_list.empty()) {
        const auto& extentions = ConfigOptionEnum<GCodeThumbnailsFormat>::get_enum_names();
        for (const auto& [format, size] : thumbnails_list)
            str += format_wxstr("%1%x%2%/%3%, ", size.x(), size.y(), extentions[int(format)]);
        str.resize(str.Len() - 2);
    }

    return errors;
}

Field::~Field()
{
	if (m_on_kill_focus)
		m_on_kill_focus = nullptr;
	if (m_on_change)
		m_on_change = nullptr;
	if (m_back_to_initial_value)
		m_back_to_initial_value = nullptr;
	if (m_back_to_sys_value)
		m_back_to_sys_value = nullptr;
}

void Field::PostInitialize()
{
	auto color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);

	switch (m_opt.type)
	{
	case coPercents:
	case coFloats:
	case coStrings:
	case coBools:
	case coInts:
    // BBS
    case coEnums: {
		auto tag_pos = m_opt_id.find("#");
		if (tag_pos != std::string::npos)
			m_opt_idx = stoi(m_opt_id.substr(tag_pos + 1, m_opt_id.size()));
		break;
	}
	default:
		break;
	}

    // initialize m_unit_value
    m_em_unit = em_unit(m_parent);
    parent_is_custom_ctrl = dynamic_cast<OG_CustomCtrl*>(m_parent) != nullptr;

	BUILD();

	// For the mode, when settings are in non-modal dialog, neither dialog nor tabpanel doesn't receive wxEVT_KEY_UP event, when some field is selected.
	// So, like a workaround check wxEVT_KEY_UP event for the Filed and switch between tabs if Ctrl+(1-4) was pressed
    if (getWindow()) {
        if (m_opt.readonly) { 
            this->disable();
        } else {
            this->enable();
        }
		getWindow()->Bind(wxEVT_KEY_UP, [](wxKeyEvent& evt) {
		    if ((evt.GetModifiers() & wxMOD_CONTROL) != 0) {
			    int tab_id = -1;
			    switch (evt.GetKeyCode()) {
			    case '1': { tab_id = 0; break; }
			    case '2': { tab_id = 1; break; }
				case '3': { tab_id = 2; break; }
				case '4': { tab_id = 3; break; }
#ifdef __APPLE__
				case 'f':
#else /* __APPLE__ */
				case WXK_CONTROL_F:
#endif /* __APPLE__ */
                case 'F': {
                    //wxGetApp().plater()->search(false, Preset::TYPE_MODEL, nullptr, nullptr);
                    break;
                }
			    default: break;
			    }
			    if (tab_id >= 0)
					wxGetApp().mainframe->select_tab(tab_id);
				if (tab_id > 0)
					// tab panel should be focused for correct navigation between tabs
				    wxGetApp().tab_panel()->SetFocus();
		    }

		    evt.Skip();
	    }, getWindow()->GetId());
    }
}

// Values of width to alignments of fields
int Field::def_width()			{ return 8; }
int Field::def_width_wider()	{ return 12; }
int Field::def_width_thinner()	{ return 4; }

void Field::on_kill_focus()
{
	// call the registered function if it is available
    if (m_on_kill_focus!=nullptr)
        m_on_kill_focus(m_opt_id);
}

void Field::on_change_field()
{
//       std::cerr << "calling Field::_on_change \n";
    if (m_on_change != nullptr && !m_disable_change_event)
        m_on_change(m_opt_id, get_value());
}

void Field::on_back_to_initial_value()
{
	if (m_back_to_initial_value != nullptr && m_is_modified_value)
		m_back_to_initial_value(m_opt_id);
}

void Field::on_back_to_sys_value()
{
	if (m_back_to_sys_value != nullptr && m_is_nonsys_value)
		m_back_to_sys_value(m_opt_id);
}

void Field::on_edit_value()
{
    if (m_fn_edit_value)
        m_fn_edit_value(m_opt_id);
}

/// Fires the enable or disable function, based on the input.

void Field::toggle(bool en) { en && !m_opt.readonly ? enable() : disable(); }

wxString Field::get_tooltip_text(const wxString &default_string)
{
	wxString tooltip_text("");
#ifdef NDEBUG
	wxString tooltip = _(m_opt.tooltip);
    ::edit_tooltip(tooltip);

    std::string opt_id = m_opt_id;
    auto hash_pos = opt_id.find("#");
    if (hash_pos != std::string::npos) {
        opt_id.replace(hash_pos, 1,"[");
        opt_id += "]";
    }

	if (tooltip.length() > 0)
        tooltip_text = tooltip + "\n" + 
        _(L("parameter name")) + "\t: " + opt_id;
 #endif
	return tooltip_text;
}

bool Field::is_matched(const std::string& string, const std::string& pattern)
{
	std::regex regex_pattern(pattern, std::regex_constants::icase); // use ::icase to make the matching case insensitive like /i in perl
	return std::regex_match(string, regex_pattern);
}

void Field::get_value_by_opt_type(wxString& str, const bool check_value/* = true*/)
{
	switch (m_opt.type) {
    case coInt: {
        long val = 0;
        if (!str.ToLong(&val)) {
            if (!check_value) {
                m_value.clear();
                break;
            }
            show_error(m_parent, _(L("Invalid numeric.")));
            set_value(int(val), true);
        }
        m_value = int(val);
        break;
    }
	case coPercent:
	case coPercents:
	case coFloats:
	case coFloat:{
		if (m_opt.type == coPercent && !str.IsEmpty() &&  str.Last() == '%')
			str.RemoveLast();
		else if (!str.IsEmpty() && str.Last() == '%')
        {
            if (!check_value) {
                m_value.clear();
                break;
            }

			wxString label = m_opt.full_label.empty() ? _(m_opt.label) : _(m_opt.full_label);
            show_error(m_parent, from_u8((boost::format(_utf8(L("%s can't be a percentage"))) % into_u8(label)).str()));
			set_value(double_to_string(m_opt.min), true);
			m_value = double(m_opt.min);
			break;
		}
        double val;

        bool is_na_value = m_opt.nullable && str == m_na_value;

        const char dec_sep = is_decimal_separator_point() ? '.' : ',';
        const char dec_sep_alt = dec_sep == '.' ? ',' : '.';
        // Replace the first incorrect separator in decimal number, 
        // if this value doesn't "N/A" value in some language
        if (!is_na_value && str.Replace(dec_sep_alt, dec_sep, false) != 0)
            set_value(str, false);

        if (str == dec_sep)
            val = 0.0;
        else
        {
            if (is_na_value)
                val = ConfigOptionFloatsNullable::nil_value();
            else if (!str.ToDouble(&val))
            {
                if (!check_value) {
                    m_value.clear();
                    break;
                }
                show_error(m_parent, _(L("Invalid numeric.")));
                set_value(double_to_string(val), true);
            }
            if (m_opt.min > val || val > m_opt.max)
            {
                if (!check_value) {
                    m_value.clear();
                    break;
                }
                std::string opt_key_without_idx = m_opt_id.substr(0, m_opt_id.find('#'));
                if (m_opt_id == "filament_flow_ratio") {
                    if (m_value.empty() || boost::any_cast<double>(m_value) != val) {
                        wxString msg_text = format_wxstr(_L("Value %s is out of range, continue?"), str);
//                        wxMessageDialog dialog(m_parent, msg_text, _L("Parameter validation") + ": " + m_opt_id, wxICON_WARNING | wxYES | wxNO);
                        WarningDialog dialog(m_parent, msg_text, _L("Parameter validation") + ": " + m_opt_id, wxYES | wxNO);
                        if (dialog.ShowModal() == wxID_NO) {
                            if (m_value.empty()) {
                                if (m_opt.min > val) val = m_opt.min;
                                if (val > m_opt.max) val = m_opt.max;
                            }
                            else
                                val = boost::any_cast<double>(m_value);
                            set_value(double_to_string(val), true);
                        }
                    }
                }
                else if(m_opt_id == "filament_retraction_distances_when_cut" || opt_key_without_idx == "retraction_distances_when_cut"){
                    if (m_value.empty() || boost::any_cast<double>(m_value) != val) {
                        wxString msg_text = format_wxstr(_L("Value %s is out of range. The valid range is from %d to %d."), str, m_opt.min, m_opt.max);
                        WarningDialog dialog(m_parent, msg_text, _L("Parameter validation") + ": " + m_opt_id, wxYES);
                        if (dialog.ShowModal()) {
                            if (m_value.empty()) {
                                if (m_opt.min > val) val = m_opt.min;
                                if (val > m_opt.max) val = m_opt.max;
                            }
                            else
                                val = boost::any_cast<double>(m_value);
                            set_value(double_to_string(val), true);
                        }
                    }
                }
                else {
                    show_error(m_parent, _L("Value is out of range."));
                    if (m_opt.min > val) val = m_opt.min;
                    if (val > m_opt.max) val = m_opt.max;
                    set_value(double_to_string(val), true);
                }
            }
        }
        m_value = val;
		break; }
	case coString:
	case coStrings:
    case coFloatOrPercent: {
        if (m_opt.type == coFloatOrPercent && !str.IsEmpty() &&  str.Last() != '%')
        {
            double val = 0.;
            const char dec_sep = is_decimal_separator_point() ? '.' : ',';
            const char dec_sep_alt = dec_sep == '.' ? ',' : '.';
            // Replace the first incorrect separator in decimal number.
            if (str.Replace(dec_sep_alt, dec_sep, false) != 0)
                set_value(str, false);


            // remove space and "mm" substring, if any exists
            str.Replace(" ", "", true);
            str.Replace("m", "", true);

            if (!str.ToDouble(&val))
            {
                if (!check_value) {
                    m_value.clear();
                    break;
                }
                show_error(m_parent, _L("Invalid numeric."));
                set_value(double_to_string(val), true);
            }
            else if (((m_opt.sidetext.rfind("mm/s") != std::string::npos && val > m_opt.max) ||
                     (m_opt.sidetext.rfind("mm ") != std::string::npos && val > /*1*/m_opt.max_literal)) &&
                     (m_value.empty() || into_u8(str) != boost::any_cast<std::string>(m_value)))
            {
                if (!check_value) {
                    m_value.clear();
                    break;
                }

                const std::string sidetext = m_opt.sidetext.rfind("mm/s") != std::string::npos ? "mm/s" : "mm";
                const wxString stVal = double_to_string(val, 2);
                const wxString msg_text = from_u8((boost::format(_utf8(L("Is it %s%% or %s %s?\n"
                    "YES for %s%%, \n"
                    "NO for %s %s."))) % stVal % stVal % sidetext % stVal % stVal % sidetext).str());
                WarningDialog dialog(m_parent, msg_text, _L("Parameter validation") + ": " + m_opt_id, wxYES | wxNO);
                if ((val > 100) && dialog.ShowModal() == wxID_YES) {
                    set_value(from_u8((boost::format("%s%%") % stVal).str()), false/*true*/);
                    str += "%%";
                }
				else
					set_value(stVal, false); // it's no needed but can be helpful, when inputted value contained "," instead of "."
            }
        }
        if (m_opt.opt_key == "thumbnails") {
            wxString        str_out = str;
            ThumbnailErrors errors  = validate_thumbnails_string(str_out);
            if (errors != enum_bitmask<ThumbnailError>()) {
                set_value(str_out, true);
                wxString error_str;
                if (errors.has(ThumbnailError::InvalidVal))
                    error_str += format_wxstr(_L("Invalid input format. Expected vector of dimensions in the following format: \"%1%\""),
                                              "XxY/EXT, XxY/EXT, ...");
                if (errors.has(ThumbnailError::OutOfRange)) {
                    if (!error_str.empty())
                        error_str += "\n\n";
                    error_str += _L("Input value is out of range");
                }
                if (errors.has(ThumbnailError::InvalidExt)) {
                    if (!error_str.empty())
                        error_str += "\n\n";
                    error_str += _L("Some extension in the input is invalid");
                }
                show_error(m_parent, error_str);
            } else if (str_out != str) {
                str = str_out;
                set_value(str, true);
            }
        } else if (m_opt.opt_key == "sparse_infill_rotate_template" || m_opt.opt_key == "solid_infill_rotate_template") {
            string ustr(str.utf8_string());
            if (!ConfigOptionFloats::validate_string(ustr)) {
                string      v;
                std::smatch match;
                string      ps = (m_opt.opt_key == "sparse_infill_rotate_template") ?
                                     u8"[BT][!]?|[#][\\d]+[!]?|[+\\-]?[\\d.]+[%]?[*]?[\\d]*[/NnZz$LlUuQq~^|#]?[+\\-]?[\\d.]*[%#\'\"cm]?[m]?[BT]?[!*]?" :
                                     u8"[#][\\d]+[!]?|[+\\-]?[\\d.]+[%]?[*]?[\\d]*[/NnZz$LlUuQq~^|#]?[+\\-]?[\\d.]*[%#\'\"cm]?[m]?[!*]?";

                while (std::regex_search(ustr, match, std::regex(ps))) {
                    for (auto x : match) v += x.str() + ", ";
                    ustr = match.suffix().str();
                }
                v = v.substr(0, v.length() - 2);
                try {
                    this->set_value(from_u8(v), true);
                    m_value = into_u8(v);
                } catch (...) {
                    show_error(m_parent, format_wxstr(_L("This parameter expects a valid template.")));
                    wxString old_value(boost::any_cast<std::string>(m_value));
                    this->set_value(old_value, true); // Revert to previous value
                }
            } else {
                // Valid string, so update m_value with the new string from the control.
                m_value = into_u8(str);
            }
            break;
        } else if (m_opt.opt_key == "extra_solid_infills") {
            string ustr(str.utf8_string());
            // New rule: accept either interval form (N or N#K) or explicit list (e.g. 1,7,9), with optional quotes.
            const std::regex rx_interval(u8R"(^\s*['"]?\s*\d+\s*(?:#\s*\d*)?\s*['"]?\s*$)");
            // List entries may be plain numbers or number with optional #K count, e.g., 5, 9#2, 18
            const std::regex rx_list(u8R"(^\s*['"]?\s*\d+(?:\s*#\s*\d*)?(?:\s*,\s*\d+(?:\s*#\s*\d*)?)*\s*['"]?\s*$)");
            bool is_valid = ustr.empty() || std::regex_match(ustr, rx_interval) || std::regex_match(ustr, rx_list);
            if (!is_valid) {
                show_error(m_parent, format_wxstr(_L("Invalid pattern. Use N, N#K, or a comma-separated list with optional #K per entry. Examples: 5, 5#2, 1,7,9, 5,9#2,18.")));
                wxString old_value(boost::any_cast<std::string>(m_value));
                this->set_value(old_value, true); // Revert to previous value
            }
            // Valid string or empty, so update m_value with the new string from the control.
            m_value = into_u8(str);
            break;
        }

        m_value = into_u8(str);
		break; }
    case coPoint:{
        Vec2d out_value;
        str.Replace(" ", wxEmptyString, true);
        if (!str.IsEmpty()) {
            bool              invalid_val      = true;
            double            x, y;
            wxStringTokenizer thumbnail(str, "x");
            if (thumbnail.HasMoreTokens()) {
                wxString x_str = thumbnail.GetNextToken();
                if (x_str.ToDouble(&x) && thumbnail.HasMoreTokens()) {
                    wxString y_str = thumbnail.GetNextToken();
                    if (y_str.ToDouble(&y) && !thumbnail.HasMoreTokens()) {
                        out_value  = Vec2d(x, y);
                        invalid_val = false;
                    }
                }
            }

            if (invalid_val) {
                wxString text_value;
                if (!m_value.empty()) text_value = get_thumbnail_string(boost::any_cast<Vec2d>(m_value));
                set_value(text_value, true);
                show_error(m_parent, format_wxstr(_L("Invalid format. Expected vector format: \"%1%\""), "XxY, XxY, ..."));
            }
        }

        m_value = out_value;
        break; }

    case coPoints: {
        std::vector<Vec2d> out_values;
        str.Replace(" ", wxEmptyString, true);
        if (!str.IsEmpty()) {
            bool invalid_val = false;
            bool out_of_range_val = false;
            wxStringTokenizer points(str, ",");
            while (points.HasMoreTokens()) {
                wxString token = points.GetNextToken();
                double x, y;
                wxStringTokenizer _point(token, "x");
                if (_point.HasMoreTokens()) {
                    wxString x_str = _point.GetNextToken();
                    if (x_str.ToDouble(&x) && _point.HasMoreTokens()) {
                        wxString y_str = _point.GetNextToken();
                        if (y_str.ToDouble(&y) && !_point.HasMoreTokens()) {
                            if (m_opt_id == "bed_exclude_area") {
                                if (0 <= x &&  0 <= y) {
                                    out_values.push_back(Vec2d(x, y));
                                    continue;
                                }
                            }
                            else if (m_opt_id == "printable_area") {
                                if (0 <= x && x <= 1000 && 0 <= y && y <= 1000) {
                                    out_values.push_back(Vec2d(x, y));
                                    continue;
                                }
                            }
                            else {
                                if (0 < x && x < 1000 && 0 < y && y < 1000) {
                                    out_values.push_back(Vec2d(x, y));
                                    continue;
                                }
                            }
                            out_of_range_val = true;
                            break;
                        }
                    }
                }
                invalid_val = true;
                break;
            }

            if (out_of_range_val) {
                wxString text_value;
                if (!m_value.empty())
                    text_value = get_thumbnails_string(boost::any_cast<std::vector<Vec2d>>(m_value));
                set_value(text_value, true);
                show_error(m_parent, _L("Value is out of range."));
            }
            else if (invalid_val) {
                wxString text_value;
                if (!m_value.empty())
                    text_value = get_thumbnails_string(boost::any_cast<std::vector<Vec2d>>(m_value));
                set_value(text_value, true);
                show_error(m_parent, format_wxstr(_L("Invalid format. Expected vector format: \"%1%\""),"XxY, XxY, ..." ));
            }
        }

        m_value = out_values;
        break; }

	default:
		break;
	}
}

void Field::msw_rescale()
{
	// update em_unit value
	m_em_unit = em_unit(m_parent);
}

void Field::sys_color_changed()
{
#ifdef _WIN32
	if (wxWindow* win = this->getWindow())
		wxGetApp().UpdateDarkUI(win);
#endif
}

std::vector<std::deque<wxWindow *>**> spools;
std::vector<std::deque<wxWindow *>*> spools2;

void switch_window_pools()
{
    for (auto p : spools) {
        spools2.push_back(*p);
        *p = new std::deque<wxWindow*>;
    }
}

void release_window_pools()
{
    for (auto p : spools2) {
        delete p;
    }
    spools2.clear();
}

template<typename T>
struct Builder
{
    Builder()
    {
        pool_ = new std::deque<wxWindow*>;
        spools.push_back(&pool_);
    }

    template<typename... Args>
    T *build(wxWindow * p, Args ...args)
    {
        if (pool_->empty()) {
            auto t = new T(p, args...);
            t->SetClientData(pool_);
            return t;
        }
        auto t = dynamic_cast<T*>(pool_->front());
        pool_->pop_front();
        t->Reparent(p);
        t->Enable();
        t->Show();
        return t;
    }
    std::deque<wxWindow*>* pool_;
};

struct wxEventFunctorRef
{
    wxEventFunctor * func;
};

wxEventFunctor & wxMakeEventFunctor(const int, wxEventFunctorRef func)
{
    return *func.func;
}

struct myEvtHandler : wxEvtHandler
{
    void UnbindAll()
    {
        size_t cookie;
        for (wxDynamicEventTableEntry *entry = GetFirstDynamicEntry(cookie);
                entry;
                entry = GetNextDynamicEntry(cookie)) {
            // In Field, All Bind has id, but for TextInput, ComboBox, SpinInput, all not
            if (entry->m_id != wxID_ANY && entry->m_lastId == wxID_ANY)
                Unbind(entry->m_eventType,
                    wxEventFunctorRef{entry->m_fn}, 
                    entry->m_id, 
                    entry->m_lastId, 
                    entry->m_callbackUserData);
            //DoUnbind(entry->m_id, entry->m_lastId, entry->m_eventType, *entry->m_fn, entry->m_callbackUserData);
        }
    }
};

static void unbind_events(wxEvtHandler *h)
{
    static_cast<myEvtHandler *>(h)->UnbindAll();
}

void free_window(wxWindow *win)
{
#if !defined(__WXGTK__)
    unbind_events(win);
    for (auto c : win->GetChildren())
        if (dynamic_cast<wxTextCtrl*>(c))
            unbind_events(c);
    win->Hide();
    if (auto sizer = win->GetContainingSizer())
        sizer->Clear();
    win->Reparent(wxGetApp().mainframe);
    if (win->GetClientData())
        reinterpret_cast<std::deque<wxWindow *>*>(win->GetClientData())->push_back(win);
#else
    delete win;
#endif
}

template<class T>
bool is_defined_input_value(wxWindow* win, const ConfigOptionType& type)
{
    if (!win || (static_cast<T*>(win)->GetValue().empty() && type != coString && type != coStrings && type != coPoints && type != coPoint))
        return false;
    return true;
}

void TextCtrl::BUILD() {
    auto size = wxSize(def_width_wider() * m_em_unit, wxDefaultCoord);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

	wxString text_value = wxString("");

	switch (m_opt.type) {
	case coFloatOrPercent:
	{
		text_value = double_to_string(m_opt.default_value->getFloat());
		if (m_opt.get_default_value<ConfigOptionFloatOrPercent>()->percent)
			text_value += "%";
		break;
	}
	case coPercent:
	{
		text_value = wxString::Format(_T("%i"), int(m_opt.default_value->getFloat()));
		text_value += "%";
		break;
	}
	case coPercents:
	case coFloats:
	case coFloat:
	{
		double val = m_opt.type == coFloats ?
			m_opt.get_default_value<ConfigOptionFloats>()->get_at(m_opt_idx) :
			m_opt.type == coFloat ?
				m_opt.default_value->getFloat() :
				m_opt.get_default_value<ConfigOptionPercents>()->get_at(m_opt_idx);
		text_value = double_to_string(val);
        m_last_meaningful_value = text_value;
		break;
	}
	case coString:
		text_value = m_opt.get_default_value<ConfigOptionString>()->value;
		break;
	case coStrings:
	{
		const ConfigOptionStrings *vec = m_opt.get_default_value<ConfigOptionStrings>();
		if (vec == nullptr || vec->empty()) break; //for the case of empty default value
		text_value = vec->get_at(m_opt_idx);
		break;
	}
    case coPoint:
        text_value = get_thumbnail_string(m_opt.get_default_value<ConfigOptionPoint>()->value);
        break;
    case coPoints:
        text_value = get_thumbnails_string(m_opt.get_default_value<ConfigOptionPoints>()->values);
        break;
	default:
		break;
	}

	// BBS: new param ui style
    // const long style = m_opt.multiline ? wxTE_MULTILINE : wxTE_PROCESS_ENTER/*0*/;
    static Builder<wxTextCtrl> builder1;
    static Builder<::TextInput> builder2;
    auto temp = m_opt.multiline
        ? (wxWindow*)builder1.build(m_parent, wxID_ANY, "", wxDefaultPosition, size, wxTE_MULTILINE)
        : builder2.build(m_parent, "", "", "", wxDefaultPosition, size, wxTE_PROCESS_ENTER);
    temp->SetLabel(_L(m_opt.sidetext));
	auto text_ctrl = m_opt.multiline ? (wxTextCtrl *)temp : ((TextInput *) temp)->GetTextCtrl();
    text_ctrl->SetLabel(text_value);
    temp->SetSize(size);
    m_combine_side_text = !m_opt.multiline;
    if (parent_is_custom_ctrl && m_opt.height < 0)
        opt_height = (double) text_ctrl->GetSize().GetHeight() / m_em_unit;
    if (m_opt.is_code)
        temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());


    temp->SetForegroundColour(StateColor::darkModeColorFor(*wxBLACK));
	wxGetApp().UpdateDarkUI(temp);

    if (! m_opt.multiline && !wxOSX)
		// Only disable background refresh for single line input fields, as they are completely painted over by the edit control.
		// This does not apply to the multi-line edit field, where the last line and a narrow frame around the text is not cleared.
		temp->SetBackgroundStyle(wxBG_STYLE_PAINT);
#ifdef __WXOSX__
    text_ctrl->OSXDisableAllSmartSubstitutions(); // BBS
#endif // __WXOSX__

	temp->SetToolTip(get_tooltip_text(text_value));

    if (!m_opt.multiline) {
        text_ctrl->Bind(wxEVT_TEXT_ENTER, ([this, temp](wxEvent &e)
        {
#if !defined(__WXGTK__)
            e.Skip();
            temp->GetToolTip()->Enable(true);
#endif // __WXGTK__
            EnterPressed enter(this);
            propagate_value();
        }), text_ctrl->GetId());
    } else {
        // Orca: adds logic that scrolls the parent if the text control doesn't have focus
        text_ctrl->Bind(wxEVT_MOUSEWHEEL, [text_ctrl](wxMouseEvent& event) {
            if (text_ctrl->HasFocus() && text_ctrl->GetScrollRange(wxVERTICAL) != 1)
                event.Skip(); // don't consume the event so that the text control will scroll
            else
                text_ctrl->GetParent()->ScrollLines((event.GetWheelRotation() > 0 ? -1 : 1) * event.GetLinesPerAction());
        });
    }

	text_ctrl->Bind(wxEVT_LEFT_DOWN, ([temp](wxEvent &event)
	{
		//! to allow the default handling
		event.Skip();
		//! eliminating the g-code pop up text description
		bool flag = false;
#ifdef __WXGTK__
		// I have no idea why, but on GTK flag works in other way
		flag = true;
#endif // __WXGTK__
		temp->GetToolTip()->Enable(flag);
	}), text_ctrl->GetId());

	temp->Bind(wxEVT_KILL_FOCUS, ([this, temp](wxEvent &e)
	{
		e.Skip();
#if !defined(__WXGTK__)
		temp->GetToolTip()->Enable(true);
#endif // __WXGTK__
        if (!bEnterPressed)
            propagate_value();
	}), temp->GetId());
        /*
	// select all text using Ctrl+A
	temp->Bind(wxEVT_CHAR, ([temp](wxKeyEvent& event)
	{
		if (wxGetKeyState(wxKeyCode('A')) && wxGetKeyState(WXK_CONTROL))
			temp->SetSelection(-1, -1); //select all
		event.Skip();
	}));
*/
    // recast as a wxWindow to fit the calling convention
    window = dynamic_cast<wxWindow*>(temp);
}

bool TextCtrl::value_was_changed()
{
    if (m_value.empty())
        return true;

    boost::any val = m_value;
    wxString   ret_str = text_ctrl()->GetValue(); // BBS
    // update m_value!
    // ret_str might be changed inside get_value_by_opt_type
    get_value_by_opt_type(ret_str);

    switch (m_opt.type) {
    case coInt:
        return boost::any_cast<int>(m_value) != boost::any_cast<int>(val);
    case coPercent:
    case coPercents:
    case coFloats:
    case coFloat: {
        if (m_opt.nullable && std::isnan(boost::any_cast<double>(m_value)) &&
                              std::isnan(boost::any_cast<double>(val)))
            return false;
        return boost::any_cast<double>(m_value) != boost::any_cast<double>(val);
    }
    case coString:
    case coStrings:
    case coFloatOrPercent:
        return boost::any_cast<std::string>(m_value) != boost::any_cast<std::string>(val);
    default:
        return true;
    }
}

void TextCtrl::propagate_value()
{
    if (!is_defined_input_value<wxTextCtrl>(text_ctrl(), m_opt.type)) { // BBS
		// on_kill_focus() cause a call of OptionsGroup::reload_config(),
		// Thus, do it only when it's really needed (when undefined value was input)
        if (!m_value.empty()) // BBS: null value
            on_kill_focus();
	} else if (value_was_changed())
        on_change_field();
}

void TextCtrl::set_value(const boost::any& value, bool change_event/* = false*/) {
    m_disable_change_event = !change_event;
    if (m_opt.nullable) {
        if (boost::any_cast<wxString>(value) != _(L("N/A")))
            m_last_meaningful_value = value;

        text_ctrl()->SetValue(boost::any_cast<wxString>(value)); // BBS
    }
    else
        text_ctrl()->SetValue(value.empty() ? "" : boost::any_cast<wxString>(value)); // BBS // BBS: null value
    m_disable_change_event = false;

    if (!change_event) {
        wxString ret_str = text_ctrl()->GetValue();
        /* Update m_value to correct work of next value_was_changed().
         * But after checking of entered value, don't fix the "incorrect" value and don't show a warning message,
         * just clear m_value in this case.
         */
        get_value_by_opt_type(ret_str, false);
    }
}

void TextCtrl::set_last_meaningful_value()
{
    text_ctrl()->SetValue(boost::any_cast<wxString>(m_last_meaningful_value)); // BBS
    propagate_value();
}

void TextCtrl::update_na_value(const boost::any& value)
{
    m_na_value = boost::any_cast<wxString>(value);
}

void TextCtrl::set_na_value()
{
    text_ctrl()->SetValue(m_na_value); // BBS
    propagate_value();
}

boost::any& TextCtrl::get_value()
{
    wxString ret_str = text_ctrl()->GetValue(); // BBS
	// update m_value
	get_value_by_opt_type(ret_str);

	return m_value;
}

void TextCtrl::msw_rescale()
{
    Field::msw_rescale();
    auto size = wxSize(def_width_wider() * m_em_unit, wxDefaultCoord);

    if (m_opt.height >= 0)
        size.SetHeight(m_opt.height*m_em_unit);
    else if (parent_is_custom_ctrl && opt_height > 0)
        size.SetHeight(lround(opt_height*m_em_unit));
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

    if (size != wxDefaultSize)
    {
        wxTextCtrl *field = text_ctrl(); // BBS
        if (parent_is_custom_ctrl)
            field->SetSize(size);
        else
            field->SetMinSize(size);
        if (field != window) {
            window->SetSize(size);
            window->SetMinSize(size);
            dynamic_cast<::TextInput *>(window)->Rescale();
        }
    }
}

void TextCtrl::enable()
{
    window->Enable();
    text_ctrl()->SetEditable(true); // BBS
}
void TextCtrl::disable()
{
    window->Disable();
    text_ctrl()->SetEditable(false); // BBS
}

 // BBS
wxTextCtrl *TextCtrl::text_ctrl()
{
    auto ctrl = dynamic_cast<wxTextCtrl *>(window);
    if (ctrl == nullptr)
        ctrl = dynamic_cast<::TextInput *>(window)->GetTextCtrl();
    return ctrl;
}

#ifdef __WXGTK__
void TextCtrl::change_field_value(wxEvent& event)
{
    if ((bChangedValueEvent = (event.GetEventType()==wxEVT_KEY_UP)))
		on_change_field();
    event.Skip();
};
#endif //__WXGTK__

void CheckBox::BUILD() {
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

	bool check_value =	m_opt.type == coBool ?
						m_opt.default_value->getBool() : m_opt.type == coBools ?
							m_opt.get_default_value<ConfigOptionBools>()->get_at(m_opt_idx) :
    						false;

    m_last_meaningful_value = static_cast<unsigned char>(check_value);

	// BBS: use ::CheckBox
    static Builder<::CheckBox> builder;
	auto temp = builder.build(m_parent); 
	if (!wxOSX) temp->SetBackgroundStyle(wxBG_STYLE_PAINT);
	//temp->SetBackgroundColour(*wxWHITE);
	temp->SetValue(check_value);

	temp->Bind(wxEVT_TOGGLEBUTTON, ([this](wxCommandEvent & e) {
        m_is_na_val = false;
	    on_change_field();
		e.Skip();
	}), temp->GetId());

	temp->SetToolTip(get_tooltip_text(check_value ? "true" : "false"));

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);
}

void CheckBox::set_value(const bool value, bool change_event)
{
	m_disable_change_event = !change_event;
    dynamic_cast<::CheckBox *>(window)->SetValue(value); // BBS
	m_disable_change_event = false;
}

void CheckBox::set_value(const boost::any& value, bool change_event)
{
    m_disable_change_event = !change_event;
    if (m_opt.nullable) {
        const bool is_value_unsigned_char = value.type() == typeid(unsigned char);
        m_is_na_val = is_value_unsigned_char &&
                      boost::any_cast<unsigned char>(value) == ConfigOptionBoolsNullable::nil_value();
        if (!m_is_na_val)
            m_last_meaningful_value = is_value_unsigned_char ? value : static_cast<unsigned char>(boost::any_cast<bool>(value));

        const auto bool_value = is_value_unsigned_char ?
                                    boost::any_cast<unsigned char>(value) != 0 :
                                    boost::any_cast<bool>(value);
        dynamic_cast<::CheckBox*>(window)->SetValue(m_is_na_val ? false : bool_value); // BBS
    }
    else if (!value.empty()) // BBS: null value
        dynamic_cast<::CheckBox*>(window)->SetValue(boost::any_cast<bool>(value)); // BBS
    dynamic_cast<::CheckBox*>(window)->SetHalfChecked(value.empty());
    m_disable_change_event = false;
}

void CheckBox::set_last_meaningful_value()
{
    if (m_opt.nullable) {
        m_is_na_val = false;
        dynamic_cast<::CheckBox*>(window)->SetValue(boost::any_cast<unsigned char>(m_last_meaningful_value) != 0); // BBS
        on_change_field();
    }
}

void CheckBox::set_na_value()
{
    if (m_opt.nullable) {
        m_is_na_val = true;
        dynamic_cast<::CheckBox *>(window)->SetValue(false); // BBS
        on_change_field();
    }
}

boost::any& CheckBox::get_value()
{
// 	boost::any m_value;
	bool value = dynamic_cast<::CheckBox*>(window)->GetValue(); // BBS
	if (m_opt.type == coBool)
		m_value = static_cast<bool>(value);
	else
		m_value = m_is_na_val ? ConfigOptionBoolsNullable::nil_value() : static_cast<unsigned char>(value);
 	return m_value;
}

void CheckBox::msw_rescale()
{
    Field::msw_rescale();

	// BBS: new param style
	::CheckBox* field = dynamic_cast<::CheckBox*>(window);
    //field->SetMinSize(wxSize(-1, int(1.5f*field->GetFont().GetPixelSize().y +0.5f)));
    field->Rescale();
}


void SpinCtrl::BUILD() {
    auto size = wxSize(def_width_wider() * m_em_unit, wxDefaultCoord);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

	wxString	text_value = wxString("");
	int			default_value = 0;

	switch (m_opt.type) {
	case coInt:
		default_value = m_opt.default_value->getInt();
		text_value = wxString::Format(_T("%i"), default_value);
		break;
	case coInts:
	{
		const ConfigOptionInts *vec = m_opt.get_default_value<ConfigOptionInts>();
		if (vec == nullptr || vec->empty()) break;
		for (size_t id = 0; id < vec->size(); ++id)
		{
			default_value = vec->get_at(id);
			text_value += wxString::Format(_T("%i"), default_value);
		}
		break;
	}
	default:
		break;
	}

    const int min_val = m_opt.min == INT_MIN ? 0 : m_opt.min;
	const int max_val = m_opt.max < 2147483647 ? m_opt.max : 2147483647;

    static Builder<SpinInput> builder;
	auto temp = builder.build(m_parent, "", "", wxDefaultPosition, size,
		wxSP_ARROW_KEYS);
    temp->SetSize(size);
    temp->SetLabel(_L(m_opt.sidetext));
    temp->GetTextCtrl()->SetLabel(text_value);
    temp->SetRange(min_val, max_val);
    temp->SetValue(default_value);
    m_combine_side_text = true;
#ifdef __WXGTK3__
	wxSize best_sz = temp->GetBestSize();
	if (best_sz.x > size.x)
		temp->SetSize(wxSize(size.x + 2 * best_sz.y, best_sz.y));
#endif //__WXGTK3__
	// temp->SetFont(Slic3r::GUI::wxGetApp().normal_font()); // BBS
    if (!wxOSX) temp->SetBackgroundStyle(wxBG_STYLE_PAINT);
	wxGetApp().UpdateDarkUI(temp);

    if (m_opt.height < 0 && parent_is_custom_ctrl)
        opt_height = (double)temp->GetTextCtrl()->GetSize().GetHeight() / m_em_unit;

    temp->Bind(wxEVT_KILL_FOCUS, ([this](wxEvent &e)
	{
        e.Skip();
        if (bEnterPressed) {
            bEnterPressed = false;
            return;
        }

        propagate_value();
	}), temp->GetId());

    temp->Bind(wxEVT_SPINCTRL, ([this](wxCommandEvent e) {  propagate_value();  }), temp->GetId()); 
    
    temp->Bind(wxEVT_TEXT_ENTER, ([this](wxCommandEvent & e)
    {
        e.Skip();
        propagate_value();
        bEnterPressed = true;
    }), temp->GetId());

	temp->GetTextCtrl()->Bind(wxEVT_TEXT, ([this, temp](wxCommandEvent e)
	{
// 		# On OSX / Cocoa, SpinInput::GetValue() doesn't return the new value
// 		# when it was changed from the text control, so the on_change callback
// 		# gets the old one, and on_kill_focus resets the control to the old value.
// 		# As a workaround, we get the new value from $event->GetString and store
// 		# here temporarily so that we can return it from get_value()

		long value;
		const bool parsed = e.GetString().ToLong(&value);
        if (!parsed || value < INT_MIN || value > INT_MAX)
            tmp_value = UNDEF_VALUE;
        else {
            tmp_value = std::min(std::max((int)value, m_opt.min), m_opt.max);
#ifdef __WXOSX__
#ifdef UNDEFINED__WXOSX__ // BBS
            // Forcibly set the input value for SpinControl, since the value
            // inserted from the keyboard or clipboard is not updated under OSX
            SpinInput* spin = static_cast<SpinInput*>(window);
            spin->SetValue(tmp_value);
            // But in SetValue() is executed m_text_ctrl->SelectAll(), so
            // discard this selection and set insertion point to the end of string
            // temp->GetText()->SetInsertionPointEnd();
#endif
#else
            // update value for the control only if it was changed in respect to the Min/max values
            if (tmp_value != (int)value) {
                temp->SetValue(tmp_value);
                // But after SetValue() cursor ison the first position
                // so put it to the end of string
                // int pos = std::to_string(tmp_value).length();
                // temp->SetSelection(pos, pos);
            }
#endif
        }
	}), temp->GetTextCtrl()->GetId());

	temp->SetToolTip(get_tooltip_text(text_value));

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);
}

void SpinCtrl::propagate_value()
{
    if (suppress_propagation)
        return;

    suppress_propagation = true;
    if (tmp_value == UNDEF_VALUE) {
        if (!m_value.empty()) // BBS: null value
            on_kill_focus();
	} else {
        auto ctrl = dynamic_cast<SpinInput *>(window);
        if (m_value.empty() 
            ? !ctrl->GetTextCtrl()->GetLabel().IsEmpty()
            : ctrl->GetValue() != boost::any_cast<int>(m_value))
            on_change_field();
    }
    suppress_propagation = false;
}

void SpinCtrl::set_value(const boost::any& value, bool change_event) {
    m_disable_change_event = !change_event;
    m_value = value;
    if (value.empty()) { // BBS: null value
        dynamic_cast<SpinInput*>(window)->SetValue(m_opt.min);
        dynamic_cast<SpinInput*>(window)->GetTextCtrl()->SetValue("");
    }
    else {
        tmp_value = boost::any_cast<int>(value);
        dynamic_cast<SpinInput*>(window)->SetValue(tmp_value);
    }
    m_disable_change_event = false;
}

void SpinCtrl::msw_rescale()
{
    Field::msw_rescale();

    SpinInput* field = dynamic_cast<SpinInput*>(window);
    if (parent_is_custom_ctrl) {
        field->GetTextCtrl()->SetSize(wxSize(def_width_wider() * m_em_unit, lround(opt_height * m_em_unit)));
    } else {
        field->GetTextCtrl()->SetMinSize(wxSize(def_width_wider() * m_em_unit, int(1.9f * field->GetFont().GetPixelSize().y)));
    }
    field->SetSize(wxSize(def_width_wider() * m_em_unit, lround(opt_height * m_em_unit)));
    field->Rescale();
}

#ifdef __WXOSX__
using choice_ctrl = ::ComboBox; // BBS
#else
using choice_ctrl = ::ComboBox; // BBS
#endif // __WXOSX__

static std::map<std::string, DynamicList*> dynamic_lists;

void Choice::register_dynamic_list(std::string const &optname, DynamicList *list) { dynamic_lists.emplace(optname, list); }

void DynamicList::update()
{
    for (auto c : m_choices) apply_on(c);
}

void DynamicList::add_choice(Choice *choice)
{
    auto iter = std::find(m_choices.begin(), m_choices.end(), choice);
    if (iter != m_choices.end()) return;
    apply_on(choice);
    m_choices.push_back(choice);
}

void DynamicList::remove_choice(Choice *choice)
{
    auto iter = std::find(m_choices.begin(), m_choices.end(), choice);
    if (iter != m_choices.end()) m_choices.erase(iter);
}

Choice::~Choice()
{
    if (m_list) { m_list->remove_choice(this); }
}

void Choice::BUILD()
{
    wxSize size(def_width_wider() * m_em_unit, wxDefaultCoord);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

    if (m_opt.nullable)
        m_last_meaningful_value = dynamic_cast<ConfigOptionEnumsGenericNullable const *>(m_opt.default_value.get())->get_at(0);

    choice_ctrl *              temp;
    auto         dynamic_list = dynamic_lists.find(m_opt.opt_key);
    if (dynamic_list != dynamic_lists.end())
        m_list = dynamic_list->second;
    if (m_opt.gui_type != ConfigOptionDef::GUIType::undefined && m_opt.gui_type != ConfigOptionDef::GUIType::select_open 
            && m_list == nullptr) {
        m_is_editable = true;
        static Builder<choice_ctrl> builder1;
        temp = builder1.build(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr, wxTE_PROCESS_ENTER);
    }
    else {
#ifdef UNDEIFNED__WXOSX__ // __WXOSX__ // BBS
        /* wxBitmapComboBox with wxCB_READONLY style return NULL for GetTextCtrl(),
         * so ToolTip doesn't shown.
         * Next workaround helps to solve this problem
         */
        temp = new choice_ctrl();
        temp->SetTextCtrlStyle(wxTE_READONLY);
        temp->Create(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr);
#else
        static Builder<choice_ctrl> builder2;
        temp = builder2.build(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr, wxCB_READONLY);
#endif //__WXOSX__
    }
    // temp->SetSize(size);
    temp->Clear();
    temp->GetDropDown().SetUseContentWidth(true);
    if (parent_is_custom_ctrl && m_opt.height < 0)
        opt_height = (double) temp->GetTextCtrl()->GetSize().GetHeight() / m_em_unit;

    // BBS
    temp->SetTextLabel(m_opt.sidetext);
    m_combine_side_text = true;

#ifdef __WXGTK3__
    wxSize best_sz = temp->GetBestSize();
    if (best_sz.x > size.x)
        temp->SetSize(best_sz);
#endif //__WXGTK3__

	//temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    if (!wxOSX) temp->SetBackgroundStyle(wxBG_STYLE_PAINT);

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

	if (! m_opt.enum_labels.empty() || ! m_opt.enum_values.empty()) {
		if (m_opt.enum_labels.empty()) {
			// Append non-localized enum_values
			for (auto el : m_opt.enum_values)
				temp->Append(el);
		} else {
			// Append localized enum_labels
            int i = 0;
            boost::filesystem::path image_path(Slic3r::resources_dir());
            image_path /= "images";
            for (auto el : m_opt.enum_labels) {
                auto icon_name = "param_" + m_opt.enum_values[i];
                if (boost::filesystem::exists(image_path / (icon_name + ".svg"))) {
                    ScalableBitmap bm(temp, icon_name, 24);
				    temp->Append(_(el), bm.bmp());
                } else {
                    temp->Append(_(el));
                }
                ++i;
            }
		}
		set_selection();
    } else if (m_list) {
        m_list->add_choice(this);
        set_selection();
    }

    temp->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) {
        if (m_suppress_scroll && !m_is_dropped)
            e.StopPropagation();
        else
            e.Skip();
        }, temp->GetId());
    temp->Bind(wxEVT_COMBOBOX_DROPDOWN, [this](wxCommandEvent&) { m_is_dropped = true; }, temp->GetId());
    temp->Bind(wxEVT_COMBOBOX_CLOSEUP,  [this](wxCommandEvent&) { m_is_dropped = false; }, temp->GetId());

    temp->Bind(wxEVT_COMBOBOX,          [this](wxCommandEvent&) { on_change_field(); }, temp->GetId());

    if (m_is_editable) {
        temp->Bind(wxEVT_KILL_FOCUS, [this](wxEvent& e) {
            e.Skip();
            if (!bEnterPressed)
                propagate_value();
        }, temp->GetId() );

        temp->Bind(wxEVT_TEXT_ENTER, [this](wxEvent& e) {
            EnterPressed enter(this);
            propagate_value();
        }, temp->GetId() );
    }

	temp->SetToolTip(get_tooltip_text(temp->GetValue()));
}

void Choice::propagate_value()
{
    if (m_opt.type == coStrings) {
        on_change_field();
        return;
    }

    if (is_defined_input_value<choice_ctrl>(window, m_opt.type)) {
        switch (m_opt.type) {
        case coFloatOrPercent:
        {
            std::string old_val = !m_value.empty() ? boost::any_cast<std::string>(m_value) : "";
            if (old_val == boost::any_cast<std::string>(get_value()))
                return;
            break;
        }
        case coInt:
        {
            int old_val = !m_value.empty() ? boost::any_cast<int>(m_value) : 0;
            if (old_val == boost::any_cast<int>(get_value()))
                return;
            break;
        }
        default:
        {
            double old_val = !m_value.empty() ? boost::any_cast<double>(m_value) : -99999;
            if (fabs(old_val - boost::any_cast<double>(get_value())) <= 0.0001)
                return;
        }
        }
        on_change_field();
    }
    else if (!m_value.empty()) // BBS: null value
        on_kill_focus();
}

void Choice::suppress_scroll()
{
    m_suppress_scroll = true;
}

void Choice::set_selection()
{
    /* To prevent earlier control updating under OSX set m_disable_change_event to true
     * (under OSX wxBitmapComboBox send wxEVT_COMBOBOX even after SetSelection())
     */
    //m_disable_change_event = true;

	wxString text_value = wxString("");

    choice_ctrl* field = dynamic_cast<choice_ctrl*>(window);
	switch (m_opt.type) {
	case coEnum:{
        field->SetSelection(m_opt.default_value->getInt());
		break;
	}
	case coFloat:
	case coPercent:	{
		double val = m_opt.default_value->getFloat();
		text_value = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 1);
		break;
	}
	case coInt:{
		text_value = wxString::Format(_T("%i"), int(m_opt.default_value->getInt()));
		break;
	}
	case coStrings:{
		text_value = m_opt.get_default_value<ConfigOptionStrings>()->get_at(m_opt_idx);
		break;
	}
	case coFloatOrPercent: {
		text_value = double_to_string(m_opt.default_value->getFloat());
		if (m_opt.get_default_value<ConfigOptionFloatOrPercent>()->percent)
			text_value += "%";
		break;
	}
    default: break;
	}

	if (!text_value.IsEmpty()) {
        size_t idx = 0;
		for (auto el : m_opt.enum_values) {
			if (el == text_value)
				break;
			++idx;
		}
		idx == m_opt.enum_values.size() ? field->SetValue(text_value) : field->SetSelection(idx);
	}
}

void Choice::set_value(const std::string& value, bool change_event)  //! Redundant?
{
    m_disable_change_event = !change_event;

	size_t idx=0;
	for (auto el : m_opt.enum_values)
	{
		if (el == value)
			break;
		++idx;
	}

    choice_ctrl* field = dynamic_cast<choice_ctrl*>(window);
	idx == m_opt.enum_values.size() ?
		field->SetValue(value) :
		field->SetSelection(idx);

	m_disable_change_event = false;
}

void Choice::set_value(const boost::any& value, bool change_event)
{
	m_disable_change_event = !change_event;

    choice_ctrl* field = dynamic_cast<choice_ctrl*>(window);

    // BBS: null value
    if (value.empty()) {
        field->SetValue("");
        m_value = value;
        m_disable_change_event = false;
        return;
    }

	switch (m_opt.type) {
	case coInt:
	case coFloat:
	case coPercent:
	case coFloatOrPercent:
	case coString:
	case coStrings: {
		wxString text_value;
		if (m_opt.type == coInt)
			text_value = wxString::Format(_T("%i"), int(boost::any_cast<int>(value)));
		else
			text_value = boost::any_cast<wxString>(value);
        size_t idx = 0;
        const std::vector<std::string>& enums = m_opt.enum_values.empty() ? m_opt.enum_labels : m_opt.enum_values;
		for (auto el : enums)
		{
			if (el == text_value)
				break;
			++idx;
		}
        if (m_list)
			field->SetSelection(m_list->index_of(text_value));
        else if (idx == enums.size()) {
            // For editable Combobox under OSX is needed to set selection to -1 explicitly,
            // otherwise selection doesn't be changed
            field->SetSelection(-1);
            field->SetValue(text_value);
        }
        else
			field->SetSelection(idx);

        if (!m_value.empty() && m_opt.opt_key == "sparse_infill_density") {
            // If m_value was changed before, then update m_value here too to avoid case 
            // when control's value is already changed from the ConfigManipulation::update_print_fff_config(),
            // but m_value doesn't respect it.
            if (double val; text_value.ToDouble(&val))
                m_value = val;
        }

		break;
	}
	case coEnum:
    // BBS
    case coEnums: {
		int val = boost::any_cast<int>(value);

        // Support ThirdPartyPrinter
        if (m_opt_id.compare("host_type") == 0 && val != 0 &&
			m_opt.enum_values.size() > field->GetCount()) // for case, when PrusaLink isn't used as a HostType
			val--;
        if (m_opt_id == "top_surface_pattern" || m_opt_id == "bottom_surface_pattern" ||
            m_opt_id == "internal_solid_infill_pattern" || m_opt_id == "sparse_infill_pattern" ||
            m_opt_id == "support_base_pattern" || m_opt_id == "support_interface_pattern" ||
            m_opt_id == "ironing_pattern" || m_opt_id == "support_ironing_pattern" ||
            m_opt_id == "support_style" || m_opt_id == "curr_bed_type")
		{
			std::string key;
			const t_config_enum_values& map_names = *m_opt.enum_keys_map;
			for (auto it : map_names)
				if (val == it.second) {
					key = it.first;
					break;
				}

			const std::vector<std::string>& values = m_opt.enum_values;
			auto it = std::find(values.begin(), values.end(), key);
			val = it == values.end() ? 0 : it - values.begin();
		}
        if (m_opt.nullable) {
            if (val != ConfigOptionEnumsGenericNullable::nil_value())
                m_last_meaningful_value = value;
            else
                val = -1;
        }
		field->SetSelection(val);
		break;
	}
	default:
		break;
	}

	m_disable_change_event = false;
}

//! it's needed for _update_serial_ports()
void Choice::set_values(const std::vector<std::string>& values)
{
	if (values.empty())
		return;
	m_disable_change_event = true;

// 	# it looks that Clear() also clears the text field in recent wxWidgets versions,
// 	# but we want to preserve it
	auto ww = dynamic_cast<choice_ctrl*>(window);
	auto value = ww->GetValue();
	ww->Clear();
	ww->Append("");
	for (const auto &el : values)
		ww->Append(wxString(el));
	ww->SetValue(value);

	m_disable_change_event = false;
}

void Choice::set_values(const wxArrayString &values)
{
	if (values.empty())
		return;

	m_disable_change_event = true;

	// 	# it looks that Clear() also clears the text field in recent wxWidgets versions,
	// 	# but we want to preserve it
	auto ww = dynamic_cast<choice_ctrl*>(window);
	auto value = ww->GetValue();
	ww->Clear();
//	ww->Append("");
	for (const auto &el : values)
		ww->Append(el);
	ww->SetValue(value);

	m_disable_change_event = false;
}

boost::any& Choice::get_value()
{
    choice_ctrl* field = dynamic_cast<choice_ctrl*>(window);

	wxString ret_str = field->GetValue();

	// options from right panel
	std::vector <std::string> right_panel_options{ "support", "pad", "scale_unit" };
	for (auto rp_option: right_panel_options)
		if (m_opt_id == rp_option)
			return m_value = boost::any(ret_str);

    // BBS
	if (m_opt.type == coEnum || m_opt.type == coEnums)
    {
        if (m_opt.nullable && field->GetSelection() == -1)
            m_value = ConfigOptionEnumsGenericNullable::nil_value();
        else if (   m_opt_id == "top_surface_pattern" || m_opt_id == "bottom_surface_pattern" ||
                    m_opt_id == "internal_solid_infill_pattern" || m_opt_id == "sparse_infill_pattern" ||
                    m_opt_id == "support_base_pattern" || m_opt_id == "support_interface_pattern" ||
                    m_opt_id == "ironing_pattern" || m_opt_id == "support_ironing_pattern" ||
                    m_opt_id == "support_style" || m_opt_id == "curr_bed_type")
        {
            const std::string &key = m_opt.enum_values[field->GetSelection()];
            m_value = int(m_opt.enum_keys_map->at(key));
        }
        // Support ThirdPartyPrinter
        else if (m_opt_id.compare("host_type") == 0 && m_opt.enum_values.size() > field->GetCount())
        {
            // for case, when PrusaLink isn't used as a HostType
            m_value = field->GetSelection() + 1;
        }
        else
            m_value = field->GetSelection();
    }
    else if (m_opt.gui_type == ConfigOptionDef::GUIType::f_enum_open || m_opt.gui_type == ConfigOptionDef::GUIType::i_enum_open) {
        const int ret_enum = field->GetSelection();
        if (m_list) {
            ret_str = m_list->get_value(ret_enum);
            get_value_by_opt_type(ret_str);
        } else if (ret_enum < 0 || m_opt.enum_values.empty() || m_opt.type == coStrings ||
            (ret_str != m_opt.enum_values[ret_enum] && ret_str != _(m_opt.enum_labels[ret_enum])))
			// modifies ret_string!
            get_value_by_opt_type(ret_str);
        else if (m_opt.type == coFloatOrPercent)
            m_value = m_opt.enum_values[ret_enum];
        else if (m_opt.type == coInt)
            m_value = atoi(m_opt.enum_values[ret_enum].c_str());
        else
            m_value = string_to_double_decimal_point(m_opt.enum_values[ret_enum]);
    }
	else
		// modifies ret_string!
        get_value_by_opt_type(ret_str);

	return m_value;
}

void Choice::set_last_meaningful_value()
{
    if (m_opt.nullable) {
        set_value(m_last_meaningful_value, false);
        on_change_field();
    }
}

void Choice::set_na_value()
{
    dynamic_cast<choice_ctrl *>(window)->SetSelection(-1);
    on_change_field();
}

void Choice::enable()  { dynamic_cast<choice_ctrl*>(window)->Enable(); }
void Choice::disable() { dynamic_cast<choice_ctrl*>(window)->Disable(); }

void Choice::msw_rescale()
{
    Field::msw_rescale();

    auto* field = dynamic_cast<choice_ctrl*>(window)->GetTextCtrl();
#ifdef UNDEFINED__WXOSX__ // BBS
    const wxString selection = field->GetValue();// field->GetString(index);

	/* To correct scaling (set new controll size) of a wxBitmapCombobox
	 * we need to refill control with new bitmaps. So, in our case :
	 * 1. clear control
	 * 2. add content
	 * 3. add scaled "empty" bitmap to the at least one item
	 */
    field->Clear();
    wxSize size(wxDefaultSize);
    size.SetWidth((m_opt.width > 0 ? m_opt.width : def_width_wider()) * m_em_unit);

    // Set rescaled min height to correct layout
    field->SetMinSize(wxSize(-1, int(1.5f*field->GetFont().GetPixelSize().y + 0.5f)));
    // Set rescaled size
    field->SetSize(size);

    size_t idx = 0;
    if (! m_opt.enum_labels.empty() || ! m_opt.enum_values.empty()) {
    	size_t counter = 0;
    	bool   labels = ! m_opt.enum_labels.empty();
        for (const std::string &el : labels ? m_opt.enum_labels : m_opt.enum_values) {
        	wxString text = labels ? _(el) : from_u8(el);
            field->Append(text);
            if (text == selection)
                idx = counter;
            ++ counter;
        }
    }

    idx == m_opt.enum_values.size() ?
        field->SetValue(selection) :
        field->SetSelection(idx);
#else
    if (!m_opt.enum_labels.empty()) {
        boost::filesystem::path image_path(Slic3r::resources_dir());
        image_path /= "images";
        int i = 0;
        auto temp = dynamic_cast<choice_ctrl *>(window);
        for (auto el : m_opt.enum_values) {
            auto icon_name = "param_" + m_opt.enum_values[i];
            if (boost::filesystem::exists(image_path / (icon_name + ".svg"))) {
                ScalableBitmap bm(window, icon_name, 24);
                temp->SetItemBitmap(i, bm.bmp());
            }
            ++i;
        }
    }
    auto size = wxSize(def_width_wider() * m_em_unit, wxDefaultCoord);
    if (m_opt.height >= 0)
        size.SetHeight(m_opt.height * m_em_unit);
    else if (parent_is_custom_ctrl && opt_height > 0)
        size.SetHeight(lround(opt_height * m_em_unit));
    if (m_opt.width >= 0) size.SetWidth(m_opt.width * m_em_unit);

    if (parent_is_custom_ctrl)
        field->SetSize(size);
    else
        field->SetMinSize(size);
    window->SetSize(size);
    window->SetMinSize(size);
    dynamic_cast<choice_ctrl*>(window)->Rescale();
#endif
}

void ColourPicker::BUILD()
{
    auto size = wxSize(def_width_wider() * m_em_unit, -1); // ORCA match color picker width
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

	// Validate the color
	wxString clr_str(m_opt.type == coString ? m_opt.get_default_value<ConfigOptionString>()->value : m_opt.get_default_value<ConfigOptionStrings>()->get_at(m_opt_idx));
	wxColour clr(clr_str);
	if (clr_str.IsEmpty() || !clr.IsOk()) {
		clr = wxTransparentColour;
	}

	auto temp = new wxColourPickerCtrl(m_parent, wxID_ANY, clr, wxDefaultPosition, size);
    if (parent_is_custom_ctrl && m_opt.height < 0)
        opt_height = (double)temp->GetSize().GetHeight() / m_em_unit;
    temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    convert_to_picker_widget(temp);
    if (!wxOSX) temp->SetBackgroundStyle(wxBG_STYLE_PAINT);

	wxGetApp().UpdateDarkUI(temp->GetPickerCtrl());

	// 	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

	temp->Bind(wxEVT_COLOURPICKER_CHANGED, ([this,temp](wxCommandEvent e) {
        #ifdef __WXMSW__
            draw_bmp_btn(temp, temp->GetColour());
        #endif
        on_change_field();
    }), temp->GetId());

    // ORCA reset value to default on right click. previously no way to switch back on windows
    temp->GetPickerCtrl()->Bind(wxEVT_RIGHT_DOWN, [this, temp](wxMouseEvent e){
        #ifdef __WXMSW__
            temp->SetColour(wxTransparentColour);
            draw_bmp_btn(temp, wxTransparentColour);
        #else
            set_undef_value(temp);
        #endif
        on_change_field();
        e.Skip();
    });

	temp->SetToolTip(get_tooltip_text(clr_str));
}

void ColourPicker::set_undef_value(wxColourPickerCtrl* field)
{
    field->SetColour(wxTransparentColour);

    wxButton* btn = dynamic_cast<wxButton*>(field->GetPickerCtrl());
    if (!btn->GetBitmap().IsOk()) return;

    wxImage image(btn->GetBitmap().GetSize());
    image.InitAlpha();
    memset(image.GetAlpha(), 0, image.GetWidth() * image.GetHeight());
    wxBitmap   bmp(std::move(image));
    wxMemoryDC dc(bmp);
    if (!dc.IsOk()) return;
#ifdef __WXMSW__
    wxGCDC dc2(dc);
#else
    wxDC &dc2(dc);
#endif
    dc2.SetPen(wxPen("#F1754E", 1));

    const wxRect rect = wxRect(0, 0, bmp.GetWidth(), bmp.GetHeight());
    dc2.DrawLine(rect.GetLeftBottom(), rect.GetTopRight());

    dc.SelectObject(wxNullBitmap);
    btn->SetBitmapLabel(bmp);
}

// ORCA match style with button on windows
void ColourPicker::draw_bmp_btn(wxColourPickerCtrl* field, wxColour color)
{
    wxButton* btn = dynamic_cast<wxButton*>(field->GetPickerCtrl());

    if (!btn->GetBitmap().IsOk()) return;
    btn->SetWindowStyle(wxBORDER_NONE); // ORCA just in case to prevent any overflow
    btn->SetBackgroundColour(*wxWHITE);
    wxGetApp().UpdateDarkUI(btn);

    auto create_bitmap = [btn](const wxColour& picker_color,const wxColour& bg_color, bool focus) -> wxBitmap {
        wxSize  btn_sz = btn->GetSize();
        wxImage image(btn_sz);
        image.InitAlpha();
        memset(image.GetAlpha(), 0, image.GetWidth() * image.GetHeight());
        wxBitmap   bmp(std::move(image));
        wxMemoryDC dc(bmp);
        if (!dc.IsOk()) return bmp;
        wxGCDC dc2(dc); // just use wxGCDC since bitmap button only used for windows

        dc2.SetPen(focus ? wxPen(wxColour(StateColor::darkModeColorFor(wxColour("#FF7C3F"))), 1) : *wxTRANSPARENT_PEN);
        dc2.SetBrush(wxBrush(StateColor::darkModeColorFor(bg_color)));
        dc2.DrawRoundedRectangle(btn->GetRect(), btn->FromDIP(4));

        int padding = btn->FromDIP(5);
        dc2.SetPen(*wxTRANSPARENT_PEN);
        if (picker_color != wxTransparentColour){ // Draw color
            dc2.SetBrush(wxBrush(picker_color));
            dc2.DrawRectangle(wxRect(padding, padding, btn_sz.x - 2 * padding, btn_sz.y - 2 * padding));
        } else { // Draw Pick text
            // Label::Body_14 rendered much bolder with wxGCDC
            dc2.SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            wxString text    = _L("Pick") + " " + dots;
            wxSize   text_sz = dc2.GetTextExtent(text);
            dc2.SetTextForeground(StateColor::darkModeColorFor(wxColour("#262E30")));
            dc2.DrawText(text, (btn_sz.x - text_sz.x) / 2, (btn_sz.y - text_sz.y) / 2);
        }
        dc.SelectObject(wxNullBitmap);
        return bmp;
    };

    btn->SetBitmap(        create_bitmap(color, wxColour("#DFDFDF"), false)); // Normal
    btn->SetBitmapFocus(   create_bitmap(color, wxColour("#DFDFDF"), true )); // Focus
    btn->SetBitmapCurrent( create_bitmap(color, wxColour("#D4D4D4"), false)); // Hover
}

void ColourPicker::set_value(const boost::any& value, bool change_event)
{
    m_disable_change_event = !change_event;
    const wxString clr_str(boost::any_cast<wxString>(value));
    auto field = dynamic_cast<wxColourPickerCtrl*>(window);

    #ifdef __WXMSW__
        wxColour clr = (clr_str.IsEmpty() || !clr.IsOk()) ? wxTransparentColour : clr_str;
        field->SetColour(clr);
        draw_bmp_btn(field, clr);
    #else
        wxColour clr(clr_str);
        if (clr_str.IsEmpty() || !clr.IsOk())
            set_undef_value(field);
        else
            field->SetColour(clr);
    #endif

    m_disable_change_event = false;
}

boost::any& ColourPicker::get_value()
{
    save_colors_to_config();
	auto colour = static_cast<wxColourPickerCtrl*>(window)->GetColour();
    if (colour == wxTransparentColour)
        m_value = std::string("");
    else {
        m_value = encode_color(ColorRGB(colour.Red(), colour.Green(), colour.Blue()));
    }
	return m_value;
}

void ColourPicker::msw_rescale()
{
    Field::msw_rescale();

	wxColourPickerCtrl* field = dynamic_cast<wxColourPickerCtrl*>(window);
    auto size = wxSize(def_width_wider() * m_em_unit, -1); // ORCA match color picker width with parameters
    if (m_opt.height >= 0)
        size.SetHeight(m_opt.height * m_em_unit);
    else if (parent_is_custom_ctrl && opt_height > 0)
        size.SetHeight(lround(opt_height * m_em_unit));
    if (m_opt.width >= 0) size.SetWidth(m_opt.width * m_em_unit);
    if (parent_is_custom_ctrl)
        field->SetSize(size);
    else
        field->SetMinSize(size);

    #ifdef __WXMSW__
        draw_bmp_btn(field, field->GetColour());
    #else
        if (field->GetColour() == wxTransparentColour)
            set_undef_value(field);
    #endif

}

void ColourPicker::sys_color_changed()
{
#ifdef _WIN32
    if (wxWindow* win = this->getWindow())
        if (wxColourPickerCtrl* picker = dynamic_cast<wxColourPickerCtrl*>(win)){
            wxGetApp().UpdateDarkUI(picker->GetPickerCtrl(), true);
            draw_bmp_btn(picker, picker->GetColour());
        }
#endif
}

void ColourPicker::on_button_click(wxCommandEvent &event) {
#if !defined(__linux__) && !defined(__LINUX__)
    if (m_clrData) {
        std::vector<std::string> colors = wxGetApp().app_config->get_custom_color_from_config();
        for (int i = 0; i < colors.size(); i++) {
            m_clrData->SetCustomColour(i, string_to_wxColor(colors[i]));
        }
    }
    m_picker_widget->OnButtonClick(event);
#endif
}

void ColourPicker::convert_to_picker_widget(wxColourPickerCtrl *widget)
{
#if !defined(__linux__) && !defined(__LINUX__)
    m_picker_widget = dynamic_cast<wxColourPickerWidget*>(widget->GetPickerCtrl());
    if (m_picker_widget) {
        m_picker_widget->Bind(wxEVT_BUTTON, &ColourPicker::on_button_click, this);
        m_clrData = m_picker_widget->GetColourData();
    }
#endif
}

void ColourPicker::save_colors_to_config() {
#if !defined(__linux__) && !defined(__LINUX__)
    if (m_clrData) {
        std::vector<std::string> colors;
        if (colors.size() != CUSTOM_COLOR_COUNT) {
            colors.resize(CUSTOM_COLOR_COUNT);
        }
        for (int i = 0; i < CUSTOM_COLOR_COUNT; i++) {
            colors[i] = color_to_string(m_clrData->GetCustomColour(i));
        }
        wxGetApp().app_config->save_custom_color_to_config(colors);
    }
#endif
}

void PointCtrl::BUILD()
{
	auto temp = new wxBoxSizer(wxHORIZONTAL);
	m_combine_side_text = true; // Prefer using side text in input box

    //const wxSize field_size(4 * m_em_unit, -1);
    const wxSize  field_size((m_opt.width >= 0 ? m_opt.width : def_width_wider()) * m_em_unit, -1); // ORCA match width with other components
    Slic3r::Vec2d default_pt;
    if(m_opt.type == coPoints)
	    default_pt = m_opt.get_default_value<ConfigOptionPoints>()->values.at(0);
    else
        default_pt = m_opt.get_default_value<ConfigOptionPoint>()->value;
	double val = default_pt(0);
	wxString X = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None);
	val = default_pt(1);
	wxString Y = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None);

	long style = wxTE_PROCESS_ENTER;
//#ifdef _WIN32
//	style |= wxBORDER_SIMPLE;
//#endif
    // ORCA add icons to point control boxes instead of using text for X / Y
    x_input = new ::TextInput(m_parent, X, m_opt.sidetext, "inputbox_x", wxDefaultPosition, field_size, style);
    y_input = new ::TextInput(m_parent, Y, m_opt.sidetext, "inputbox_y", wxDefaultPosition, field_size, style);
    x_textctrl = x_input->GetTextCtrl();
    y_textctrl = y_input->GetTextCtrl();
    if (parent_is_custom_ctrl && m_opt.height < 0)
        opt_height = (double)x_textctrl->GetSize().GetHeight() / m_em_unit;

    x_input->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    x_input->SetBackgroundStyle(wxBG_STYLE_PAINT);
    y_input->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    y_input->SetBackgroundStyle(wxBG_STYLE_PAINT);

	//auto static_text_x = new wxStaticText(m_parent, wxID_ANY, "x : ");
	//auto static_text_y = new wxStaticText(m_parent, wxID_ANY, "   y : ");
	//static_text_x->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	//static_text_x->SetBackgroundStyle(wxBG_STYLE_PAINT);
	//static_text_y->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	//static_text_y->SetBackgroundStyle(wxBG_STYLE_PAINT);

	wxGetApp().UpdateDarkUI(x_input);
	wxGetApp().UpdateDarkUI(y_input);
	//wxGetApp().UpdateDarkUI(static_text_x, false, true);
	//wxGetApp().UpdateDarkUI(static_text_y, false, true);

	//temp->Add(static_text_x, 0, wxALIGN_CENTER_VERTICAL, 0);
	temp->Add(x_input);
	//temp->Add(static_text_y, 0, wxALIGN_CENTER_VERTICAL, 0);
	temp->Add(y_input);

    x_textctrl->Bind(wxEVT_TEXT_ENTER, ([this](wxCommandEvent e) { propagate_value(x_textctrl); }), x_textctrl->GetId());
	y_textctrl->Bind(wxEVT_TEXT_ENTER, ([this](wxCommandEvent e) { propagate_value(y_textctrl); }), y_textctrl->GetId());

    x_textctrl->Bind(wxEVT_KILL_FOCUS, ([this](wxEvent& e) { e.Skip(); propagate_value(x_textctrl); }), x_textctrl->GetId());
    y_textctrl->Bind(wxEVT_KILL_FOCUS, ([this](wxEvent& e) { e.Skip(); propagate_value(y_textctrl); }), y_textctrl->GetId());

	// 	// recast as a wxWindow to fit the calling convention
    window = dynamic_cast<wxWindow*>(x_input);
	sizer = dynamic_cast<wxSizer*>(temp);

	x_textctrl->SetToolTip(get_tooltip_text(X+", "+Y));
	y_textctrl->SetToolTip(get_tooltip_text(X+", "+Y));
}

void PointCtrl::msw_rescale()
{
    Field::msw_rescale();

    //wxSize field_size(4 * m_em_unit, -1);
    wxSize  field_size((m_opt.width >= 0 ? m_opt.width : def_width_wider()) * m_em_unit, -1); // ORCA match width with other components

    if (parent_is_custom_ctrl) {
        field_size.SetHeight(lround(opt_height * m_em_unit));
        x_input->SetSize(field_size);
        y_input->SetSize(field_size);
    }
    else {
        x_input->SetMinSize(field_size);
        y_input->SetMinSize(field_size);
    }
}

void PointCtrl::sys_color_changed()
{
#ifdef _WIN32
    for (wxSizerItem* item: sizer->GetChildren())
        if (item->IsWindow())
            wxGetApp().UpdateDarkUI(item->GetWindow());
#endif
}

bool PointCtrl::value_was_changed(wxTextCtrl* win)
{
	if (m_value.empty())
		return true;

	boost::any val = m_value;
	// update m_value!
	get_value();

	return boost::any_cast<Vec2d>(m_value) != boost::any_cast<Vec2d>(val);
}

void PointCtrl::propagate_value(wxTextCtrl* win)
{
    if (win->GetValue().empty())
        on_kill_focus();
	else if (value_was_changed(win))
        on_change_field();
}

void PointCtrl::set_value(const Vec2d& value, bool change_event)
{
	m_disable_change_event = !change_event;

	double val = value(0);
	x_textctrl->SetValue(val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None));
	val = value(1);
	y_textctrl->SetValue(val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None));

	m_disable_change_event = false;
}

void PointCtrl::set_value(const boost::any& value, bool change_event)
{
	Vec2d pt(Vec2d::Zero());
    const Vec2d* ptf = boost::any_cast<Vec2d>(&value);
    if (!ptf) {
        if (m_opt.type == coPoint) {
            ConfigOptionPoint* pts = boost::any_cast<ConfigOptionPoint*>(value);
            pt = pts->value;
        }
        else {
            ConfigOptionPoints* pts = boost::any_cast<ConfigOptionPoints*>(value);
            pt = pts->values.at(0);
        }
    }
    else
        pt = *ptf;
	set_value(pt, change_event);
}

boost::any& PointCtrl::get_value()
{
	double x, y;
	if (!x_textctrl->GetValue().ToDouble(&x) ||
		!y_textctrl->GetValue().ToDouble(&y))
	{
		set_value(m_value.empty() ? Vec2d(0.0, 0.0) : m_value, true);
        show_error(m_parent, _L("Invalid numeric."));
	}
	else
	if (m_opt.min > x || x > m_opt.max ||
		m_opt.min > y || y > m_opt.max)
	{
		if (m_opt.min > x) x = m_opt.min;
		if (x > m_opt.max) x = m_opt.max;
		if (m_opt.min > y) y = m_opt.min;
		if (y > m_opt.max) y = m_opt.max;
		set_value(Vec2d(x, y), true);

		show_error(m_parent, _L("Value is out of range."));
	}

	return m_value = Vec2d(x, y);
}

#pragma region Phrozen LCD Parameter
void DualFloatField::BUILD()
{
	auto temp = new wxBoxSizer(wxHORIZONTAL);
	m_combine_side_text = true;

	// Per-input width: first from dual_float_width (or width or default), second from dual_float_width_second (or same as first)
	static const int dual_float_em_width = 8;
	const int w1_em = m_opt.dual_float_width >= 0 ? m_opt.dual_float_width : (m_opt.width >= 0 ? m_opt.width : dual_float_em_width);
	const int w2_em = m_opt.dual_float_width_second >= 0 ? m_opt.dual_float_width_second : w1_em;
	const wxSize field_size1(w1_em * m_em_unit, -1);
	const wxSize field_size2(w2_em * m_em_unit, -1);
	std::vector<double> default_vals;
	if (m_opt.get_default_value<ConfigOptionFloats>()->values.size() >= 2) {
		default_vals = m_opt.get_default_value<ConfigOptionFloats>()->values;
	} else {
		default_vals = { 0.2, 0.2 };
	}
	wxString v1 = double_to_string(default_vals[0]);
	wxString v2 = double_to_string(default_vals[1]);

	long style = wxTE_PROCESS_ENTER;
	m_input1 = new ::TextInput(m_parent, v1, "", "", wxDefaultPosition, field_size1, style);
	m_input2 = new ::TextInput(m_parent, v2, _L(m_opt.sidetext), "", wxDefaultPosition, field_size2, style);
	m_text1 = m_input1->GetTextCtrl();
	m_text2 = m_input2->GetTextCtrl();
	if (parent_is_custom_ctrl && m_opt.height < 0)
		opt_height = (double)m_text1->GetSize().GetHeight() / m_em_unit;

	m_input1->SetFont(wxGetApp().normal_font());
	m_input1->SetBackgroundStyle(wxBG_STYLE_PAINT);
	m_input2->SetFont(wxGetApp().normal_font());
	m_input2->SetBackgroundStyle(wxBG_STYLE_PAINT);
	wxGetApp().UpdateDarkUI(m_input1);
	wxGetApp().UpdateDarkUI(m_input2);

	wxStaticText* plus_lbl = new wxStaticText(m_parent, wxID_ANY, " + ", wxDefaultPosition, wxDefaultSize);
	plus_lbl->SetFont(wxGetApp().normal_font());
	plus_lbl->SetBackgroundStyle(wxBG_STYLE_PAINT);

	temp->Add(m_input1, 0, wxALIGN_CENTER_VERTICAL, 0);
	temp->Add(plus_lbl, 0, wxALIGN_CENTER_VERTICAL, 2);
	temp->Add(m_input2, 0, wxALIGN_CENTER_VERTICAL, 0);

	m_text1->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent& e) { propagate_value(); });
	m_text2->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent& e) { propagate_value(); });
	m_text1->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { e.Skip(); propagate_value(); });
	m_text2->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { e.Skip(); propagate_value(); });

	sizer = temp;
	window = m_input1;
}

void DualFloatField::propagate_value()
{
	get_value();
	on_change_field();
}

void DualFloatField::set_value(const boost::any& value, bool change_event)
{
	m_disable_change_event = !change_event;
	std::vector<double> vals;
	try {
		vals = boost::any_cast<std::vector<double>>(value);
	} catch (...) {
		vals = { 0.2, 0.2 };
	}
	if (vals.size() < 2)
		vals.resize(2, 0.2);
	m_text1->SetValue(double_to_string(vals[0]));
	m_text2->SetValue(double_to_string(vals[1]));
	m_disable_change_event = false;
}

boost::any& DualFloatField::get_value()
{
	double a = 0.2, b = 0.2;
	if (!m_text1->GetValue().ToDouble(&a) || !m_text2->GetValue().ToDouble(&b)) {
		if (!m_value.empty()) {
			try {
				set_value(boost::any_cast<std::vector<double>>(m_value), true);
			} catch (...) {
				show_error(m_parent, _L("Invalid numeric."));
			}
		} else
			show_error(m_parent, _L("Invalid numeric."));
		return m_value;
	}
	if (m_opt.min > a || a > m_opt.max || m_opt.min > b || b > m_opt.max) {
		const double min_val = (double)m_opt.min;
		const double max_val = (double)m_opt.max;
		a = std::max(min_val, std::min(max_val, a));
		b = std::max(min_val, std::min(max_val, b));
		set_value(std::vector<double>{ a, b }, true);
		show_error(m_parent, _L("Value is out of range."));
	}
	return m_value = std::vector<double>{ a, b };
}

void DualFloatField::msw_rescale()
{
	Field::msw_rescale();
	static const int dual_float_em_width = 8;
	const int w1_em = m_opt.dual_float_width >= 0 ? m_opt.dual_float_width : (m_opt.width >= 0 ? m_opt.width : dual_float_em_width);
	const int w2_em = m_opt.dual_float_width_second >= 0 ? m_opt.dual_float_width_second : w1_em;
	wxSize size1(w1_em * m_em_unit, -1);
	wxSize size2(w2_em * m_em_unit, -1);
	if (parent_is_custom_ctrl && m_opt.height >= 0) {
		const int h = lround(opt_height * m_em_unit);
		size1.SetHeight(h);
		size2.SetHeight(h);
	}
	m_input1->SetMinSize(size1);
	m_input2->SetMinSize(size2);
}

void DualFloatField::enable()
{
	m_input1->Enable();
	m_input2->Enable();
}

void DualFloatField::disable()
{
	m_input1->Disable();
	m_input2->Disable();
}

namespace {
constexpr int GRAYSCALE_RANGE_MIN = 0;
constexpr int GRAYSCALE_RANGE_MAX = 255;
constexpr int TICK_COUNT = 8;
constexpr int THUMB_HALF_W = 6;
constexpr int THUMB_H = 10;
constexpr int BAR_HEIGHT = 12;
constexpr int TICKS_TOP_OFFSET = 2;
constexpr int THUMB_GAP = 4; // gap between gradient bar and slider thumbs so they don't overlap

static int grayscale_text_to_int(wxTextCtrl* t, int def) {
	if (!t) return def;
	long v = 0;
	if (t->GetValue().ToLong(&v))
		return std::clamp(static_cast<int>(v), GRAYSCALE_RANGE_MIN, GRAYSCALE_RANGE_MAX);
	return def;
}

static void grayscale_int_to_text(wxTextCtrl* t, int v) {
	if (t) t->SetValue(wxString::Format("%d", std::clamp(v, GRAYSCALE_RANGE_MIN, GRAYSCALE_RANGE_MAX)));
}

class GrayscaleRangePanel : public wxPanel {
public:
	GrayscaleRangePanel(wxWindow* parent, wxTextCtrl* text_min, wxTextCtrl* text_max,
		std::function<void()> on_change)
		: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
		, m_text_min(text_min)
		, m_text_max(text_max)
		, m_on_change(std::move(on_change))
		, m_dragging(-1)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(wxSize(200 + 2 * THUMB_HALF_W, 64));
		Bind(wxEVT_PAINT, &GrayscaleRangePanel::OnPaint, this);
		Bind(wxEVT_LEFT_DOWN, &GrayscaleRangePanel::OnLeftDown, this);
		Bind(wxEVT_LEFT_UP, &GrayscaleRangePanel::OnLeftUp, this);
		Bind(wxEVT_MOTION, &GrayscaleRangePanel::OnMotion, this);
		Bind(wxEVT_LEAVE_WINDOW, &GrayscaleRangePanel::OnLeave, this);
	}

	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(GetBackgroundColour());
		dc.Clear();
		wxSize sz = GetClientSize();
		// Horizontal padding so left/right thumbs at 0/255 are not clipped
		const int bar_x = 2 + THUMB_HALF_W;
		const int bar_w = std::max(1, sz.x - 4 - 2 * THUMB_HALF_W);
		const int bar_y = TICKS_TOP_OFFSET + dc.GetCharHeight() + 2;

		wxFont font = dc.GetFont();
		font.SetPointSize(std::max(7, font.GetPointSize() - 1));
		dc.SetFont(font);
		for (int i = 1; i <= TICK_COUNT; ++i) {
			int x = bar_x + (i - 1) * bar_w / (TICK_COUNT - 1) - dc.GetTextExtent(wxString() << i).x / 2;
			dc.DrawText(wxString() << i, wxPoint(std::max(0, x), 0));
		}

		wxRect barRect(bar_x, bar_y, bar_w, BAR_HEIGHT);
		dc.GradientFillLinear(barRect, wxColour(0, 0, 0), wxColour(255, 255, 255), wxEAST);
		dc.SetPen(wxColour(0x73, 0x73, 0x73));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(barRect);

		int min_val = grayscale_text_to_int(m_text_min, GRAYSCALE_RANGE_MIN);
		int max_val = grayscale_text_to_int(m_text_max, GRAYSCALE_RANGE_MAX);
		min_val = std::clamp(min_val, GRAYSCALE_RANGE_MIN, GRAYSCALE_RANGE_MAX);
		max_val = std::clamp(max_val, GRAYSCALE_RANGE_MIN, GRAYSCALE_RANGE_MAX);

		auto pos_to_x = [bar_x, bar_w](int val) {
			return bar_x + (val - GRAYSCALE_RANGE_MIN) * bar_w / std::max(1, GRAYSCALE_RANGE_MAX - GRAYSCALE_RANGE_MIN);
		};

		// Draw thumbs below the gradient bar, tip pointing upward
		const int thumb_center_y = bar_y + BAR_HEIGHT + THUMB_GAP + THUMB_H / 2;
		const int thumb_tip_y = thumb_center_y - THUMB_H / 2;   // tip at top
		const int thumb_base_y = thumb_center_y + THUMB_H / 2;   // base at bottom
		int x_min = pos_to_x(min_val);
		int x_max = pos_to_x(max_val);
		wxPoint tri_min[] = { {x_min, thumb_tip_y}, {x_min - THUMB_HALF_W, thumb_base_y}, {x_min + THUMB_HALF_W, thumb_base_y} };
		wxPoint tri_max[] = { {x_max, thumb_tip_y}, {x_max - THUMB_HALF_W, thumb_base_y}, {x_max + THUMB_HALF_W, thumb_base_y} };
		const wxColour thumb_stroke(0x73, 0x73, 0x73); // #737373 to match SVG
		dc.SetPen(thumb_stroke);
		dc.SetBrush(*wxBLACK_BRUSH);
		dc.DrawPolygon(3, tri_min);
		dc.SetBrush(*wxWHITE_BRUSH);
		dc.DrawPolygon(3, tri_max);
	}

	void OnLeftDown(wxMouseEvent& e) {
		int hit = hit_test(e.GetPosition());
		if (hit >= 0)
			m_dragging = hit;
	}

	void OnLeftUp(wxMouseEvent& e) { (void)e; m_dragging = -1; }
	void OnLeave(wxMouseEvent& e) { (void)e; m_dragging = -1; }

	void OnMotion(wxMouseEvent& e) {
		if (m_dragging < 0) return;
		wxSize sz = GetClientSize();
		const int bar_x = 2 + THUMB_HALF_W;
		const int bar_w = std::max(1, sz.x - 4 - 2 * THUMB_HALF_W);
		int x = e.GetX() - bar_x;
		int val = GRAYSCALE_RANGE_MIN + (GRAYSCALE_RANGE_MAX - GRAYSCALE_RANGE_MIN) * x / std::max(1, bar_w);
		val = std::clamp(val, GRAYSCALE_RANGE_MIN, GRAYSCALE_RANGE_MAX);
		bool value_changed = false;
		if (m_dragging == 0 && m_text_min) {
			int max_val = grayscale_text_to_int(m_text_max, GRAYSCALE_RANGE_MAX);
			int new_min = std::min(val, max_val);
			if (grayscale_text_to_int(m_text_min, GRAYSCALE_RANGE_MIN) != new_min) {
				grayscale_int_to_text(m_text_min, new_min);
				value_changed = true;
			}
		} else if (m_dragging == 1 && m_text_max) {
			int min_val = grayscale_text_to_int(m_text_min, GRAYSCALE_RANGE_MIN);
			int new_max = std::max(val, min_val);
			if (grayscale_text_to_int(m_text_max, GRAYSCALE_RANGE_MAX) != new_max) {
				grayscale_int_to_text(m_text_max, new_max);
				value_changed = true;
			}
		}
		if (value_changed && m_on_change)
			m_on_change();
		// Throttle repaints during drag to ~60fps for smoother feel
		wxLongLong now = wxGetLocalTimeMillis();
		if (value_changed || (now - m_last_drag_refresh).ToLong() >= 16) {
			m_last_drag_refresh = now;
			Refresh();
		}
	}

private:
	int hit_test(const wxPoint& pt) {
		wxSize sz = GetClientSize();
		const int bar_x = 2 + THUMB_HALF_W;
		const int bar_w = std::max(1, sz.x - 4 - 2 * THUMB_HALF_W);
		const int bar_y = TICKS_TOP_OFFSET + 2 + GetCharHeight() + 2;
		const int thumb_center_y = bar_y + BAR_HEIGHT + THUMB_GAP + THUMB_H / 2;
		const int thumb_top = thumb_center_y - THUMB_H / 2;
		const int thumb_bottom = thumb_center_y + THUMB_H / 2;
		int min_val = grayscale_text_to_int(m_text_min, 0);
		int max_val = grayscale_text_to_int(m_text_max, 255);
		auto pos_to_x = [bar_x, bar_w](int val) {
			return bar_x + (val - GRAYSCALE_RANGE_MIN) * bar_w / std::max(1, GRAYSCALE_RANGE_MAX - GRAYSCALE_RANGE_MIN);
		};
		int x_min = pos_to_x(min_val);
		int x_max = pos_to_x(max_val);
		if (pt.y >= thumb_top && pt.y <= thumb_bottom) {
			if (std::abs(pt.x - x_min) <= THUMB_HALF_W + 2) return 0;
			if (std::abs(pt.x - x_max) <= THUMB_HALF_W + 2) return 1;
		}
		return -1;
	}

	wxTextCtrl* m_text_min;
	wxTextCtrl* m_text_max;
	std::function<void()> m_on_change;
	int m_dragging{ -1 };
	wxLongLong m_last_drag_refresh{ 0 };
};
}

void GrayScaleLevelRangeCtrl::BUILD()
{
	const int em = m_em_unit;
	const int text_w = 5 * em;

	std::vector<int> default_vals = { 0, 255 };
	if (const ConfigOptionInts* def = m_opt.get_default_value<ConfigOptionInts>()) {
		if (def->values.size() >= 2)
			default_vals = { def->values[0], def->values[1] };
	}
	default_vals[0] = std::clamp(default_vals[0], GRAYSCALE_RANGE_MIN, GRAYSCALE_RANGE_MAX);
	default_vals[1] = std::clamp(default_vals[1], GRAYSCALE_RANGE_MIN, GRAYSCALE_RANGE_MAX);

	m_container = new wxPanel(m_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	m_container->SetBackgroundColour(m_parent->GetBackgroundColour());

	m_text_min = new wxTextCtrl(m_container, wxID_ANY, wxString::Format("%d", default_vals[0]),
		wxDefaultPosition, wxSize(text_w, wxDefaultCoord), wxTE_PROCESS_ENTER);
	m_text_max = new wxTextCtrl(m_container, wxID_ANY, wxString::Format("%d", default_vals[1]),
		wxDefaultPosition, wxSize(text_w, wxDefaultCoord), wxTE_PROCESS_ENTER);
	m_text_min->SetFont(wxGetApp().normal_font());
	m_text_max->SetFont(wxGetApp().normal_font());
	wxGetApp().UpdateDarkUI(m_text_min);
	wxGetApp().UpdateDarkUI(m_text_max);

	m_gradient_panel = new GrayscaleRangePanel(m_container, m_text_min, m_text_max, [this]() {
		if (!m_disable_change_event) { get_value(); on_change_field(); }
	});
	m_gradient_panel->SetBackgroundColour(m_container->GetBackgroundColour());

	auto* main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(m_gradient_panel, 0, wxEXPAND | wxBOTTOM, 4);

	auto* row_sizer = new wxBoxSizer(wxHORIZONTAL);
	row_sizer->Add(m_text_min, 0, wxALIGN_CENTER_VERTICAL, 0);
	row_sizer->AddStretchSpacer(1);
	row_sizer->Add(m_text_max, 0, wxALIGN_CENTER_VERTICAL, 0);

	main_sizer->Add(row_sizer, 0, wxEXPAND, 0);
	m_container->SetSizer(main_sizer);
	wxSize min_sz = main_sizer->GetMinSize();
	m_container->SetMinSize(min_sz);
	m_container->SetSize(min_sz);

	sizer = nullptr;
	window = m_container;

	auto sync_from_text = [this]() {
		if (m_disable_change_event) return;
		get_value();
		on_change_field();
		if (m_gradient_panel) m_gradient_panel->Refresh();
	};
	m_text_min->Bind(wxEVT_TEXT_ENTER, [sync_from_text](wxCommandEvent&) { sync_from_text(); });
	m_text_max->Bind(wxEVT_TEXT_ENTER, [sync_from_text](wxCommandEvent&) { sync_from_text(); });
	m_text_min->Bind(wxEVT_KILL_FOCUS, [sync_from_text](wxFocusEvent& e) { e.Skip(); sync_from_text(); });
	m_text_max->Bind(wxEVT_KILL_FOCUS, [sync_from_text](wxFocusEvent& e) { e.Skip(); sync_from_text(); });
}

void GrayScaleLevelRangeCtrl::set_value(const boost::any& value, bool change_event)
{
	m_disable_change_event = !change_event;
	std::vector<int> vals;
	try {
		vals = boost::any_cast<std::vector<int>>(value);
	} catch (...) {
		vals = { 0, 255 };
	}
	if (vals.size() < 2) vals.resize(2, vals.empty() ? 0 : vals[0]);
	int a = std::clamp(vals[0], GRAYSCALE_RANGE_MIN, GRAYSCALE_RANGE_MAX);
	int b = std::clamp(vals[1], GRAYSCALE_RANGE_MIN, GRAYSCALE_RANGE_MAX);
	if (a > b) std::swap(a, b);
	grayscale_int_to_text(m_text_min, a);
	grayscale_int_to_text(m_text_max, b);
	if (m_gradient_panel) m_gradient_panel->Refresh();
	m_disable_change_event = false;
}

boost::any& GrayScaleLevelRangeCtrl::get_value()
{
	int a = grayscale_text_to_int(m_text_min, GRAYSCALE_RANGE_MIN);
	int b = grayscale_text_to_int(m_text_max, GRAYSCALE_RANGE_MAX);
	a = std::clamp(a, GRAYSCALE_RANGE_MIN, GRAYSCALE_RANGE_MAX);
	b = std::clamp(b, GRAYSCALE_RANGE_MIN, GRAYSCALE_RANGE_MAX);
	if (a > b) std::swap(a, b);
	grayscale_int_to_text(m_text_min, a);
	grayscale_int_to_text(m_text_max, b);
	return m_value = boost::any(std::vector<int>{ a, b });
}

void GrayScaleLevelRangeCtrl::msw_rescale()
{
	Field::msw_rescale();
}

void GrayScaleLevelRangeCtrl::enable()
{
	if (m_text_min) m_text_min->Enable();
	if (m_text_max) m_text_max->Enable();
	if (m_gradient_panel) m_gradient_panel->Enable();
}

void GrayScaleLevelRangeCtrl::disable()
{
	if (m_text_min) m_text_min->Disable();
	if (m_text_max) m_text_max->Disable();
	if (m_gradient_panel) m_gradient_panel->Disable();
}
#pragma endregion

void StaticText::BUILD()
{
	auto size = wxSize(wxDefaultSize);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

    const wxString legend = from_u8(m_opt.get_default_value<ConfigOptionString>()->value);
    auto temp = new wxStaticText(m_parent, wxID_ANY, legend, wxDefaultPosition, size, wxST_ELLIPSIZE_MIDDLE);
	temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	temp->SetBackgroundStyle(wxBG_STYLE_PAINT);
    temp->SetFont(wxGetApp().bold_font());

	wxGetApp().UpdateDarkUI(temp);

	// 	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

	temp->SetToolTip(get_tooltip_text(legend));
}

void StaticText::msw_rescale()
{
    Field::msw_rescale();

    auto size = wxSize(wxDefaultSize);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

    if (size != wxDefaultSize)
    {
        wxStaticText* field = dynamic_cast<wxStaticText*>(window);
        field->SetSize(size);
        field->SetMinSize(size);
    }
}

void SliderCtrl::BUILD()
{
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	auto temp = new wxBoxSizer(wxHORIZONTAL);

	auto def_val = m_opt.get_default_value<ConfigOptionInt>()->value;
	auto min = m_opt.min == INT_MIN ? 0 : m_opt.min;
	auto max = m_opt.max == INT_MAX ? 100 : m_opt.max;

	m_slider = new wxSlider(m_parent, wxID_ANY, def_val * m_scale,
							min * m_scale, max * m_scale,
							wxDefaultPosition, size);
	m_slider->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	m_slider->SetBackgroundStyle(wxBG_STYLE_PAINT);
 	wxSize field_size(40, -1);

	m_textctrl = new wxTextCtrl(m_parent, wxID_ANY, wxString::Format("%d", m_slider->GetValue()/m_scale),
								wxDefaultPosition, field_size);
	m_textctrl->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	m_textctrl->SetBackgroundStyle(wxBG_STYLE_PAINT);

	temp->Add(m_slider, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL, 0);
	temp->Add(m_textctrl, 0, wxALIGN_CENTER_VERTICAL, 0);

	m_slider->Bind(wxEVT_SLIDER, ([this](wxCommandEvent e) {
		if (!m_disable_change_event) {
			int val = boost::any_cast<int>(get_value());
			m_textctrl->SetLabel(wxString::Format("%d", val));
			on_change_field();
		}
	}), m_slider->GetId());

	m_textctrl->Bind(wxEVT_TEXT, ([this](wxCommandEvent e) {
		std::string value = e.GetString().utf8_str().data();
		if (is_matched(value, "^-?\\d+(\\.\\d*)?$")) {
			m_disable_change_event = true;
			m_slider->SetValue(stoi(value)*m_scale);
			m_disable_change_event = false;
			on_change_field();
		}
	}), m_textctrl->GetId());

	m_sizer = dynamic_cast<wxSizer*>(temp);
}

void SliderCtrl::set_value(const boost::any& value, bool change_event)
{
	m_disable_change_event = !change_event;

	m_slider->SetValue(boost::any_cast<int>(value)*m_scale);
	int val = boost::any_cast<int>(get_value());
	m_textctrl->SetLabel(wxString::Format("%d", val));

	m_disable_change_event = false;
}

boost::any& SliderCtrl::get_value()
{
// 	int ret_val;
// 	x_textctrl->GetValue().ToDouble(&val);
	return m_value = int(m_slider->GetValue()/m_scale);
}


}} // Slic3r
