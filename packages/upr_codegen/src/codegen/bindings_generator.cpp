#include "universal_protocol_runtime/codegen/bindings_generator.hpp"

#include <algorithm>  // IWYU pragma: keep
#include <cctype>
#include <cstddef>        // IWYU pragma: keep
#include <iomanip>        // IWYU pragma: keep
#include <span>           // IWYU pragma: keep
#include <sstream>        // IWYU pragma: keep
#include <string_view>    // IWYU pragma: keep
#include <unordered_set>  // IWYU pragma: keep
#include <vector>         // IWYU pragma: keep

#include "universal_protocol_runtime/core/types.hpp"  // IWYU pragma: keep

namespace universal_protocol_runtime {
namespace {

#include "bindings_generator_common.inc"
#include "bindings_generator_cpp.inc"
#include "bindings_generator_python.inc"

}  // namespace

#include "bindings_generator_cpp_module.inc"
#include "bindings_generator_python_module.inc"

namespace {

#include "bindings_generator_typescript.inc"

}  // namespace

#include "bindings_generator_typescript_module.inc"

}  // namespace universal_protocol_runtime
