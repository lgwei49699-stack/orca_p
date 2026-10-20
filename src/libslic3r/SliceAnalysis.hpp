#ifndef slic3r_SliceAnalysis_hpp_
#define slic3r_SliceAnalysis_hpp_

#include <nlohmann/json.hpp>

namespace Slic3r {

class Print;
struct GCodeProcessorResult;

// Read-only summary of an already sliced print and its finalized G-code result.
// Does not slice, change settings, select parameters or perform physical validation.
nlohmann::json make_slice_analysis_report(const Print& print, const GCodeProcessorResult& gcode_result);

} // namespace Slic3r

#endif
