#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__COMPILER_HINTS_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__COMPILER_HINTS_HPP_

#if defined(__clang__) || defined(__GNUC__)
#define UPR_LIKELY(condition) (__builtin_expect(static_cast<bool>(condition), 1))
#define UPR_UNLIKELY(condition) (__builtin_expect(static_cast<bool>(condition), 0))
#else
#define UPR_LIKELY(condition) (condition)
#define UPR_UNLIKELY(condition) (condition)
#endif

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__COMPILER_HINTS_HPP_
