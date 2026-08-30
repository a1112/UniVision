#include "univision/status.h"

namespace univision {

Status::Status(ErrorCode code, std::string message)
    : code_(code), message_(std::move(message)) {}

Status Status::success() { return {}; }

bool Status::ok() const noexcept { return code_ == ErrorCode::ok; }

ErrorCode Status::code() const noexcept { return code_; }

const std::string& Status::message() const noexcept { return message_; }

Status::operator bool() const noexcept { return ok(); }

}  // namespace univision
