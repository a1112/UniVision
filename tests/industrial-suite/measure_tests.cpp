#include "univision/industrial/measure.h"

#include <cmath>
#include <iostream>
#include <numbers>

using namespace univision::industrial;

namespace {
int failures = 0;
void check(bool ok, const char* name) {
  if (!ok) {
    std::cerr << "failed: " << name << '\n';
    ++failures;
  }
}

Profile circle(double start_deg = 0, double sweep_deg = 360) {
  Profile p;
  p.unit = Unit::millimeter;
  p.coordinate_frame = "profile-xz-mm";
  p.calibration_id = "synthetic-identity-v1";
  p.synthetic = true;
  for (int i = 0; i < 72; ++i) {
    const double angle = (start_deg + sweep_deg * i / 72.0) *
                         std::numbers::pi / 180.0;
    p.points.push_back({2.0 + 10.0 * std::cos(angle),
                        -3.0 + 10.0 * std::sin(angle), true});
  }
  return p;
}

CalibrationProfile calibration() {
  CalibrationProfile c;
  c.id = "synthetic-identity-v1";
  c.coordinate_frame = "profile-xz-mm";
  c.scale_to_mm = 1.0;
  c.approved = false;
  return c;
}
}  // namespace

int main() {
  const auto c = calibration();
  auto ideal = fit_circle_diameter(circle(), &c);
  check(ideal.validity == MeasurementValidity::synthetic,
        "synthetic result labeled synthetic");
  check(ideal.diameter_mm && std::abs(*ideal.diameter_mm - 20.0) < 1e-8,
        "ideal circle diameter");
  check(ideal.residual_rms_mm < 1e-8, "ideal circle residual");
  check(ideal.decision == Decision::not_evaluated,
        "synthetic result cannot pass production decision");

  auto noisy = circle();
  for (std::size_t i = 0; i < noisy.points.size(); ++i) {
    noisy.points[i].x += 0.001 * std::sin(static_cast<double>(i) * 3.0);
  }
  auto n = fit_circle_diameter(noisy, &c);
  check(n.diameter_mm && std::abs(*n.diameter_mm - 20.0) < 0.01,
        "noisy synthetic circle within stated mathematical tolerance");

  auto raw = circle();
  raw.unit = Unit::raw_device_code;
  check(fit_circle_diameter(raw, nullptr).validity == MeasurementValidity::invalid,
        "raw code without calibration is invalid");
  check(!fit_circle_diameter(raw, nullptr).diameter_mm,
        "invalid result has no stale diameter");
  auto approved = c;
  approved.approved = true;
  approved.scale_to_mm = 0.5;
  auto raw_profile = circle();
  raw_profile.synthetic = false;
  raw_profile.unit = Unit::raw_device_code;
  for (auto& point : raw_profile.points) {
    point.x *= 2;
    point.z *= 2;
  }
  auto converted = fit_circle_diameter(raw_profile, &approved);
  check(converted.validity == MeasurementValidity::valid &&
            converted.diameter_mm &&
            std::abs(*converted.diameter_mm - 20.0) < 1e-8,
        "approved raw-code calibration converts to millimeters");

  auto short_arc = circle(0, 10);
  check(fit_circle_diameter(short_arc, &c).validity == MeasurementValidity::invalid,
        "degenerate arc is invalid");
  auto few = circle();
  few.points.resize(20);
  check(fit_circle_diameter(few, &c).validity == MeasurementValidity::invalid,
        "too few points are invalid");
  auto outlier = circle();
  outlier.points[0].x += 100;
  check(fit_circle_diameter(outlier, &c).validity ==
            MeasurementValidity::invalid,
        "gross outlier does not yield a valid diameter");

  check(verification_decision(0.012, 0.008, 0.03) == Decision::pass,
        "guard band pass");
  check(verification_decision(0.025, 0.010, 0.03) == Decision::indeterminate,
        "guard band indeterminate");
  check(verification_decision(0.050, 0.008, 0.03) == Decision::fail,
        "guard band fail");
  check(verification_decision(0.012, -1.0, 0.03) == Decision::not_evaluated,
        "invalid uncertainty cannot yield decision");

  if (failures != 0) return 1;
  std::cout << "industrial measure checks passed\n";
}
