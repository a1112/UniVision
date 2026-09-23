#pragma once

#include "univision/industrial/contracts.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace univision::industrial {

struct ProfilePoint {
  double x{};
  double z{};
  bool valid{true};
};

struct Profile {
  std::vector<ProfilePoint> points;
  Unit unit{Unit::raw_device_code};
  std::string coordinate_frame;
  std::string calibration_id;
  bool synthetic{false};
};

struct CalibrationProfile {
  std::string id;
  std::string coordinate_frame;
  double scale_to_mm{1.0};
  bool approved{false};
};

struct CircleFitMethod {
  std::string version{"circle-geometric-lsq-v1"};
  std::size_t minimum_points{30};
  double minimum_coverage_deg{120.0};
  double maximum_rms_mm{1.0};
  std::size_t maximum_iterations{50};
  double convergence_mm{1e-10};
};

enum class MeasurementValidity { valid, invalid, synthetic };
enum class Decision { pass, fail, indeterminate, not_evaluated };

struct CircleFitResult {
  std::optional<double> diameter_mm;
  std::optional<double> center_x_mm;
  std::optional<double> center_z_mm;
  std::size_t point_count{};
  double angular_coverage_deg{};
  double residual_rms_mm{};
  bool converged{false};
  MeasurementValidity validity{MeasurementValidity::invalid};
  Decision decision{Decision::not_evaluated};
  std::string method_version;
  std::string calibration_id;
  std::string reason;
};

[[nodiscard]] CircleFitResult fit_circle_diameter(
    const Profile& profile, const CalibrationProfile* calibration,
    const CircleFitMethod& method = {});

// Evaluate an error limit T using |e|+U <= T (pass), |e|-U > T (fail).
[[nodiscard]] Decision verification_decision(double error_mm,
                                             double expanded_uncertainty_mm,
                                             double error_limit_mm);

}  // namespace univision::industrial
