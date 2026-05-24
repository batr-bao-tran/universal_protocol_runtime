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

/**
 * @brief Lightweight status value used for success/error reporting.
 */
class Status {
 public:
  Status() = default;
  /**
   * @brief Destroys the status value.
   * @return No return value.
   */
  ~Status() noexcept = default;

  /**
   * @brief Constructs a non-OK status with code and message.
   * @param code Status code.
   * @param message Human-readable status message.
   * @return No return value.
   */
  Status(StatusCode code, std::string message) : code_(code), message_(std::move(message)) {}

  /**
   * @brief Checks whether the status represents success.
   * @return `true` when the status code is `kOk`.
   */
  bool ok() const { return code_ == StatusCode::kOk; }

  /**
   * @brief Returns the status code.
   * @return Status code value.
   */
  StatusCode code() const { return code_; }

  /**
   * @brief Returns the human-readable status message.
   * @return Status message text.
   */
  std::string_view message() const { return message_; }

  /**
   * @brief Creates an OK status value.
   * @return OK status instance.
   */
  static Status ok_status() { return {}; }

 private:
  StatusCode code_ = StatusCode::kOk;
  std::string message_;
};

/**
 * @brief Converts a status code to a stable string view.
 * @param code Status code value.
 * @return String representation of the status code.
 */
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

/**
 * @brief Creates an invalid-argument status.
 * @param message Human-readable error message.
 * @return Invalid-argument status value.
 */
inline Status invalid_argument(std::string message) { return {StatusCode::kInvalidArgument, std::move(message)}; }

/**
 * @brief Creates a not-found status.
 * @param message Human-readable error message.
 * @return Not-found status value.
 */
inline Status not_found(std::string message) { return {StatusCode::kNotFound, std::move(message)}; }

/**
 * @brief Creates a schema-error status.
 * @param message Human-readable error message.
 * @return Schema-error status value.
 */
inline Status schema_error(std::string message) { return {StatusCode::kSchemaError, std::move(message)}; }

/**
 * @brief Creates a decode-error status.
 * @param message Human-readable error message.
 * @return Decode-error status value.
 */
inline Status decode_error(std::string message) { return {StatusCode::kDecodeError, std::move(message)}; }

/**
 * @brief Creates an I/O-error status.
 * @param message Human-readable error message.
 * @return I/O-error status value.
 */
inline Status io_error(std::string message) { return {StatusCode::kIoError, std::move(message)}; }

/**
 * @brief Creates an exhausted-resource status.
 * @param message Human-readable error message.
 * @return Exhausted status value.
 */
inline Status exhausted(std::string message) { return {StatusCode::kExhausted, std::move(message)}; }

template <typename T>
/**
 * @brief Holds either a value of type `T` or an error status.
 */
class StatusOr {
 public:
  /**
   * @brief Destroys the wrapper.
   * @return No return value.
   */
  ~StatusOr() noexcept(std::is_nothrow_destructible_v<T>) = default;

  /**
   * @brief Constructs a successful wrapper from a copied value.
   * @param value Value to store.
   * @return No return value.
   */
  StatusOr(const T& value) : value_(value) {}

  /**
   * @brief Constructs a successful wrapper from a moved value.
   * @param value Value to store.
   * @return No return value.
   */
  StatusOr(T&& value) : value_(std::move(value)) {}

  /**
   * @brief Constructs an error wrapper from a status.
   * @param status Error status to store.
   * @return No return value.
   */
  StatusOr(Status status) : status_(std::move(status)) {}

  /**
   * @brief Checks whether a value is present.
   * @return `true` when the wrapper contains a value.
   */
  bool ok() const { return value_.has_value(); }

  /**
   * @brief Returns the stored value as a const lvalue reference.
   * @return Stored value reference.
   */
  const T& value() const& { return *value_; }

  /**
   * @brief Returns the stored value as an lvalue reference.
   * @return Stored value reference.
   */
  T& value() & { return *value_; }

  /**
   * @brief Returns the stored value as an rvalue reference.
   * @return Stored value reference.
   */
  T&& value() && { return std::move(*value_); }

  /**
   * @brief Returns the stored error status or an OK status when a value is present.
   * @return Stored status reference.
   */
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
