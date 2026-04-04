#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__UNREACHABLE_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__UNREACHABLE_HPP_

#include <cstdlib>

namespace universal_protocol_runtime {

[[noreturn]] inline void unreachable() noexcept {
#if defined(_MSC_VER)
  __assume(false);
#elif defined(__clang__) || defined(__GNUC__)
  __builtin_unreachable();
#else
  std::abort();
#endif
}

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__UNREACHABLE_HPP_
