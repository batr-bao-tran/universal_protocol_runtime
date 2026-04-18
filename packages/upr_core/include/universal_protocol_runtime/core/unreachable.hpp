#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__UNREACHABLE_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__UNREACHABLE_HPP_

#include <cstdlib>

namespace universal_protocol_runtime {

#if defined(__clang__)
#pragma clang coverage off
#endif
// LCOV_EXCL_START
[[noreturn, clang::no_sanitize("coverage")]] inline void unreachable() noexcept {  // LCOV_EXCL_LINE
#if defined(_MSC_VER)
  __assume(false);  // LCOV_EXCL_LINE
#elif defined(__clang__) || defined(__GNUC__)
  __builtin_unreachable();  // LCOV_EXCL_LINE
#else
  std::abort();  // LCOV_EXCL_LINE
#endif
}
// LCOV_EXCL_STOP
#if defined(__clang__)
#pragma clang coverage on
#endif

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__UNREACHABLE_HPP_
