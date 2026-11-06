#include "ModelReconstructionDialog.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelReconstruction.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/display.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace Slic3r { namespace GUI {
namespace {

struct PreviewGeometry
{
    std::shared_ptr<const TriangleMesh> mesh;
    std::vector<Vec3d>                  vertices;
    BoundingBoxf3                       bounds;

    explicit PreviewGeometry(const ModelObject& object) : mesh(object.volumes.front()->mesh_ptr())
    {
        const Transform3d transform = object.instances.front()->get_matrix() * object.volumes.front()->get_matrix();
        vertices.reserve(mesh->its.vertices.size());
        for (const Vec3f& vertex : mesh->its.vertices) {
            vertices.emplace_back(transform * vertex.cast<double>());
            bounds.merge(vertices.back());
        }
    }
};

using PreviewSegment = std::array<Vec2d, 2>;

std::vector<PreviewSegment> make_section(const PreviewGeometry& geometry, int axis, double position)
{
    std::vector<PreviewSegment> segments;
    const int                   x_axis = axis == 0 ? 1 : 0;
    const int                   y_axis = axis == 2 ? 1 : 2;
    for (const Vec3i32& face : geometry.mesh->its.indices) {
        std::array<Vec3d, 3> intersections;
        size_t               count = 0;
        for (int edge = 0; edge < 3; ++edge) {
            const Vec3d& a = geometry.vertices[face[edge]];
            const Vec3d& b = geometry.vertices[face[(edge + 1) % 3]];
            // Half-open intervals avoid adding a plane vertex twice. Coplanar
            // triangles are omitted; these are cross-sections, not silhouettes.
            if ((a[axis] <= position && b[axis] > position) || (b[axis] <= position && a[axis] > position))
                intersections[count++] = a + (position - a[axis]) / (b[axis] - a[axis]) * (b - a);
        }
        if (count == 2)
            segments.push_back(
                {Vec2d(intersections[0][x_axis], intersections[0][y_axis]), Vec2d(intersections[1][x_axis], intersections[1][y_axis])});
    }
    return segments;
}

class ReconstructionSectionPanel final : public wxPanel
{
public:
    ReconstructionSectionPanel(
        wxWindow* parent, const PreviewGeometry& source, const PreviewGeometry& candidate, const BoundingBoxf3& bounds, int axis)
        : wxPanel(parent)
        , m_source_geometry(source)
        , m_candidate_geometry(candidate)
        , m_bounds(bounds)
        , m_axis(axis)
        , m_position(bounds.center()[axis])
        , m_source(make_section(source, axis, m_position))
        , m_candidate(make_section(candidate, axis, m_position))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(FromDIP(wxSize(220, 190)));
        Bind(wxEVT_PAINT, &ReconstructionSectionPanel::paint, this);
    }

    void set_position(double position)
    {
        if (position == m_position)
            return;
        m_position  = position;
        m_source    = make_section(m_source_geometry, m_axis, position);
        m_candidate = make_section(m_candidate_geometry, m_axis, position);
        Refresh();
    }

private:
    void paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();
        const wxSize size   = GetClientSize();
        const int    x_axis = m_axis == 0 ? 1 : 0;
        const int    y_axis = m_axis == 2 ? 1 : 2;
        const double width  = std::max(m_bounds.size()[x_axis], 1.e-6);
        const double height = std::max(m_bounds.size()[y_axis], 1.e-6);
        const double scale  = std::min(std::max(1, size.x - FromDIP(24)) / width, std::max(1, size.y - FromDIP(52)) / height);
        const auto   point  = [&](const Vec2d& p) {
            return wxPoint(static_cast<int>(size.x * 0.5 + (p.x() - m_bounds.center()[x_axis]) * scale),
                              static_cast<int>((size.y + FromDIP(24)) * 0.5 - (p.y() - m_bounds.center()[y_axis]) * scale));
        };
        dc.SetTextForeground(GetForegroundColour());
        const wxString plane = m_axis == 2 ? "XY" : (m_axis == 1 ? "XZ" : "YZ");
        dc.DrawText(plane + wxString::Format("  %.3f mm", m_position), FromDIP(8), FromDIP(6));
        dc.SetPen(wxPen(wxColour(159, 84, 209), FromDIP(2), wxPENSTYLE_SHORT_DASH));
        for (const PreviewSegment& segment : m_source)
            dc.DrawLine(point(segment[0]), point(segment[1]));
        dc.SetPen(wxPen(wxColour(0, 158, 150), FromDIP(1)));
        for (const PreviewSegment& segment : m_candidate)
            dc.DrawLine(point(segment[0]), point(segment[1]));
    }

    // The two preview geometries outlive the modal dialog and all its panels.
    const PreviewGeometry&      m_source_geometry;
    const PreviewGeometry&      m_candidate_geometry;
    BoundingBoxf3               m_bounds;
    int                         m_axis;
    double                      m_position;
    std::vector<PreviewSegment> m_source;
    std::vector<PreviewSegment> m_candidate;
};

wxStaticText* add_wrapped_text(wxWindow& parent, wxSizer& sizer, const wxString& text, int width)
{
    auto* label = new wxStaticText(&parent, wxID_ANY, text);
    label->Wrap(parent.FromDIP(width));
    sizer.Add(label, 0, wxEXPAND | wxALL, parent.FromDIP(8));
    return label;
}

wxString dimensions(const Vec3d& size) { return wxString::Format("%.3f × %.3f × %.3f mm", size.x(), size.y(), size.z()); }

void add_experimental_warning(wxWindow& parent, wxBoxSizer& sizer, int width)
{
    auto*  warning = add_wrapped_text(parent, sizer,
                                      _L("Experimental approximate candidate. Automatic diagnostic slicing, demonstrated repair benefit "
                                          "and cross-platform release validation are not yet complete."),
                                      width);
    wxFont font    = warning->GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    warning->SetFont(font);
}

wxScrolledWindow* add_scrollable_content(wxDialog& dialog, wxBoxSizer& root, wxBoxSizer* content_sizer)
{
    auto* content = new wxScrolledWindow(&dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL | wxTAB_TRAVERSAL);
    content->SetScrollRate(dialog.FromDIP(10), dialog.FromDIP(10));
    // The viewport may shrink without changing the content's virtual minimum.
    // Action buttons belong to the dialog root, never to this scrolling area.
    content->SetMinSize(wxSize(1, 1));
    content->SetSizer(content_sizer);
    root.Add(content, 1, wxEXPAND);
    return content;
}

void fit_scrollable_dialog(wxDialog& dialog, wxBoxSizer* root, wxScrolledWindow& content)
{
    dialog.SetSizer(root);
    const wxSize body_size = content.GetSizer()->CalcMin();
    const wxSize root_min  = root->CalcMin();
    const wxSize preferred = dialog.ClientToWindowSize(
        wxSize(std::max(body_size.x + dialog.FromDIP(24), root_min.x), body_size.y + root_min.y + dialog.FromDIP(24)));
    const wxSize minimum = dialog.ClientToWindowSize(wxSize(std::max(root_min.x, dialog.FromDIP(320)), root_min.y + dialog.FromDIP(120)));

    const int display_index = wxDisplay::GetFromWindow(dialog.GetParent() ? dialog.GetParent() : &dialog);
    wxRect    work_area     = wxDisplay(display_index == wxNOT_FOUND ? 0u : static_cast<unsigned>(display_index)).GetClientArea();
    const int margin        = std::min({dialog.FromDIP(12), work_area.width / 4, work_area.height / 4});
    work_area.Deflate(margin);
    const wxSize available = work_area.GetSize();
    dialog.SetMinSize(wxSize(std::min(minimum.x, available.x), std::min(minimum.y, available.y)));
    dialog.SetSize(wxSize(std::min(preferred.x, available.x), std::min(preferred.y, available.y)));
    dialog.Layout();
    content.FitInside();
    dialog.CentreOnParent();
    const wxSize  size     = dialog.GetSize();
    const wxPoint position = dialog.GetPosition();
    dialog.Move(std::clamp(position.x, work_area.x, std::max(work_area.x, work_area.GetRight() - size.x + 1)),
                std::clamp(position.y, work_area.y, std::max(work_area.y, work_area.GetBottom() - size.y + 1)));
}

} // namespace

bool show_model_reconstruction_settings(wxWindow* parent, const BoundingBoxf3& bounds_mm, ModelReconstructionOptions& options)
{
    if (!model_reconstruction_available()) {
        wxMessageBox(_L("This Orca build does not include the MeshLib SDK backend."), _L("Reconstruct model"), wxOK | wxICON_WARNING,
                     parent);
        return false;
    }
    wxDialog dialog(parent, wxID_ANY, _L("Reconstruct model"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto*    root          = new wxBoxSizer(wxVERTICAL);
    auto*    content_sizer = new wxBoxSizer(wxVERTICAL);
    auto*    content       = add_scrollable_content(dialog, *root, content_sizer);
    add_experimental_warning(*content, *content_sizer, 570);
    add_wrapped_text(
        *content, *content_sizer,
        _L("This creates an approximate outer shape. It may close holes, fill cavities, merge nearby surfaces or remove fine details. "
           "It does not restore precise holes, slots or internal cavities. The original model is kept until you explicitly apply a "
           "candidate."),
        570);

    auto* grid = new wxFlexGridSizer(2, dialog.FromDIP(8), dialog.FromDIP(12));
    grid->AddGrowableCol(1);
    const auto add_parameter = [&](const wxString& label, double value, double minimum, double maximum, double increment) {
        grid->Add(new wxStaticText(content, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        auto* control = new wxSpinCtrlDouble(content, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, minimum,
                                             maximum, value, increment);
        control->SetDigits(3);
        grid->Add(control, 1, wxEXPAND);
        return control;
    };
    auto* voxel     = add_parameter(_L("Voxel size (mm)"), options.voxel_mm > 0. ? options.voxel_mm : 0.3, 0.001, 100.0, 0.01);
    auto* deviation = add_parameter(_L("Surface deviation warning threshold (mm)"), options.mesh.max_deviation_mm, 0.001, 100.0, 0.05);
    content_sizer->Add(grid, 0, wxEXPAND | wxALL, dialog.FromDIP(12));
    add_wrapped_text(*content, *content_sizer,
                     _L("MeshLib reconstructs the model at the selected voxel size. Smaller voxels require more time and memory. "
                        "No automatic coarsening or fallback is performed. Surface deviation is a warning, not a rejection limit."),
                     570);
    add_wrapped_text(*content, *content_sizer,
                     wxString::Format(_L("Cooperative budget: %u seconds, %u MiB for the whole application. "
                                         "Cancellation waits for reconstruction and validation to return; the original model is kept."),
                                      options.limits.timeout_ms / 1000, static_cast<unsigned>(options.limits.memory_limit_mb)),
                     570);
    auto*      cost_label  = add_wrapped_text(*content, *content_sizer, wxEmptyString, 570);
    const auto update_cost = [cost_label, content, bounds_mm](double value) {
        const char* warning = model_reconstruction_grid_warning(bounds_mm, value);
        if (warning) {
            cost_label->SetLabel(wxGetTranslation(from_u8(warning)));
            cost_label->Wrap(content->FromDIP(570));
        }
        cost_label->Show(warning != nullptr);
        content->Layout();
        content->FitInside();
    };
    update_cost(voxel->GetValue());
    voxel->Bind(wxEVT_TEXT, [update_cost](wxCommandEvent& event) {
        double value = 0.;
        event.GetString().ToDouble(&value);
        update_cost(value);
        event.Skip();
    });
    voxel->Bind(wxEVT_SPINCTRLDOUBLE, [update_cost](wxSpinDoubleEvent& event) {
        update_cost(event.GetValue());
        event.Skip();
    });
    auto* consent = new wxCheckBox(content, wxID_ANY, _L("I understand that reconstruction changes the model's shape."));
    content_sizer->Add(consent, 0, wxALL, dialog.FromDIP(12));
    auto* buttons  = dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    auto* generate = wxDynamicCast(dialog.FindWindow(wxID_OK), wxButton);
    wxDynamicCast(dialog.FindWindow(wxID_CANCEL), wxButton)->SetLabel(_L("Cancel"));
    generate->SetLabel(_L("Generate candidate"));
    generate->Enable(false);
    consent->Bind(wxEVT_CHECKBOX, [generate, consent](wxCommandEvent&) { generate->Enable(consent->GetValue()); });
    generate->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        if (!consent->GetValue())
            return;
        if (!std::isfinite(voxel->GetValue()) || voxel->GetValue() <= 0 || !std::isfinite(deviation->GetValue()) ||
            deviation->GetValue() <= 0) {
            wxMessageBox(_L("Reconstruction options require finite positive numbers."), _L("Reconstruct model"), wxOK | wxICON_WARNING,
                         &dialog);
            return;
        }
        dialog.EndModal(wxID_OK);
    });
    root->Add(buttons, 0, wxEXPAND | wxALL, dialog.FromDIP(12));
    wxGetApp().UpdateDlgDarkUI(&dialog);
    fit_scrollable_dialog(dialog, root, *content);
    if (dialog.ShowModal() != wxID_OK)
        return false;
    options.voxel_mm              = voxel->GetValue();
    options.mesh.max_deviation_mm = deviation->GetValue();
    return true;
}

bool show_model_reconstruction_preview(wxWindow*                        parent,
                                       const ModelObject&               original,
                                       const ModelObject&               candidate,
                                       const ModelReconstructionResult& result,
                                       double                           deviation_limit_mm,
                                       bool&                            place_on_bed)
{
    const PreviewGeometry source(original);
    const PreviewGeometry rebuilt(candidate);
    BoundingBoxf3         bounds = source.bounds;
    bounds.merge(rebuilt.bounds);
    wxDialog dialog(parent, wxID_ANY, _L("Review reconstructed model"), wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto*    root          = new wxBoxSizer(wxVERTICAL);
    auto*    content_sizer = new wxBoxSizer(wxVERTICAL);
    auto*    content       = add_scrollable_content(dialog, *root, content_sizer);
    add_experimental_warning(*content, *content_sizer, 770);
    add_wrapped_text(*content, *content_sizer,
                     _L("Cross-sections at the same physical coordinates: dashed purple = original; solid teal = candidate. "
                        "Set the X/Y/Z positions and update the sections to inspect specific holes or cavities. "
                        "Selected sections do not prove that every feature is preserved."),
                     770);
    auto*                                      views = new wxBoxSizer(wxHORIZONTAL);
    std::array<ReconstructionSectionPanel*, 3> panels;
    std::array<wxSpinCtrlDouble*, 3>           positions;
    for (int axis : {2, 1, 0}) {
        auto* column = new wxBoxSizer(wxVERTICAL);
        panels[axis] = new ReconstructionSectionPanel(content, source, rebuilt, bounds, axis);
        column->Add(panels[axis], 1, wxEXPAND);
        auto*          row        = new wxBoxSizer(wxHORIZONTAL);
        const wxString coordinate = axis == 2 ? "Z (mm)" : (axis == 1 ? "Y (mm)" : "X (mm)");
        row->Add(new wxStaticText(content, wxID_ANY, coordinate), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, dialog.FromDIP(6));
        positions[axis] = new wxSpinCtrlDouble(content, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
                                               bounds.min[axis], bounds.max[axis], bounds.center()[axis], 0.1);
        positions[axis]->SetDigits(3);
        positions[axis]->SetName(coordinate);
        row->Add(positions[axis], 1, wxEXPAND);
        column->Add(row, 0, wxEXPAND | wxTOP, dialog.FromDIP(4));
        views->Add(column, 1, wxEXPAND | wxALL, dialog.FromDIP(4));
    }
    content_sizer->Add(views, 1, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(8));
    auto* update_sections = new wxButton(content, wxID_ANY, _L("Update cross-sections"));
    update_sections->Bind(wxEVT_BUTTON, [panels, positions](wxCommandEvent&) {
        for (int axis = 0; axis < 3; ++axis)
            panels[axis]->set_position(positions[axis]->GetValue());
    });
    content_sizer->Add(update_sections, 0, wxALIGN_RIGHT | wxALL, dialog.FromDIP(8));
    add_wrapped_text(*content, *content_sizer,
                     _L("Original size:") + " " + dimensions(source.bounds.size()) + "\n" + _L("Candidate size:") + " " +
                         dimensions(rebuilt.bounds.size()) + "\n" + _L("Bottom height before / after (mm):") +
                         wxString::Format(" %.3f / %.3f   (ΔZ %+.3f)", source.bounds.min.z(), rebuilt.bounds.min.z(),
                                          rebuilt.bounds.min.z() - source.bounds.min.z()) +
                         "\n" + _L("Surface deviation warning threshold (mm)") + wxString::Format(": %.3f", deviation_limit_mm),
                     770);
    // The core owns the validation report. A missing metric must remain unknown,
    // and must never be presented as zero deviation or a successful slice.
    const auto metrics_it = result.report.find("metrics");
    const auto metric     = [&](const char* name) {
        if (metrics_it != result.report.end() && metrics_it->is_object()) {
            const auto value = metrics_it->find(name);
            if (value != metrics_it->end() && value->is_number()) {
                const double number = value->get<double>();
                if (std::isfinite(number))
                    return wxString::Format("%.5f", number);
            }
        }
        return _L("Not available");
    };
    add_wrapped_text(*content, *content_sizer,
                     _L("Sampled distance, original to candidate / candidate to original (mm):") + " " +
                         metric("sampled_source_to_candidate_mm") + " / " + metric("sampled_candidate_to_source_mm") + "\n" +
                         _L("Bounding box deviation (mm):") + " " + metric("bbox_deviation_mm") + "\n" + _L("Voxel size (mm)") + ": " +
                         wxString::Format("%.5f", result.report.at("parameters").at("voxel_mm").get<double>()) + "\n" +
                         _L("Sampled distances are not a guaranteed maximum surface error."),
                     770);
    const auto engine = result.report.find("engine");
    if (engine != result.report.end() && engine->is_object()) {
        const auto structure = engine->find("structure_preservation");
        if (structure != engine->end() && structure->value("topology_comparison_complete", false) &&
            structure->at("topology_changed").get<bool>()) {
            auto*  warning = add_wrapped_text(*content, *content_sizer,
                                              _L("Reconstruction changed the connectivity of closed surfaces or through-holes. "
                                                  "Inspect holes, cavities and separate parts before applying the candidate, even if "
                                                  "surface deviation is below the warning threshold."),
                                              770);
            wxFont font    = warning->GetFont();
            font.SetWeight(wxFONTWEIGHT_BOLD);
            warning->SetFont(font);
        }
    }
    if (metrics_it != result.report.end() && metrics_it->value("deviation_warning", false))
        add_wrapped_text(*content, *content_sizer,
                         _L("Surface deviation exceeds the warning threshold. The candidate is valid, but its shape needs review."), 770);
    auto* bed = new wxCheckBox(content, wxID_ANY, _L("Move the candidate in Z so its lowest point rests on the bed"));
    bed->SetValue(false);
    content_sizer->Add(bed, 0, wxALL, dialog.FromDIP(12));
    add_wrapped_text(*content, *content_sizer,
                     _L("Diagnostic slicing has not been run. Applying this candidate does not mean it is ready to print. "
                        "Slice it with the current printer, filament and process settings, then inspect every layer before printing."),
                     770);
    auto* buttons = dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxDynamicCast(dialog.FindWindow(wxID_OK), wxButton)->SetLabel(_L("Apply candidate"));
    wxDynamicCast(dialog.FindWindow(wxID_CANCEL), wxButton)->SetLabel(_L("Keep original"));
    root->Add(buttons, 0, wxEXPAND | wxALL, dialog.FromDIP(12));
    wxGetApp().UpdateDlgDarkUI(&dialog);
    fit_scrollable_dialog(dialog, root, *content);
    if (dialog.ShowModal() != wxID_OK)
        return false;
    place_on_bed = bed->GetValue();
    return true;
}

}} // namespace Slic3r::GUI
