#pragma region Phrozen LCD Parameter
#ifndef slic3r_GLGizmoLcdOverhangDetection_hpp_
#define slic3r_GLGizmoLcdOverhangDetection_hpp_

#include "GLGizmoPainterBase.hpp"
//BBS
#include "libslic3r/Print.hpp"
#include "libslic3r/IslandDetection/Island_Detector.hpp"
#include "libslic3r/ObjectID.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLModel.hpp"

#include <boost/thread.hpp>

namespace Slic3r::GUI {

class GLGizmoLcdOverhangDetection : public GLGizmoPainterBase
{
public:
    GLGizmoLcdOverhangDetection(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void render_painter_gizmo() override;

    //BBS: add edit state
    enum EditState {
        state_idle = 0,
        state_generating = 1,
        state_ready
    };

    //BBS
    bool on_key_down_select_tool_type(int keyCode);
    
    bool on_is_selectable() const override;
    bool on_is_activable() const override;

protected:
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;

    void on_set_state() override;
    void show_tooltip_information(float caption_max, float x, float y);
    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return "Entering Overhang Detection"; }
    std::string get_gizmo_leaving_text() const override { return "Leaving Overhang Detection"; }
    std::string get_action_snapshot_name() const override { return "Overhang Detection editing"; }

    // BBS
    wchar_t                           m_current_tool = 0;

    // Phrozen LCD Overhang Detection UI
    enum DetectionAccuracy {
        Accuracy_Low = 0,
        Accuracy_Middle = 1,
        Accuracy_High = 2
    };
    DetectionAccuracy m_detection_accuracy = Accuracy_Low;
    int m_current_model_index = 0;
    int m_current_overhang_area_index = 0;
    int m_total_overhang_areas = 8;
    std::vector<std::string> m_model_names;

private:
    bool on_init() override;

    //BBS: remove const
    void update_model_object() override;
    //BBS: add logic to distinguish the first_time_update and later_update
    void update_from_model_object(bool first_update) override;
    void tool_changed(wchar_t old_tool, wchar_t new_tool);

    void on_opening() override;
    void on_shutdown() override;
    PainterGizmoType get_painter_type() const override;

    void select_facets_by_angle(float threshold, bool block);
    // BBS
    int get_selection_support_threshold_angle();

    int m_support_threshold_angle = -1;

    // Phrozen Island Detection
    void Island_Detection();
    void rebuild_island_models();
    void render_island_contours();
    std::vector<Slic3r::island::Island> m_detected_islands;
    std::vector<GLModel> m_island_models;

    //BBS: add support preview logic
    void init_print_instance();
    void update_support_volumes();
    void invalid_support_volumes(bool invalid_step = false);
    bool need_regenerate_support_volumes();
    void generate_support_volume();
    void run_thread();
    float m_angle_threshold_deg = 40.f;
    bool m_volume_valid = false;


    GLVolume *m_support_volume = NULL;
    mutable bool m_volume_ready = false;
    bool m_is_tree_support = false;
    bool m_cancel = false;
    size_t m_object_id;
    std::vector<ObjectBase::Timestamp> m_volume_timestamps;
    PrintInstance m_print_instance;
    mutable EditState m_edit_state;
    //thread
    boost::thread   m_thread;
    // Mutex and condition variable to synchronize m_thread with the UI thread.
    std::mutex      m_mutex;
    int m_generate_count;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;
};



} // namespace Slic3r::GUI


#endif // slic3r_GLGizmoLcdOverhangDetection_hpp_
#pragma endregion