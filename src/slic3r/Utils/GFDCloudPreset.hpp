#ifndef slic3r_GFDCloudPreset_hpp_
#define slic3r_GFDCloudPreset_hpp_

#include "libslic3r/Preset.hpp"

namespace Slic3r::GFD {

// Embedded process presets may contain only differences from a system parent.
// The project snapshot records the effective values, including inherited values
// and unsaved edits. Never replace those with local defaults or parent profiles.
inline DynamicPrintConfig cloud_process_config(const DynamicPrintConfig& project_config, const DynamicPrintConfig* embedded_config)
{
    DynamicPrintConfig result;
    for (const std::string& key : Preset::print_options()) {
        const ConfigOption* option = project_config.option(key);
        if (option == nullptr && embedded_config != nullptr)
            option = embedded_config->option(key);
        if (option != nullptr)
            result.set_key_value(key, option->clone());
    }
    return result;
}

} // namespace Slic3r::GFD

#endif
