#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UTILS_INCLUDE_UTILS__STATUS_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UTILS_INCLUDE_UTILS__STATUS_HPP_
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace universal_protocol_runtime {

enum class StatusCode {
  kOk,
  kInvalidArgument,
  kNotFound,
  kSchemaError,
  kDecodeError,
  kIoError,
  kExhausted,
};

class Status {
 public:
  Status() = default;
  ~Status() noexcept = default;

  Status(StatusCode code, std::string message) : code_(code), message_(std::move(message)) {}

  bool ok() const { return code_ == StatusCode::kOk; }

  StatusCode code() const { return code_; }

  std::string_view message() const { return message_; }

  static Status ok_status() { return {}; }

 private:
  StatusCode code_ = StatusCode::kOk;
  std::string message_;
};

inline std::string_view to_string(StatusCode code) {
  switch (code) {
    case StatusCode::kOk:
      return "ok";
    case StatusCode::kInvalidArgument:
      return "invalid_argument";
    case StatusCode::kNotFound:
      return "not_found";
    case StatusCode::kSchemaError:
      return "schema_error";
    case StatusCode::kDecodeError:
      return "decode_error";
    case StatusCode::kIoError:
      return "io_error";
    case StatusCode::kExhausted:
      return "exhausted";
  }
  return "unknown";
}

inline Status invalid_argument(std::string message) { return {StatusCode::kInvalidArgument, std::move(message)}; }

inline Status not_found(std::string message) { return {StatusCode::kNotFound, std::move(message)}; }

inline Status schema_error(std::string message) { return {StatusCode::kSchemaError, std::move(message)}; }

inline Status decode_error(std::string message) { return {StatusCode::kDecodeError, std::move(message)}; }

inline Status io_error(std::string message) { return {StatusCode::kIoError, std::move(message)}; }

inline Status exhausted(std::string message) { return {StatusCode::kExhausted, std::move(message)}; }

template <typename T>
class StatusOr {
 public:
  ~StatusOr() noexcept(std::is_nothrow_destructible_v<T>) = default;

  StatusOr(const T& value) : value_(value) {}

  StatusOr(T&& value) : value_(std::move(value)) {}

  StatusOr(Status status) : status_(std::move(status)) {}

  bool ok() const { return value_.has_value(); }

  const T& value() const& { return *value_; }

  T& value() & { return *value_; }

  T&& value() && { return std::move(*value_); }

  const Status& status() const {
    if (ok()) {
      static const Status kOkStatus = Status::ok_status();
      return kOkStatus;
    }
    return status_;
  }

 private:
  std::optional<T> value_;
  Status status_ = Status::ok_status();
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UTILS_INCLUDE_UTILS__STATUS_HPP_
