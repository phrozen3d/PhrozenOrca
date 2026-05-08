#ifndef slic3r_SLASlice2DCanvas_hpp_
#define slic3r_SLASlice2DCanvas_hpp_

#include <wx/panel.h>

#include <chrono>
#include <memory>

#include "GLModel.hpp"
#include "libslic3r/ExPolygon.hpp"

class wxGLCanvas;
class wxPaintEvent;
class wxSizeEvent;
class wxMouseEvent;
class wxTimer;
class wxTimerEvent;

struct ImGuiContext;

namespace Slic3r {

class SLAPrint;

namespace GUI {

class IMSlider;

// Right-pane SLA layer preview: on-demand single-layer rasterization (Strategy A) or vector fallback.
class SLASlice2DCanvas : public wxPanel
{
public:
    SLASlice2DCanvas(wxWindow* parent);
    ~SLASlice2DCanvas() override;

    void set_sla_print(const SLAPrint* print);
    void set_view_layer_index(int idx);
    void reset_print();

    // Same IMSlider instance as the 3D preview; drawn again via a secondary ImGui context for identical styling.
    void attach_layers_slider(IMSlider* layers_slider);
    void detach_layers_slider();

    /// Centered slice portal in client coordinates; aspect matches machine display (after orientation), same as layer PNG / 3D plate.
    static void compute_slice_portal(int vp_w, int vp_h, double platform_aspect_w_over_h, int& px, int& py, int& pw, int& ph);

private:
    void on_gl_paint(wxPaintEvent& evt);
    void on_size(wxSizeEvent& evt);
    void on_mouse_wheel(wxMouseEvent& evt);
    void on_gl_mouse_motion(wxMouseEvent& evt);
    void on_gl_mouse_button(wxMouseEvent& evt);
    void on_gl_leave(wxMouseEvent& evt);
    void on_hq_timer(wxTimerEvent& evt);

    void render();
    void ensure_sla_imgui_context(ImGuiContext* main_ctx);
    void render_layers_imgui_overlay(int vp_w, int vp_h);
    void upload_grayscale_texture(const unsigned char* data, int w, int h);
    void render_texture_letterbox(int portal_w, int portal_h);
    void render_vector_fallback(int portal_w, int portal_h);
    void build_vector_model_from_polys(const ExPolygons& polys);
    void destroy_texture();

    wxGLCanvas* m_gl{ nullptr };
    IMSlider*   m_im_layers_slider{ nullptr };

    ImGuiContext* m_imgui_ctx_main{ nullptr }; // primary app context; used when destroying SLA context
    ImGuiContext* m_imgui_ctx_sla{ nullptr };
    float         m_imgui_mouse_x{ -1.f };
    float         m_imgui_mouse_y{ -1.f };
    bool          m_imgui_mouse_btn[3]{ false, false, false };
    float         m_imgui_wheel_accum{ 0.f };
    bool          m_imgui_wants_capture_mouse{ false };
    std::chrono::steady_clock::time_point m_imgui_last_frame{};
    bool          m_imgui_have_last_frame{ false };
    const SLAPrint* m_print{ nullptr };
    int m_layer_idx{ 0 };
    int m_cached_layer{ -1 };
    unsigned int m_tex_id{ 0 };
    int m_tex_w{ 0 };
    int m_tex_h{ 0 };
    // Scales the entire slice portal (black rect + slice content); 1 = base size from compute_slice_portal.
    float m_zoom{ 1.f };
    // Pan offset of the whole slice portal (black + content) in client pixels, relative to centered position.
    float m_pan_px{ 0.f };
    float m_pan_py{ 0.f };
    bool  m_pan_dragging{ false };
    float m_pan_anchor_x{ 0.f };
    float m_pan_anchor_y{ 0.f };

    GLModel m_vector_model;
    int m_vector_cached_layer{ -1 };

    // Debounce: hold fast vector preview while layers are changing, switch to
    // high-quality expolygons_to_cvmat 200 ms after the last layer change.
    std::unique_ptr<wxTimer> m_hq_timer;
    bool m_layer_interactive{ false };
};

} // namespace GUI
} // namespace Slic3r

#endif
