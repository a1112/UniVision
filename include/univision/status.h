#pragma once

#include "univision/export.h"

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace univision {

enum class ErrorCode : std::uint32_t {
  ok = 0,
  invalid_argument,
  invalid_state,
  not_found,
  already_exists,
  timeout,
  unsupported,
  access_denied,
  io_error,
  transport_error,
  device_lost,
  buffer_exhausted,
  adapter_failure,
  internal_error,
};

class UNIVISION_API Status {
 public:
  Status() = default;
  Status(ErrorCode code, std::string message = {});

  [[nodiscard]] static Status success();
  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] ErrorCode code() const noexcept;
  [[nodiscard]] const std::string& message() const noexcept;
  explicit operator bool() const noexcept;

 private:
  ErrorCode code_{ErrorCode::ok};
  std::string message_;
};

template <typename T>
class Result {
 public:
  Result(T value) : value_(std::move(value)) {}
  Result(Status status) : value_(std::move(status)) {}

  [[nodiscard]] bool ok() const noexcept {
    return std::holds_alternative<T>(value_);
  }

  explicit operator bool() const noexcept { return ok(); }

  [[nodiscard]] T& value() & { return std::get<T>(value_); }
  [[nodiscard]] const T& value() const& { return std::get<T>(value_); }
  [[nodiscard]] T&& value() && { return std::get<T>(std::move(value_)); }

  [[nodiscard]] const Status& status() const& {
    if (const auto* status = std::get_if<Status>(&value_)) {
      return *status;
    }
    static const Status ok_status{};
    return ok_status;
  }

 private:
  std::variant<T, Status> value_;
};

}  // namespace univision
