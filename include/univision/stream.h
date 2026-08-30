#pragma once

#include "univision/status.h"
#include "univision/types.h"

#include <chrono>

namespace univision {

class Stream {
 public:
  virtual ~Stream() = default;
  virtual Status start() = 0;
  virtual Status stop() = 0;
  [[nodiscard]] virtual bool running() const noexcept = 0;
  [[nodiscard]] virtual Result<Frame> wait_next(std::chrono::milliseconds timeout) = 0;
  [[nodiscard]] virtual StreamStatistics statistics() const noexcept = 0;
};

}  // namespace univision
