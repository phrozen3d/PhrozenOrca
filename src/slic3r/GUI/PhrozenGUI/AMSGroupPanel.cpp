#include "AMSGroupPanel.hpp"
#include <wx/dcbuffer.h>

namespace Slic3r {
namespace GUI {

#pragma region AMSGroupPanel
wxBEGIN_EVENT_TABLE(AMSGroupPanel, wxPanel)
    EVT_PAINT(AMSGroupPanel::OnPaint)
    EVT_LEFT_DOWN(AMSGroupPanel::OnLeftDown)
    EVT_ERASE_BACKGROUND(AMSGroupPanel::OnEraseBackground)
wxEND_EVENT_TABLE()

AMSGroupPanel::AMSGroupPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, id, pos, size, wxTAB_TRAVERSAL)
    , m_selected_slot(-1)
    , m_panel_bg(0xEE, 0xEE, 0xEE)
    , m_slot_outline_enabled(0x82, 0x82, 0x80)
    , m_slot_outline_disabled(0xCF, 0xD2, 0xD3)
    , m_slot_outline_selected(0xFF, 0x7C, 0x3F)
    , m_line_color(0x82, 0x82, 0x80)
    , m_feed_port_color(0xCF, 0xD2, 0xD3)
    , m_eInputType( FilamentInputType::Empty )
    , m_text_color(0x82, 0x82, 0x80)
    , m_loading_line_color( 0xFF, 0x7C, 0x3F )
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    // Inherit background color from parent
    if (parent) {
        SetBackgroundColour(parent->GetBackgroundColour());
        //SetBackgroundColour(wxColor(0xff, 0x00, 0x00)); //set red for check background area
    }
    SetMinSize(wxSize(375, 168));  // 76 (SpoolHolder) + 10 (gap) + 289 (original panel)

    // Initialize slot configurations
    for (int i = 0; i < SLOT_COUNT; i++) {
        m_slot_configs[i] = AMSSlotConfig();
    }
}

AMSGroupPanel::~AMSGroupPanel()
{
}

void AMSGroupPanel::SetSlotConfig(int slot_index, const AMSSlotConfig& config)
{
    if (slot_index >= 0 && slot_index < SLOT_COUNT) {
        m_slot_configs[slot_index] = config;
        Refresh();
    }
}

AMSSlotConfig AMSGroupPanel::GetSlotConfig(int slot_index) const
{
    if (slot_index >= 0 && slot_index < SLOT_COUNT) {
        return m_slot_configs[slot_index];
    }
    assert( 0 && "out of bound" );
    return AMSSlotConfig();
}

void AMSGroupPanel::SetSelectedSlot(int slot_index)
{
    if (slot_index >= -1 && slot_index < SLOT_COUNT) {
        m_selected_slot = slot_index;
        Refresh();
    }
}

void AMSGroupPanel::msw_rescale() 
{
    Refresh();
}

void AMSGroupPanel::SetFilamentInputType( const FilamentInputType& eType )
{
    m_eInputType = eType;
    Refresh();
}

bool AMSGroupPanel::IsAnyFilamentLoading()
{
    for ( auto& kInfo : m_slot_configs )
    {
        if ( kInfo.IsLoading() ) return true;
    }
    return false;
}

void AMSGroupPanel::OnEraseBackground(wxEraseEvent& event)
{
    // Do nothing to prevent flicker
}

void AMSGroupPanel::OnPaint(wxPaintEvent& event)
{
    wxAutoBufferedPaintDC dc(this);

    // Clear with transparent background
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);

    if (gc) {
        // Calculate scale to fit panel
        wxSize size = GetClientSize();
        double scale_x = size.GetWidth() / PANEL_WIDTH;
        double scale_y = size.GetHeight() / PANEL_HEIGHT;
        double scale = std::min(scale_x, scale_y);

        // Center the drawing
        double offset_x = (size.GetWidth() - PANEL_WIDTH * scale) / 2.0;
        double offset_y = (size.GetHeight() - PANEL_HEIGHT * scale) / 2.0;

        gc->Translate(offset_x, offset_y);
        gc->Scale(scale, scale);

        DrawPanel(gc);

        delete gc;
    }
}

void AMSGroupPanel::DrawPanel(wxGraphicsContext* gc)
{
    // Draw SpoolHolder on the left side
    DrawSpoolHolder(gc);

    // Save current transform
    gc->PushState();

    // Translate to the right of SpoolHolder with gap
    gc->Translate(SPOOL_HOLDER_WIDTH + GAP_WIDTH, 0);

    // <!-- panel -->
    // Modified from original SVG: <rect x="7" width="282" height="168" rx="10" fill="#EEEEEE"/>
    // Changed to x="0" width="289" to make panel edges align with parent sides
    float fOpacity = m_eInputType == FilamentInputType::AMS ? 1.0 : 0.3;
    auto panel_bg = wxColor( m_panel_bg.Red(), m_panel_bg.Green(), m_panel_bg.Blue(), 255 * fOpacity);
    gc->SetBrush(wxBrush(panel_bg));
    gc->SetPen(*wxTRANSPARENT_PEN);
    wxGraphicsPath path = gc->CreatePath();
    path.AddRoundedRectangle(0, 0, ORIGINAL_PANEL_WIDTH, 168, 10);
    gc->FillPath(path);

    // Draw all four slot groups
    std::vector< int > kLoadingSlotIds;
    kLoadingSlotIds.reserve(4);
    for (int i = 0; i < SLOT_COUNT; i++) {
        if ( m_slot_configs[ i ].IsLoading() )
        {
            //loading line need draw on the top, so paint it final
            kLoadingSlotIds.push_back( i );
            continue;
        }
        double x_offset = 23 + i * 66; // Spacing between slots
        DrawSlotGroup(gc, i, x_offset);
    }

    for ( auto& id : kLoadingSlotIds )
    {
        double x_offset = 23 + id * 66;
        DrawSlotGroup(gc, id, x_offset);
    }


    // <!-- feed port rectangle -->
    DrawFeedPortRectangle(gc);

    // Restore transform
    gc->PopState();
}

void AMSGroupPanel::DrawSlotGroup(wxGraphicsContext* gc, int slot_index, double x_offset)
{
    double light_cx = x_offset + 26; // Center of light circle
    double light_cy = 18;
    double outline_x = x_offset;
    double outline_y = 32;

    bool bIsEnable = ( m_eInputType == FilamentInputType::AMS );
    const AMSSlotConfig& config = m_slot_configs[slot_index]; 

    // Draw light circle (always draw, color determines state)
    DrawLightCircle(gc, light_cx, light_cy, config );

    // Draw slot outline
    bool is_selected = m_selected_slot >= 0 ? ( m_selected_slot == slot_index ) : false;
    DrawSlotOutline(gc, outline_x, outline_y, config, is_selected );

    // Draw title (A1, A2, A3, A4) - centered in the outline
    DrawSlotTitle(gc, outline_x, outline_y, config, slot_index + 1 );

    // Draw connection line
    DrawConnectionLine(gc, slot_index, config );
}

void AMSGroupPanel::DrawLightCircle(wxGraphicsContext* gc, double cx, double cy, const AMSSlotConfig& config )
{
    if ( !IsAMSEnabled() || !config.IsEntry() ) {
        float fOpacity = 0.3;
        gc->SetPen( wxPen(wxColour(m_line_color.Red(), m_line_color.Green(), m_line_color.Blue(), 255 * fOpacity ), 1) );
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->DrawEllipse(cx - 5.5, cy - 5.5, 11, 11);
        return;
    }

    // draw light circle outline
    gc->SetPen(wxPen(wxColour(m_line_color.Red(), m_line_color.Green(), m_line_color.Blue()), 1));
    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->DrawEllipse(cx - 5.5, cy - 5.5, 11, 11);

    float fOpacity = 1.0;
    auto kColor = config.light_color_entry;
    // <!-- light gradient --> Draw with blur effect (simulated with multiple semi-transparent circles)
    for (int i = 6; i >= 0; i--) {
        double radius = 3.0 + i * 0.9;
        int alpha = 255 - i * 40; // Fade out
        wxColour grad_color(kColor.Red(), kColor.Green(), kColor.Blue(), alpha > 0 ? alpha : 0);
        gc->SetBrush(wxBrush(grad_color));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawEllipse(cx - radius, cy - radius, radius * 2, radius * 2);
    }
    
    // <!-- light infill --> <circle cx="X" cy="18" r="6" fill="color"/>
    gc->SetBrush(wxBrush(kColor));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawEllipse(cx - 6, cy - 6, 12, 12);
}

void AMSGroupPanel::DrawSlotOutline(wxGraphicsContext* gc, double x, double y, const AMSSlotConfig& config, bool is_selected )
{
    
    wxGraphicsPath path = gc->CreatePath();
    float fOpacity = IsAMSEnabled() ? 1.0 : 0.3;


    if (is_selected && IsAMSEnabled() ) {
        // draw selected outline
        path.AddRoundedRectangle(x - 3, y - 3, 58, 94, 13);
        gc->SetPen(wxPen(m_slot_outline_selected, 1.5));
        gc->SetBrush(wxBrush(wxColour(255, 255, 255, 25))); // white with 0.1 opacity
        gc->DrawPath(path);
    }

    // Draw main outline
    path = gc->CreatePath();
    path.AddRoundedRectangle(x, y, 52, 88, 10);

    //gc->SetPen(*wxTRANSPARENT_PEN);
    //gc->SetBrush(wxBrush(wxColour(m_panel_bg.Red(), m_panel_bg.Green(), m_panel_bg.Blue(), 255 * fOpacity )));
    //gc->FillPath(path);
    if ( IsAMSEnabled() && config.IsEntry() )
    {
        // <!-- outline --> Enabled slot
        gc->SetPen(wxPen(wxColor( m_line_color.Red(), m_line_color.Green(), m_line_color.Blue(), 255 * fOpacity ), 1.5));
        gc->SetBrush(wxBrush(wxColour(255, 255, 255, 25))); // white with 0.1 opacity
        gc->DrawPath(path);
    }
    else
    {
        // Draw dashed stroke manually
        DrawDashedRoundedRect(gc, x, y, 52, 88, 10, 
                              wxColor( m_line_color.Red(), m_line_color.Green(), m_line_color.Blue(), 255 * fOpacity ), 
                              1.5, 3, 3);
    }


    #if 0
    if (!is_enabled) {
        // <!-- outline --> Disabled slot with dashed line
        // <rect stroke-dasharray="2.73 2.73"/>
        // wxGraphicsContext doesn't support wxPen dashes, need to manually draw dashed outline
        gc->SetPen(*wxTRANSPARENT_PEN);
        //gc->SetBrush(wxBrush(wxColour(255, 255, 255, 25))); // white with 0.1 opacity
        gc->SetBrush(wxBrush(wxColour(m_panel_bg.Red(), m_panel_bg.Green(), m_panel_bg.Blue(), 255 * fOpacity )));
        
        gc->FillPath(path);

        // Draw dashed stroke manually
        DrawDashedRoundedRect(gc, x, y, 52, 88, 10, m_slot_outline_disabled, 1.5, 3, 3);
    } else {
        // <!-- outline --> Enabled slot
        gc->SetPen(wxPen(m_slot_outline_enabled, 1.5));
        gc->SetBrush(wxBrush(wxColour(255, 255, 255, 25))); // white with 0.1 opacity
        gc->DrawPath(path);
    }
    #endif
}

void AMSGroupPanel::DrawDashedRoundedRect(wxGraphicsContext* gc, double x, double y, double w, double h,
                                           double radius, const wxColour& color, double width,
                                           double dash_on, double dash_off)
{
    // Manually draw dashed rounded rectangle by drawing small line segments
    gc->SetPen(wxPen(color, width));

    double pattern_length = dash_on + dash_off;
    double perimeter = 2 * (w + h - 8 * radius) + 2 * M_PI * radius; // Approximate perimeter

    // Draw dashed lines for each side
    // Top side (with rounded corners)
    double current_pos = 0;
    bool drawing = true;

    // Helper lambda to draw dash segment
    auto draw_segment = [&](double x1, double y1, double x2, double y2) {
        if (drawing) {
            gc->StrokeLine(x1, y1, x2, y2);
        }
    };

    // Simplified approach: draw dashes on straight segments only
    // Top line
    for (double px = x + radius; px < x + w - radius; ) {
        double segment_len = drawing ? dash_on : dash_off;
        double next_px = std::min(px + segment_len, x + w - radius);
        if (drawing) {
            gc->StrokeLine(px, y, next_px, y);
        }
        px = next_px;
        drawing = !drawing;
    }

    // Right line
    drawing = true;
    for (double py = y + radius; py < y + h - radius; ) {
        double segment_len = drawing ? dash_on : dash_off;
        double next_py = std::min(py + segment_len, y + h - radius);
        if (drawing) {
            gc->StrokeLine(x + w, py, x + w, next_py);
        }
        py = next_py;
        drawing = !drawing;
    }

    // Bottom line
    drawing = true;
    for (double px = x + w - radius; px > x + radius; ) {
        double segment_len = drawing ? dash_on : dash_off;
        double next_px = std::max(px - segment_len, x + radius);
        if (drawing) {
            gc->StrokeLine(px, y + h, next_px, y + h);
        }
        px = next_px;
        drawing = !drawing;
    }

    // Left line
    drawing = true;
    for (double py = y + h - radius; py > y + radius; ) {
        double segment_len = drawing ? dash_on : dash_off;
        double next_py = std::max(py - segment_len, y + radius);
        if (drawing) {
            gc->StrokeLine(x, py, x, next_py);
        }
        py = next_py;
        drawing = !drawing;
    }

    // Draw rounded corners (simplified as arcs)
    // Top-left corner
    wxGraphicsPath path = gc->CreatePath();
    path.AddArc(x + radius, y + radius, radius, M_PI, M_PI * 1.5, true);
    gc->StrokePath(path);

    // Top-right corner
    path = gc->CreatePath();
    path.AddArc(x + w - radius, y + radius, radius, M_PI * 1.5, M_PI * 2, true);
    gc->StrokePath(path);

    // Bottom-right corner
    path = gc->CreatePath();
    path.AddArc(x + w - radius, y + h - radius, radius, 0, M_PI * 0.5, true);
    gc->StrokePath(path);

    // Bottom-left corner
    path = gc->CreatePath();
    path.AddArc(x + radius, y + h - radius, radius, M_PI * 0.5, M_PI, true);
    gc->StrokePath(path);
}

void AMSGroupPanel::DrawSlotTitle(wxGraphicsContext* gc, double x, double y, const AMSSlotConfig& config, int slot_number )
{
    // Set font for title - using Roboto font
    wxFont font(wxFontInfo(12).FaceName("Roboto"));

    // Fallback to system font if Roboto is not available
    if (!font.IsOk()) {
        font = wxFont(wxFontInfo(12).Family(wxFONTFAMILY_DEFAULT));
    }

    float fOpacity = ( IsAMSEnabled() && config.IsEntry() ) ? 1.0 : 0.3;
    auto text_color = wxColor( m_text_color.Red(), m_text_color.Green(), m_text_color.Red(), 255 * fOpacity );
    gc->SetFont(font, text_color );

    // Draw "A" + number (A1, A2, A3, A4)
    wxString title = wxString::Format("A%d", slot_number);

    // Get text extent to center it
    double text_width, text_height;
    gc->GetTextExtent(title, &text_width, &text_height);

    // <!-- title (A1)--> Draw the text centered in the outline
    // Outline: x, y, width=52, height=88
    double text_x = x + (52 - text_width) / 2.0;   // Center horizontally
    double text_y = y + (88 - text_height) / 2.0;  // Center vertically

    gc->DrawText(title, text_x, text_y);
}

void AMSGroupPanel::DrawConnectionLine(wxGraphicsContext* gc, int slot_index, const AMSSlotConfig& config )
{
    double line_width_enable = 6.0;
    double line_width_disable = 2.0;

    bool bHasFilamentInput = config.IsParking();
    auto line_width = (IsAMSEnabled() && bHasFilamentInput ) ? line_width_enable : line_width_disable;

    double start_x = 23 + slot_index * 66 + 26; // Center of slot
    double start_y = 120; // Bottom edge of slot (y=32 + height=88)

    // Use BUTT cap for the connection to slot (flat edge aligned with slot bottom)
    float fOpacity = IsAMSEnabled() ? 1.0 : 0.3;

    auto lineColor = ( IsAMSEnabled() && config.IsLoading() ) ? m_loading_line_color : m_line_color;
    lineColor.Set( lineColor.Red(), lineColor.Green(), lineColor.Blue(), 255 * fOpacity );

    //wxPen pen_butt(m_line_color, line_width);
    wxPen pen_butt(lineColor, line_width);
    pen_butt.SetCap(wxCAP_BUTT);

    // Use ROUND cap for other parts of the line
    wxPen pen_round(lineColor, line_width);
    pen_round.SetCap(wxCAP_ROUND);

    switch (slot_index) {
        case 0: {
            // <!-- enable line--> or <!-- disable line-->
            // draw line from slot to feedPortRectangle - use BUTT cap to align flush with slot bottom
            gc->SetPen(pen_butt);
            gc->StrokeLine(start_x, start_y, start_x, 144);
            break;
        }
        case 1: {
            // <!-- enable line--> or <!-- disable line-->
            // Draw initial vertical line from slot with BUTT cap
            gc->SetPen(pen_butt);
            gc->StrokeLine(start_x, start_y, start_x, 134);

            // Draw curve and horizontal line with ROUND cap
            gc->SetPen(pen_round);
            wxGraphicsPath path = gc->CreatePath();
            path.MoveToPoint(start_x, 134);
            // Curve to horizontal
            double curve_x = start_x - 10;
            path.AddArc(curve_x, 134, 10, 0, M_PI/2, true);
            path.AddLineToPoint(49, 144);
            gc->StrokePath(path);
            break;
        }
        case 2: {
            // <!-- disable line-->
            // Draw initial vertical line from slot with BUTT cap
            gc->SetPen(pen_butt);
            gc->StrokeLine(start_x, start_y, start_x, 134);

            // Draw curve and horizontal line with ROUND cap
            gc->SetPen(pen_round);
            wxGraphicsPath path = gc->CreatePath();
            path.MoveToPoint(start_x, 134);
            // Curve to horizontal
            double curve_x = start_x - 10;
            path.AddArc(curve_x, 134, 10, 0, M_PI/2, true);
            path.AddLineToPoint(50, 144);
            gc->StrokePath(path);
            break;
        }
        case 3: {
            // <!-- enable line-->
            // Draw initial vertical line from slot with BUTT cap
            gc->SetPen(pen_butt);
            gc->StrokeLine(start_x, start_y, start_x, 134);

            // Draw curve and horizontal line with ROUND cap
            gc->SetPen(pen_round);
            wxGraphicsPath path = gc->CreatePath();
            path.MoveToPoint(start_x, 134);
            // Curve to horizontal
            double curve_x = start_x - 10;
            path.AddArc(curve_x, 134, 10, 0, M_PI/2, true);
            path.AddLineToPoint(49, 144);
            gc->StrokePath(path);
            break;
        }
    }
}

void AMSGroupPanel::DrawFeedPortRectangle(wxGraphicsContext* gc)
{
    // <!-- feed port rectangle -->
    // <rect x="37" y="138" width="24" height="12" rx="4" fill="#CFD2D3"/>
    gc->SetBrush(wxBrush(m_feed_port_color));
    gc->SetPen(*wxTRANSPARENT_PEN);

    wxGraphicsPath path = gc->CreatePath();

    wxDouble start_x = 37;
    wxDouble start_y = 138;
    wxDouble width = 24;
    wxDouble height = 12;
    wxDouble radius = 4;

    path.AddRoundedRectangle(start_x, start_y, width, height, radius);
    gc->FillPath(path);
    gc->StrokePath(path);

    bool bUsingAMS = m_eInputType == FilamentInputType::AMS;
    float fOpacity = bUsingAMS ? 1.0 : 0.3;
    auto usingColor = ( bUsingAMS && IsAnyFilamentLoading() ) ? m_loading_line_color : m_line_color;
    auto lineColor = wxColor( usingColor.Red(), usingColor.Green(), usingColor.Blue(), 255 * fOpacity );

    auto fLineWidth = bUsingAMS && IsAnyFilamentLoading() ? line_width_enable : line_width_disable;
    wxPen pen(lineColor, fLineWidth);
    pen.SetCap(wxCAP_BUTT);
    gc->SetPen(pen);

    // move to rectangle's bottom
    start_x += width/2.0;
    start_y += height;
    gc->StrokeLine(start_x, start_y, start_x, start_y + 20 );
    start_y += 15;

#if 0
    // draw line from feedPortRectangle to nozzle
    //if (is_enabled) {
    //    wxPen pen2(line_color, line_width_enable);
    //    pen2.SetCap(wxCAP_ROUND);
    //    gc->SetPen(pen2);
    //}

    wxGraphicsPath path2 = gc->CreatePath();
    path2.MoveToPoint(start_x, start_y);
    // Curve to horizontal
    path2.AddArc(start_x - 10, start_y, 10, 0, M_PI/2, true);
    
    // move to arc end
    start_x -= 10;
    start_y += 10;
    path2.MoveToPoint(start_x, start_y);
    
    // add horizo
    start_x -= 5;
    path2.AddLineToPoint(start_x, start_y);
    
    // Curve down
    path2.AddArc(start_x, start_y + 10, 10, M_PI * 1.5, M_PI, false); 
    gc->StrokePath(path2);
#endif

}

wxRect AMSGroupPanel::GetSlotRect(int slot_index) const
{
    if (slot_index < 0 || slot_index >= SLOT_COUNT)
        return wxRect();

    // Calculate slot rectangle in client coordinates
    wxSize size = GetClientSize();
    double scale_x = size.GetWidth() / PANEL_WIDTH;
    double scale_y = size.GetHeight() / PANEL_HEIGHT;
    double scale = std::min(scale_x, scale_y);

    // Center offset for the entire panel (including SpoolHolder)
    double offset_x = (size.GetWidth() - PANEL_WIDTH * scale) / 2.0;
    double offset_y = (size.GetHeight() - PANEL_HEIGHT * scale) / 2.0;

    // Slot position needs to account for SpoolHolder width + gap + original x offset
    // In DrawPanel, we translate by (SPOOL_HOLDER_WIDTH + GAP_WIDTH) before drawing slots
    double slot_x_in_panel = SPOOL_HOLDER_WIDTH + GAP_WIDTH + (23 + slot_index * 66);
    double slot_y_in_panel = 32;

    double slot_x = slot_x_in_panel * scale + offset_x;
    double slot_y = slot_y_in_panel * scale + offset_y;
    double slot_w = 52 * scale;
    double slot_h = 88 * scale;

    return wxRect(slot_x, slot_y, slot_w, slot_h);
}

int AMSGroupPanel::HitTestSlot(const wxPoint& pos) const
{
    for (int i = 0; i < SLOT_COUNT; i++) {
        wxRect rect = GetSlotRect(i);
        if (rect.Contains(pos)) {
            return i;
        }
    }
    return -1;
}

void AMSGroupPanel::DrawSpoolHolder(wxGraphicsContext* gc)
{
    float fOpacity = m_eInputType == FilamentInputType::Spool ? 1.0 : 0.3;
    auto panel_bg_color = wxColor( m_panel_bg.Red(), m_panel_bg.Green(), m_panel_bg.Blue(), 255 * fOpacity );
    auto text_color = wxColor( m_text_color.Red(), m_text_color.Green(), m_text_color.Blue(), 255 * fOpacity );

    // Save current state and set opacity to 0.3
    gc->PushState();

    // <!-- Background rectangle -->
    // <rect opacity="0.3" width="76" height="168" rx="10" fill="#EEEEEE"/>
    gc->SetBrush(wxBrush(panel_bg_color));
    gc->SetPen(*wxTRANSPARENT_PEN);
    wxGraphicsPath path = gc->CreatePath();
    path.AddRoundedRectangle(0, 0, SPOOL_HOLDER_WIDTH, PANEL_HEIGHT, 10);
    gc->FillPath(path);

    // <!-- Text path: "Spool Holder" -->
    // Draw text using Roboto font, color #828280 with 0.3 opacity
    wxFont font(wxFontInfo(10).FaceName("Roboto"));
    if (!font.IsOk()) {
        font = wxFont(wxFontInfo(10).Family(wxFONTFAMILY_DEFAULT));
    }
    gc->SetFont(font, text_color);

    // Get text extents for centering
    double text_width1, text_height1;
    gc->GetTextExtent("Spool", &text_width1, &text_height1);

    double text_width2, text_height2;
    gc->GetTextExtent("Holder", &text_width2, &text_height2);

    // Calculate centered positions
    double text_y = 18.0;  // Starting y position
    double line_spacing = 19.0;

    // Center "Spool" horizontally
    double text_x1 = (SPOOL_HOLDER_WIDTH - text_width1) / 2.0;
    gc->DrawText("Spool", text_x1, text_y);

    // Center "Holder" horizontally
    double text_x2 = (SPOOL_HOLDER_WIDTH - text_width2) / 2.0;
    gc->DrawText("Holder", text_x2, text_y + line_spacing);

    // Restore state
    gc->PopState();
}

void AMSGroupPanel::OnLeftDown(wxMouseEvent& event)
{
    int slot_index = HitTestSlot(event.GetPosition());
    SetSelectedSlot(slot_index);
    //if (slot_index >= 0 && m_slot_configs[slot_index].is_enabled) {
    //    SetSelectedSlot(slot_index);
    //
    //    // Generate a custom event for slot selection
    //    wxCommandEvent evt(wxEVT_BUTTON, GetId());
    //    evt.SetInt(slot_index);
    //    evt.SetEventObject(this);
    //    ProcessWindowEvent(evt);
    //}

    event.Skip();
}
#pragma endregion 


} // namespace GUI
} // namespace Slic3r
