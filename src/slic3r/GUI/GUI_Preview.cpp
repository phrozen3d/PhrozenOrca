//#include "stdlib.h"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Layer.hpp"
#include "IMSlider.hpp"
#include "GUI_Preview.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "3DScene.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "OpenGLManager.hpp"
#include "GLCanvas3D.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"

#include <wx/listbook.h>
#include <wx/notebook.h>
#include <wx/glcanvas.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/combo.h>
#include <wx/combobox.h>
#include <wx/checkbox.h>

// this include must follow the wxWidgets ones or it won't compile on Windows -> see http://trac.wxwidgets.org/ticket/2421
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "NotificationManager.hpp"
#include "SLASlice2DCanvas.hpp"

#ifdef _WIN32
#include "BitmapComboBox.hpp"
#endif

namespace Slic3r {
namespace GUI {

namespace {

wxColour sla_preview_splitter_line_colour()
{
    return wxGetApp().dark_mode() ? wxColour(78, 78, 84)
                                  : wxColour(200, 202, 208); // slightly darker than gutter for contrast
}

} // namespace

View3D::View3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : m_canvas_widget(nullptr)
    , m_canvas(nullptr)
{
    init(parent, bed, model, config, process);
}

View3D::~View3D()
{
    delete m_canvas;
    delete m_canvas_widget;
}

bool View3D::init(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this);
    if (m_canvas_widget == nullptr)
        return false;

    m_canvas = new GLCanvas3D(m_canvas_widget, bed);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));

    m_canvas->allow_multisample(OpenGLManager::can_multisample());
    // XXX: If have OpenGL
    m_canvas->enable_picking(true);
    m_canvas->get_selection().set_mode(Selection::Instance);
    m_canvas->enable_moving(true);
    // XXX: more config from 3D.pm
    m_canvas->set_model(model);
    m_canvas->set_process(process);
    m_canvas->set_type(GLCanvas3D::ECanvasType::CanvasView3D);
    m_canvas->set_config(config);
    m_canvas->enable_gizmos(true);
    m_canvas->enable_selection(true);
    m_canvas->enable_main_toolbar(true);
    //BBS: GUI refactor: GLToolbar
    m_canvas->enable_select_plate_toolbar(false);
    m_canvas->enable_assemble_view_toolbar(true);
    m_canvas->enable_separator_toolbar(true);
    m_canvas->enable_labels(true);
    m_canvas->enable_slope(true);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    return true;
}

void View3D::set_as_dirty()
{
    if (m_canvas != nullptr) {
        m_canvas->set_as_dirty();
    }
}

void View3D::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void View3D::plates_count_changed()
{
    if (m_canvas != nullptr)
        m_canvas->plates_count_changed();
}

void View3D::select_view(const std::string& direction)
{
    if (m_canvas != nullptr)
        m_canvas->select_view(direction);
}

//BBS
void View3D::select_curr_plate_all()
{
    if (m_canvas != nullptr)
        m_canvas->select_curr_plate_all();
}

void View3D::select_object_from_idx(std::vector<int>& object_idxs) {
    if (m_canvas != nullptr)
        m_canvas->select_object_from_idx(object_idxs);
}

//BBS
void View3D::remove_curr_plate_all()
{
    if (m_canvas != nullptr)
        m_canvas->remove_curr_plate_all();
}

void View3D::select_all()
{
    if (m_canvas != nullptr)
        m_canvas->select_all();
}

void View3D::deselect_all()
{
    if (m_canvas != nullptr)
        m_canvas->deselect_all();
}

void View3D::exit_gizmo()
{
    if (m_canvas != nullptr)
        m_canvas->exit_gizmo();
}

void View3D::delete_selected()
{
    if (m_canvas != nullptr)
        m_canvas->delete_selected();
}

void View3D::center_selected()
{
    if (m_canvas != nullptr)
        m_canvas->do_center();
}

void View3D::drop_selected()
{
    if (m_canvas != nullptr)
        m_canvas->do_drop();
}

void View3D::center_selected_plate(const int plate_idx) {
    if (m_canvas != nullptr)
        m_canvas->do_center_plate(plate_idx);
}

void View3D::mirror_selection(Axis axis)
{
    if (m_canvas != nullptr)
        m_canvas->mirror_selection(axis);
}

bool View3D::is_layers_editing_enabled() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_enabled() : false;
}

bool View3D::is_layers_editing_allowed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_allowed() : false;
}

void View3D::enable_layers_editing(bool enable)
{
    if (m_canvas != nullptr)
        m_canvas->enable_layers_editing(enable);
}

bool View3D::is_dragging() const
{
    return (m_canvas != nullptr) ? m_canvas->is_dragging() : false;
}

bool View3D::is_reload_delayed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_reload_delayed() : false;
}

void View3D::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if (m_canvas != nullptr)
        m_canvas->reload_scene(refresh_immediately, force_full_scene_refresh);
}

void View3D::render()
{
    if (m_canvas != nullptr)
        //m_canvas->render();
        m_canvas->set_as_dirty();
}

Preview::Preview(
    wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config,
    BackgroundSlicingProcess* process, GCodeProcessorResult* gcode_result, std::function<void()> schedule_background_process_func)
    : m_config(config)
    , m_process(process)
    , m_gcode_result(gcode_result)
    , m_schedule_background_process(schedule_background_process_func)
{
    if (init(parent, bed, model))
        load_print();
}

void Preview::update_gcode_result(GCodeProcessorResult* gcode_result)
{
    m_gcode_result = gcode_result;

    return;
}

bool Preview::init(wxWindow* parent, Bed3D& bed, Model* model)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    // to match the background of the sliders
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(this);
#else
    SetBackgroundColour(GetParent()->GetBackgroundColour());
#endif // _WIN32

    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this);
    if (m_canvas_widget == nullptr)
        return false;

    m_sla_2d_canvas = new SLASlice2DCanvas(this);
    m_sla_preview_splitter = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_sla_preview_splitter->SetBackgroundStyle(wxBG_STYLE_COLOUR);
    m_sla_preview_splitter->SetBackgroundColour(sla_preview_splitter_line_colour());
    {
        const int split_w = this->FromDIP(3); // must use wxWindow context (not file-scope helper)
        m_sla_preview_splitter->SetMinSize(wxSize(split_w, -1));
        m_sla_preview_splitter->SetMaxSize(wxSize(split_w, -1));
    }

    m_canvas = new GLCanvas3D(m_canvas_widget, bed);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));
    m_canvas->allow_multisample(OpenGLManager::can_multisample());
    m_canvas->set_config(m_config);
    m_canvas->set_model(model);
    m_canvas->set_process(m_process);
    m_canvas->set_type(GLCanvas3D::ECanvasType::CanvasPreview);
    m_canvas->enable_legend_texture(true);
    m_canvas->enable_dynamic_background(true);
    //BBS: GUI refactor: GLToolbar
    if (wxGetApp().is_editor()) {
        m_canvas->enable_select_plate_toolbar(true);
    }
    m_canvas->enable_assemble_view_toolbar(false);

    // sizer, m_canvas_widget
    m_canvas_widget->Bind(wxEVT_KEY_DOWN, &Preview::update_layers_slider_from_canvas, this);

    wxBoxSizer *main_sizer = new wxBoxSizer(wxHORIZONTAL);
    main_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);
    main_sizer->Add(m_sla_preview_splitter, 0, wxEXPAND | wxTOP | wxBOTTOM, 0);
    main_sizer->Add(m_sla_2d_canvas, 1, wxALL | wxEXPAND, 0);
    m_sla_2d_canvas->Hide();
    m_sla_preview_splitter->Hide();

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    bind_event_handlers();

    return true;
}

Preview::~Preview()
{
    unbind_event_handlers();

    if (m_canvas != nullptr)
        delete m_canvas;

    if (m_canvas_widget != nullptr)
        delete m_canvas_widget;

    if (m_sla_preview_splitter != nullptr)
        delete m_sla_preview_splitter;

    if (m_sla_2d_canvas != nullptr)
        delete m_sla_2d_canvas;
}

void Preview::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
    if (m_sla_2d_canvas != nullptr && m_sla_2d_canvas->IsShown())
        m_sla_2d_canvas->Refresh();
}

void Preview::set_sla_preview_pane_visible(bool sla_mode)
{
    if (m_sla_2d_canvas == nullptr)
        return;
    m_sla_2d_canvas->Show(sla_mode);
    if (m_sla_preview_splitter != nullptr)
        m_sla_preview_splitter->Show(sla_mode);
    Layout();
}

void Preview::sync_sla_2d_layer_from_slider()
{
    if (m_sla_2d_canvas == nullptr || m_canvas == nullptr)
        return;
    IMSlider* slider = m_canvas->get_gcode_viewer().get_layers_slider();
    if (slider == nullptr)
        return;
    m_sla_2d_canvas->set_view_layer_index(slider->GetHigherValue());
}

void Preview::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void Preview::select_view(const std::string& direction)
{
    m_canvas->select_view(direction);
}

void Preview::set_drop_target(wxDropTarget* target)
{
    if (target != nullptr)
        SetDropTarget(target);
}

//BBS: add only gcode mode
void Preview::load_print(bool keep_z_range, bool only_gcode)
{
    PrinterTechnology tech = m_process->current_printer_technology();
    if (tech == ptFFF)
        load_print_as_fff(keep_z_range, only_gcode);
    else if (tech == ptSLA)     // Step 2.4: SLA preview branch
        load_print_as_sla();

    Layout();
}

//BBS: add only gcode mode
void Preview::reload_print(bool keep_volumes, bool only_gcode)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" %1%: enter, keep_volumes %2%")%__LINE__ %keep_volumes;
#ifdef __linux__
    // We are getting mysterious crashes on Linux in gtk due to OpenGL context activation GH #1874 #1955.
    // So we are applying a workaround here: a delayed release of OpenGL vertex buffers.
    if (!IsShown())
    {
        m_volumes_cleanup_required = !keep_volumes;
        return;
    }
#endif /* __linux__ */
    if (
#ifdef __linux__
        m_volumes_cleanup_required ||
#endif /* __linux__ */
        !keep_volumes)
    {
        m_canvas->reset_volumes();
        //BBS: add m_loaded_print logic
        //m_loaded = false;
        m_loaded_print = nullptr;
#ifdef __linux__
        m_volumes_cleanup_required = false;
#endif /* __linux__ */
    }

    load_print(false, only_gcode);
    m_only_gcode = only_gcode;
}

//BBS: add only gcode mode
void Preview::refresh_print()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" %1%: enter, current m_loaded_print %2%")%__LINE__ %m_loaded_print;
    //BBS: add m_loaded_print logic
    //m_loaded = false;
    m_loaded_print = nullptr;

    if (!IsShown())
        return;

    load_print(true, m_only_gcode);
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" %1%: exit")%__LINE__;
}

//BBS: always load shell at preview
void Preview::load_shells(const Print& print, bool force_previewing)
{
    m_canvas->load_shells(print, force_previewing);
}

//BBS: always load shell at preview
void Preview::reset_shells()
{
    m_canvas->reset_shells();
}

void Preview::msw_rescale()
{
    // rescale warning legend on the canvas
    get_canvas3d()->msw_rescale();

    if (m_sla_preview_splitter != nullptr) {
        const int split_w = this->FromDIP(3);
        m_sla_preview_splitter->SetMinSize(wxSize(split_w, -1));
        m_sla_preview_splitter->SetMaxSize(wxSize(split_w, -1));
    }

    // rescale legend
    refresh_print();
}

void Preview::sys_color_changed()
{
    if (m_sla_preview_splitter != nullptr)
        m_sla_preview_splitter->SetBackgroundColour(sla_preview_splitter_line_colour());
    //TODO
    // m_layers_slider->sys_color_changed();
}

void Preview::on_tick_changed(Type type)
{
    //if (type == Type::PausePrint) {
    //    m_schedule_background_process();
    //}
    m_keep_current_preview_type = false;
    reload_print(false);
}

void Preview::bind_event_handlers()
{
    this->Bind(wxEVT_SIZE, &Preview::on_size, this);
}

void Preview::unbind_event_handlers()
{
    this->Unbind(wxEVT_SIZE, &Preview::on_size, this);
}

void Preview::show_sliders(bool show)
{
    show_moves_sliders(show);
    show_layers_sliders(show);
}

void Preview::show_moves_sliders(bool show)
{
    // Step 2.5: Control IMSlider visibility for SLA mode.
    // Hides the horizontal moves slider when in SLA mode (SLA has no toolpath moves to scrub).
    // IMSlider::Show() uses m_visible guard in render() so it works for both slider instances.
    IMSlider* slider = m_canvas->get_gcode_viewer().get_moves_slider();
    if (slider != nullptr)
        slider->Show(show);
}

void Preview::show_layers_sliders(bool show)
{
    // Step 2.5: Control IMSlider visibility for SLA layer preview.
    // FFF path never calls this function — FFF slider visibility is managed by GCodeViewer.
    IMSlider* slider = m_canvas->get_gcode_viewer().get_layers_slider();
    if (slider != nullptr)
        slider->Show(show);
}


void Preview::on_size(wxSizeEvent& evt)
{
    evt.Skip();
    Refresh();
}

void Preview::check_layers_slider_values(std::vector<CustomGCode::Item>& ticks_from_model, const std::vector<double>& layers_z)
{
    // All ticks that would end up outside the slider range should be erased.
    // TODO: this should be placed into more appropriate part of code,
    // this function is e.g. not called when the last object is deleted
    unsigned int old_size = ticks_from_model.size();
    ticks_from_model.erase(std::remove_if(ticks_from_model.begin(), ticks_from_model.end(),
                     [layers_z](CustomGCode::Item val)
        {
            auto it = std::lower_bound(layers_z.begin(), layers_z.end(), val.print_z - epsilon());
            return it == layers_z.end();
        }),
        ticks_from_model.end());
    if (ticks_from_model.size() != old_size)
        m_schedule_background_process();
}

// Find an index of a value in a sorted vector, which is in <z-eps, z+eps>.
// Returns -1 if there is no such member.
static int find_close_layer_idx(const std::vector<double> &zs, double &z, double eps)
{
    if (zs.empty()) return -1;
    auto it_h = std::lower_bound(zs.begin(), zs.end(), z);
    if (it_h == zs.end()) {
        auto it_l = it_h;
        --it_l;
        if (z - *it_l < eps) return int(zs.size() - 1);
    } else if (it_h == zs.begin()) {
        if (*it_h - z < eps) return 0;
    } else {
        auto it_l = it_h;
        --it_l;
        double dist_l = z - *it_l;
        double dist_h = *it_h - z;
        if (std::min(dist_l, dist_h) < eps) { return (dist_l < dist_h) ? int(it_l - zs.begin()) : int(it_h - zs.begin()); }
    }
    return -1;
}

void Preview::update_layers_slider_mode()
{
    //    true  -> single-extruder printer profile OR
    //             multi-extruder printer profile , but whole model is printed by only one extruder
    //    false -> multi-extruder printer profile , and model is printed by several extruders
    bool one_extruder_printed_model = true;
    bool can_change_color = true;
    // extruder used for whole model for multi-extruder printer profile
    int only_extruder = -1;

    // BBS
    if (wxGetApp().filaments_cnt() > 1) {
        //const ModelObjectPtrs& objects = wxGetApp().plater()->model().objects;
        auto plate_extruders = wxGetApp().plater()->get_partplate_list().get_curr_plate()->get_extruders_without_support();
        for (auto extruder : plate_extruders) {
            if (extruder != plate_extruders[0])
                can_change_color = false;
        }
        // check if whole model uses just only one extruder
        if (!plate_extruders.empty()) {
            //const int extruder = objects[0]->config.has("extruder") ? objects[0]->config.option("extruder")->getInt() : 0;
            only_extruder = plate_extruders[0];
            //    auto is_one_extruder_printed_model = [objects, extruder]() {
            //        for (ModelObject *object : objects) {
            //            if (object->config.has("extruder") && object->config.option("extruder")->getInt() != extruder) /*return false*/;

            //            for (ModelVolume *volume : object->volumes)
            //                if ((volume->config.has("extruder") && volume->config.option("extruder")->getInt() != extruder) || !volume->mmu_segmentation_facets.empty()) return false;

            //            for (const auto &range : object->layer_config_ranges)
            //                if (range.second.has("extruder") && range.second.option("extruder")->getInt() != extruder) return false;
            //        }
            //        return true;
            //    };

            //    if (is_one_extruder_printed_model())
            //        only_extruder = extruder;
            //    else
            //        one_extruder_printed_model = false;
        }
    }

    IMSlider *m_layers_slider = m_canvas->get_gcode_viewer().get_layers_slider();
    m_layers_slider->SetModeAndOnlyExtruder(one_extruder_printed_model, only_extruder, can_change_color);
}

void Preview::update_layers_slider_from_canvas(wxKeyEvent &event)
{
    if (event.HasModifiers()) {
        event.Skip();
        return;
    }

    const auto key = event.GetKeyCode();

    IMSlider *m_layers_slider = m_canvas->get_gcode_viewer().get_layers_slider();
    IMSlider *m_moves_slider  = m_canvas->get_gcode_viewer().get_moves_slider();
    if (key == 'L') {
        if(!m_layers_slider->switch_one_layer_mode())
            event.Skip();
        m_canvas->set_as_dirty();
    }
    /*else if (key == WXK_SHIFT)
        m_layers_slider->UseDefaultColors(false);*/
    else
        event.Skip();
}

void Preview::update_layers_slider(const std::vector<double>& layers_z, bool keep_z_range)
{
    IMSlider *m_layers_slider = m_canvas->get_gcode_viewer().get_layers_slider();
    // Save the initial slider span.
    double z_low     = m_layers_slider->GetLowerValueD();
    double z_high    = m_layers_slider->GetHigherValueD();
    bool   was_empty = m_layers_slider->GetMaxValue() == 0;

    bool force_sliders_full_range = was_empty;
    if (!keep_z_range) {
        bool span_changed = layers_z.empty() || std::abs(layers_z.back() - m_layers_slider->GetMaxValueD()) > epsilon() /*1e-6*/;
        force_sliders_full_range |= span_changed;
    }
    bool snap_to_min = force_sliders_full_range || m_layers_slider->is_lower_at_min();
    bool snap_to_max = force_sliders_full_range || m_layers_slider->is_higher_at_max();

    // Detect and set manipulation mode for double slider
    update_layers_slider_mode();

    Plater* plater = wxGetApp().plater();
    //BBS: replace model custom gcode with current plate custom gcode
    CustomGCode::Info ticks_info_from_curr_plate;
    if (wxGetApp().is_editor())
        ticks_info_from_curr_plate = plater->model().get_curr_plate_custom_gcodes();
    else {
        ticks_info_from_curr_plate.mode   = CustomGCode::Mode::SingleExtruder;
        ticks_info_from_curr_plate.gcodes = m_canvas->get_custom_gcode_per_print_z();
    }
    check_layers_slider_values(ticks_info_from_curr_plate.gcodes, layers_z);

    // first of all update extruder colors to avoid crash, when we are switching printer preset from MM to SM
    m_layers_slider->SetExtruderColors(plater->get_extruder_colors_from_plater_config(wxGetApp().is_editor() ? nullptr : m_gcode_result));
    m_layers_slider->SetSliderValues(layers_z);
    assert(m_layers_slider->GetMinValue() == 0);
    m_layers_slider->SetMaxValue(layers_z.empty() ? 0 : layers_z.size() - 1);

    int idx_low  = 0;
    int idx_high = m_layers_slider->GetMaxValue();
    if (!layers_z.empty()) {
        if (!snap_to_min) {
            int idx_new = find_close_layer_idx(layers_z, z_low, epsilon() /*1e-6*/);
            if (idx_new != -1) idx_low = idx_new;
        }
        if (!snap_to_max) {
            int idx_new = find_close_layer_idx(layers_z, z_high, epsilon() /*1e-6*/);
            if (idx_new != -1) idx_high = idx_new;
        }
    }
    m_layers_slider->SetSelectionSpan(idx_low, idx_high);

    auto curr_plate = wxGetApp().plater()->get_partplate_list().get_curr_plate();
    auto curr_print_seq = curr_plate->get_real_print_seq();
    bool sequential_print = (curr_print_seq == PrintSequence::ByObject);
    m_layers_slider->SetDrawMode(sequential_print);
    
    m_layers_slider->SetTicksValues(ticks_info_from_curr_plate);

    auto print_mode_stat = m_gcode_result->print_statistics.modes.front();
    m_layers_slider->SetLayersTimes(print_mode_stat.layers_times, print_mode_stat.time);

    // Suggest the auto color change, if model looks like sign
    if (m_layers_slider->IsNewPrint()) {
        const Print &print = wxGetApp().plater()->fff_print();

        // bool is_possible_auto_color_change = false;
        for (auto object : print.objects()) {
            double object_x = double(object->size().x());
            double object_y = double(object->size().y());

            // if it's sign, than object have not to be a too height
            double  height      = object->height();
            coord_t longer_side = std::max(object_x, object_y);
            auto    num_layers  = int(object->layers().size());
            if (height / longer_side > 0.3 || num_layers < 2) continue;

            const ExPolygons &bottom      = object->get_layer(0)->lslices;
            double            bottom_area = area(bottom);

            // at least 25% of object's height have to be a solid
            int i, min_solid_height = int(0.25 * num_layers);
            for (i = 1; i <= min_solid_height; ++i) {
                double cur_area = area(object->get_layer(i)->lslices);
                if (!equivalent_areas(bottom_area, cur_area)) {
                    // but due to the elephant foot compensation, the first layer may be slightly smaller than the others
                    if (i == 1 && fabs(cur_area - bottom_area) / bottom_area < 0.1) {
                        // So, let process this case and use second layer as a bottom
                        bottom_area = cur_area;
                        continue;
                    }
                    break;
                }
            }
            if (i < min_solid_height) continue;
        }
    }
}

//BBS: add only gcode mode
void Preview::load_print_as_fff(bool keep_z_range, bool only_gcode)
{
    if (wxGetApp().mainframe == nullptr || wxGetApp().is_recreating_gui())
        // avoid processing while mainframe is being constructed
        return;

    // Step 2.5: Reset IMSlider from SLA mode back to FFF mode.
    // Clears SLA callback and restores visibility for both sliders (hidden during SLA preview).
    {
        IMSlider* layers_slider = m_canvas->get_gcode_viewer().get_layers_slider();
        if (layers_slider != nullptr) {
            layers_slider->set_on_change_callback(nullptr); // clear SLA callback
            layers_slider->Show(true);                      // restore FFF layers slider
        }
        IMSlider* moves_slider = m_canvas->get_gcode_viewer().get_moves_slider();
        if (moves_slider != nullptr)
            moves_slider->Show(true);                       // restore FFF moves slider
    }
    set_sla_preview_pane_visible(false);
    if (m_sla_2d_canvas != nullptr)
        m_sla_2d_canvas->reset_print();

    //BBS: add m_loaded_print logic
    const Print *print = m_process->fff_print();
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" %1%: previous print %2%, new print %3%")%__LINE__ %m_loaded_print %print;
    if ((m_loaded_print&&(m_loaded_print == print)) || m_process->current_printer_technology() != ptFFF) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" %1%: already loaded before, return directly")%__LINE__;
        return;
    }

    // we require that there's at least one object and the posSlice step
    // is performed on all of them(this ensures that _shifted_copies was
    // populated and we know the number of layers)
    bool has_layers = false;
    //BBS: always load shell at preview
    load_shells(*print, true);
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" %1%: print: %2%, gcode_result %3%, check started")%__LINE__ %print %m_gcode_result;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" %1%: print is step done, posSlice %2%, posSupportMaterial %3%, psGCodeExport %4%") % __LINE__ % print->is_step_done(posSlice) %print->is_step_done(posSupportMaterial) % print->is_step_done(psGCodeExport);
    if (print->is_step_done(posSlice)) {
        for (const PrintObject* print_object : print->objects())
            if (! print_object->layers().empty()) {
                has_layers = true;
                break;
            }
    }
    if (print->is_step_done(posSupportMaterial)) {
        for (const PrintObject* print_object : print->objects())
            if (! print_object->support_layers().empty()) {
                has_layers = true;
                break;
            }
    }

    //BBS: support preview gcode directly even if no slicing
    bool directly_preview = print->is_step_done(psGCodeExport) && !m_gcode_result->moves.empty();
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": directly_preview: %1%, gcode_result moves %2%, has_layers %3%") % directly_preview % m_gcode_result->moves.size() %has_layers;
    if (wxGetApp().is_editor() && !has_layers && !directly_preview) {
        show_sliders(false);
        m_canvas_widget->Refresh();
        return;
    }
    //BBS: for direct preview, don't keep z range
    else if (directly_preview && !has_layers)
        keep_z_range = false;

    GCodeViewer::EViewType gcode_view_type = m_canvas->get_gcode_view_preview_type();
    bool gcode_preview_data_valid = !m_gcode_result->moves.empty();

    // Collect colors per extruder.
    std::vector<std::string> colors;
    std::vector<CustomGCode::Item> color_print_values = {};
    // set color print values, if it si selected "ColorPrint" view type
    if (gcode_view_type == GCodeViewer::EViewType::ColorPrint) {
        colors = wxGetApp().plater()->get_colors_for_color_print(m_gcode_result);

        if (!gcode_preview_data_valid) {
            if (wxGetApp().is_editor())
                //BBS
                color_print_values = wxGetApp().plater()->model().get_curr_plate_custom_gcodes().gcodes;
            else
                color_print_values = m_canvas->get_custom_gcode_per_print_z();
            colors.push_back("#808080"); // gray color for pause print or custom G-code
        }
    }
    else if (gcode_preview_data_valid || gcode_view_type == GCodeViewer::EViewType::Tool) {
        colors = wxGetApp().plater()->get_extruder_colors_from_plater_config(m_gcode_result);
        color_print_values.clear();
    }

    std::vector<double> zs;

    if (IsShown()) {
        m_canvas->set_selected_extruder(0);
        bool is_slice_result_valid = wxGetApp().plater()->get_partplate_list().get_curr_plate()->is_slice_result_valid();
        if (gcode_preview_data_valid && (is_slice_result_valid || only_gcode)) {
            // Load the real G-code preview.
            //BBS: add more log
            BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": will load gcode_preview from result, moves count %1%") % m_gcode_result->moves.size();
            //BBS: add only gcode mode
            m_canvas->load_gcode_preview(*m_gcode_result, colors, only_gcode);
            //BBS show sliders
            show_moves_sliders();

            //Orca: keep shell preview on but make it more transparent
            m_canvas->set_shells_on_previewing(true);
            m_canvas->set_shell_transparence();
            Refresh();
            zs = m_canvas->get_gcode_layers_zs();
            //BBS: add m_loaded_print logic
            //m_loaded = true;
            m_loaded_print = print;
        }
        else if (wxGetApp().is_editor()) {
            // Load the initial preview based on slices, not the final G-code.
            //BBS: only display shell before slicing result out
            //m_canvas->load_preview(colors, color_print_values);
            show_moves_sliders(false);
            Refresh();
            //zs = m_canvas->get_volumes_print_zs(true);
        }

        if (!zs.empty() && !m_keep_current_preview_type) {
            unsigned int number_extruders = wxGetApp().is_editor() ?
                (unsigned int)print->extruders().size() :
                m_canvas->get_gcode_extruders_count();
            std::vector<CustomGCode::Item> gcodes = wxGetApp().is_editor() ?
                //BBS
                wxGetApp().plater()->model().get_curr_plate_custom_gcodes().gcodes :
                m_canvas->get_custom_gcode_per_print_z();
            const wxString choice = !gcodes.empty() ?
                _L("Multicolor Print") :
                (number_extruders > 1) ? _L("Filaments") : _L("Line Type");
        }

        if (zs.empty()) {
            // all layers filtered out
            //BBS
            show_layers_sliders(false);
            m_canvas_widget->Refresh();
        } else
            update_layers_slider(zs, keep_z_range);
    }
}

// Step 2.4: Load SLA print preview shells and set up clipping planes.
// Step 2.5: Extract layer z-values and configure IMSlider for SLA layer preview.
// Ported from PrusaSlicer GUI_Preview.cpp:1021 (load_print_as_sla).
void Preview::load_print_as_sla()
{
    const SLAPrint* print = m_process->sla_print();
    if (print == nullptr)
        return;

    // Cache check: avoid redundant reloads within the same slicing result.
    // m_loaded_print is reset to nullptr by reload_print(keep_volumes=false).
    if (m_loaded_print == print)
        return;

    m_canvas->reset_clipping_planes_cache();
    m_canvas->set_use_clipping_planes(true);

    // Step 2.5: Extract layer z-coordinates for IMSlider.
    // Only collect from objects that have completed slaposSliceSupports.
    m_sla_layers_z.clear();
    double initial_layer_height = print->material_config().initial_layer_height.value;
    for (const SLAPrintObject* obj : print->objects()) {
        if (obj->is_step_done(slaposSliceSupports) && !obj->get_slice_index().empty()) {
            auto low_coord = obj->get_slice_index().front().print_level();
            for (const auto& rec : obj->get_slice_index())
                m_sla_layers_z.emplace_back(
                    initial_layer_height + (rec.print_level() - low_coord) * SCALING_FACTOR);
        }
    }
    sort_remove_duplicates(m_sla_layers_z);

    if (IsShown()) {
        // Load SLA object mesh + support tree + pad meshes into Preview canvas.
        // Internally calls reset_volumes() then _load_sla_shells().
        m_canvas->load_sla_preview();

        // SLA has no GCode moves slider.
        show_moves_sliders(false);

        IMSlider* slider = m_canvas->get_gcode_viewer().get_layers_slider();
        if (!m_sla_layers_z.empty() && slider != nullptr) {
            // Configure IMSlider with SLA layer z-values.
            slider->SetSliderValues(m_sla_layers_z);
            slider->SetMaxValue((int)m_sla_layers_z.size() - 1);
            slider->SetSelectionSpan(0, (int)m_sla_layers_z.size() - 1);
            slider->SetHigherValue((int)m_sla_layers_z.size() - 1); // start at top = show all

            // Initial clipping: show all layers (no clip).
            m_canvas->set_clipping_plane(0, ClippingPlane::ClipsNothing());
            m_canvas->set_clipping_plane(1, ClippingPlane::ClipsNothing());

            // Register SLA callback — fires when user moves slider.
            slider->set_on_change_callback([this]() { on_sla_layer_slider_changed(); });

            show_layers_sliders(true);
            set_sla_preview_pane_visible(true);
            if (m_sla_2d_canvas != nullptr) {
                m_sla_2d_canvas->attach_layers_slider(slider);
                m_sla_2d_canvas->set_sla_print(print);
                sync_sla_2d_layer_from_slider();
            }
        } else {
            // Slicing not yet complete — hide slider.
            show_layers_sliders(false);
            set_sla_preview_pane_visible(false);
            if (m_sla_2d_canvas != nullptr)
                m_sla_2d_canvas->reset_print();
        }

        m_loaded_print = print;
    }
}

// Step 2.5: Called when IMSlider vertical slider value changes (SLA mode only).
// Reads current slider position → looks up z-coordinate → updates top clipping plane.
void Preview::on_sla_layer_slider_changed()
{
    if (m_canvas == nullptr)
        return;

    IMSlider* slider = m_canvas->get_gcode_viewer().get_layers_slider();
    if (slider == nullptr)
        return;

    const int low_pos  = slider->GetLowerValue();
    const int high_pos = slider->GetHigherValue();
    const int max_pos  = static_cast<int>(m_sla_layers_z.size()) - 1;
    if (low_pos < 0 || high_pos < 0 || high_pos > max_pos || low_pos > max_pos)
        return;

    const double z_low  = m_sla_layers_z[low_pos];
    const double z_high = m_sla_layers_z[high_pos];

    // Bottom plane: clip below lower thumb.
    // When lower thumb is at 0, use ClipsNothing to avoid clipping into pad geometry.
    // ClippingPlane(+UnitZ, -z_low): clips where P.z < z_low.
    if (low_pos == 0)
        m_canvas->set_clipping_plane(0, ClippingPlane::ClipsNothing());
    else
        m_canvas->set_clipping_plane(0, ClippingPlane(Vec3d::UnitZ(), -z_low));

    // Top plane: clip above upper thumb.
    // When upper thumb is at max, use ClipsNothing to avoid clipping top of model.
    // ClippingPlane(-UnitZ, z_high): clips where P.z > z_high.
    if (high_pos == max_pos)
        m_canvas->set_clipping_plane(1, ClippingPlane::ClipsNothing());
    else
        m_canvas->set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), z_high));

    // Do not call m_canvas->render() synchronously when this callback runs from the *left* preview
    // ImGui (inside GLCanvas3D::render): that would re-enter render() while m_in_render is set.
    // When the slider is driven from the *right* SLA 2D ImGui, we are not inside GLCanvas3D::render;
    // wx Refresh/idle alone often applies clipping one frame late — queue render after the current 2D frame finishes.
    m_canvas->set_as_dirty();
    m_canvas_widget->Refresh();
    if (!m_canvas->is_in_render()) {
        wxGetApp().CallAfter([this]() {
            if (m_canvas != nullptr)
                m_canvas->render();
        });
    }
    sync_sla_2d_layer_from_slider();
    if (m_sla_2d_canvas != nullptr)
        m_sla_2d_canvas->Refresh();
}

AssembleView::AssembleView(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : m_canvas_widget(nullptr)
    , m_canvas(nullptr)
{
    init(parent, bed, model, config, process);
}

AssembleView::~AssembleView()
{
    delete m_canvas;
    delete m_canvas_widget;
}

bool AssembleView::init(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this);
    if (m_canvas_widget == nullptr)
        return false;

    m_canvas = new GLCanvas3D(m_canvas_widget, bed);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));

    m_canvas->allow_multisample(OpenGLManager::can_multisample());
    // XXX: If have OpenGL
    m_canvas->enable_picking(true);
    m_canvas->enable_moving(true);
    // XXX: more config from 3D.pm
    m_canvas->set_model(model);
    m_canvas->set_process(process);
    m_canvas->set_type(GLCanvas3D::ECanvasType::CanvasAssembleView);
    m_canvas->set_config(config);
    m_canvas->enable_gizmos(true);
    m_canvas->enable_selection(true);
    m_canvas->enable_main_toolbar(false);
    m_canvas->enable_labels(false);
    m_canvas->enable_slope(false);
    //BBS: GUI refactor: GLToolbar
    m_canvas->enable_assemble_view_toolbar(false);
    m_canvas->enable_return_toolbar(true);
    m_canvas->enable_separator_toolbar(false);
    //m_canvas->set_show_world_axes(true);//wait for GitHub users to see if they have this requirement
    // BBS: set volume_selection_mode to Volume
    //same to 3d //m_canvas->get_selection().set_volume_selection_mode(Selection::Instance);
    //m_canvas->get_selection().lock_volume_selection_mode();

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    return true;
}

void AssembleView::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

void AssembleView::render()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

bool AssembleView::is_reload_delayed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_reload_delayed() : false;
}

void AssembleView::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if (m_canvas != nullptr) {
        if (!m_canvas->is_initialized()) {
            m_canvas->render(true);
        }
        m_canvas->reload_scene(refresh_immediately, force_full_scene_refresh);
    }
}

void AssembleView::select_view(const std::string& direction)
{
    if (m_canvas != nullptr)
        m_canvas->select_view(direction);
}


} // namespace GUI
} // namespace Slic3r
