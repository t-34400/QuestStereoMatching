#pragma once

#include <string>
#include <executorch/extension/module/module.h>

namespace executorch_utils {

/**
 * Dump method metadata (inputs/outputs/attributes/backends/memory info)
 * into a human-readable string.
 *
 * @param module      ExecuTorch Module reference
 * @param method_name Target method (default = "forward")
 * @return            String with formatted metadata
 */
std::string dump_method_meta(executorch::extension::Module& module,
                             const std::string& method_name = "forward");

} // namespace executorch_utils
