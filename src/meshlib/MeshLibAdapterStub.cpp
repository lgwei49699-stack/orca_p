#include "MeshLibAdapter.hpp"

namespace OrcaMeshLib {

bool available() noexcept { return false; }

Result rebuild(const Mesh&, const Options&)
{
    Result result;
    result.status  = "backend_unavailable";
    result.message = "This Orca build does not include the MeshLib local-evaluation backend.";
    result.report_json =
        R"({"schema":"meshlib_adapter_v1","engine":"meshlib","status":"backend_unavailable","execution":"in_process","candidate_validated":false,"print_ready":false})";
    return result;
}

} // namespace OrcaMeshLib
