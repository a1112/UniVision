#include "univision/industrial/measure.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace univision::industrial {
namespace {

struct Point { double x; double z; };

bool solve3(std::array<std::array<double, 4>, 3> a,
            std::array<double, 3>& out) {
  for (std::size_t col = 0; col < 3; ++col) {
    std::size_t pivot = col;
    for (std::size_t row = col + 1; row < 3; ++row)
      if (std::abs(a[row][col]) > std::abs(a[pivot][col])) pivot = row;
    if (std::abs(a[pivot][col]) < 1e-14 || !std::isfinite(a[pivot][col]))
      return false;
    std::swap(a[pivot], a[col]);
    const double scale = a[col][col];
    for (std::size_t j = col; j < 4; ++j) a[col][j] /= scale;
    for (std::size_t row = 0; row < 3; ++row) {
      if (row == col) continue;
      const double factor = a[row][col];
      for (std::size_t j = col; j < 4; ++j)
        a[row][j] -= factor * a[col][j];
    }
  }
  for (std::size_t i = 0; i < 3; ++i) out[i] = a[i][3];
  return std::isfinite(out[0]) && std::isfinite(out[1]) &&
         std::isfinite(out[2]);
}

double cost(const std::vector<Point>& points, double cx, double cz, double r) {
  double sum = 0;
  for (const auto& p : points) {
    const double residual = std::hypot(p.x - cx, p.z - cz) - r;
    sum += residual * residual;
  }
  return sum;
}

double angular_coverage(const std::vector<Point>& points, double cx,
                        double cz) {
  std::vector<double> angles;
  angles.reserve(points.size());
  for (const auto& p : points) {
    double a = std::atan2(p.z - cz, p.x - cx);
    if (a < 0) a += 2 * std::numbers::pi;
    angles.push_back(a);
  }
  std::sort(angles.begin(), angles.end());
  double max_gap = 0;
  for (std::size_t i = 1; i < angles.size(); ++i)
    max_gap = std::max(max_gap, angles[i] - angles[i - 1]);
  max_gap = std::max(max_gap,
                     angles.front() + 2 * std::numbers::pi - angles.back());
  return (2 * std::numbers::pi - max_gap) * 180.0 / std::numbers::pi;
}

CircleFitResult invalid(CircleFitResult result, std::string reason) {
  result.reason = std::move(reason);
  return result;
}

}  // namespace

CircleFitResult fit_circle_diameter(const Profile& profile,
                                    const CalibrationProfile* calibration,
                                    const CircleFitMethod& method) {
  CircleFitResult result;
  result.method_version = method.version;
  result.calibration_id = profile.calibration_id;
  if (method.version.empty() || method.minimum_points < 3 ||
      !std::isfinite(method.minimum_coverage_deg) ||
      method.minimum_coverage_deg <= 0 || method.minimum_coverage_deg >= 360 ||
      !std::isfinite(method.maximum_rms_mm) || method.maximum_rms_mm <= 0 ||
      method.maximum_iterations == 0 ||
      !std::isfinite(method.convergence_mm) || method.convergence_mm <= 0)
    return invalid(std::move(result), "invalid method profile");
  if (calibration == nullptr || calibration->id.empty() ||
      calibration->id != profile.calibration_id ||
      calibration->coordinate_frame != profile.coordinate_frame ||
      !std::isfinite(calibration->scale_to_mm) ||
      calibration->scale_to_mm <= 0 ||
      (!profile.synthetic && !calibration->approved))
    return invalid(std::move(result), "calibration missing or inapplicable");
  double scale = 0;
  switch (profile.unit) {
    case Unit::millimeter: scale = 1.0; break;
    case Unit::meter: scale = 1000.0; break;
    case Unit::raw_device_code:
      if (!calibration->approved)
        return invalid(std::move(result), "raw code requires approved calibration");
      scale = calibration->scale_to_mm;
      break;
    case Unit::unitless:
      return invalid(std::move(result), "profile has no length unit");
  }
  std::vector<Point> points;
  points.reserve(profile.points.size());
  for (const auto& p : profile.points) {
    if (!p.valid) continue;
    if (!std::isfinite(p.x) || !std::isfinite(p.z))
      return invalid(std::move(result), "non-finite valid point");
    const double x = p.x * scale, z = p.z * scale;
    if (!std::isfinite(x) || !std::isfinite(z))
      return invalid(std::move(result), "scaled point overflow");
    points.push_back({x, z});
  }
  result.point_count = points.size();
  if (points.size() < method.minimum_points)
    return invalid(std::move(result), "insufficient valid points");

  double mean_x = 0, mean_z = 0;
  for (const auto& p : points) { mean_x += p.x; mean_z += p.z; }
  mean_x /= points.size(); mean_z /= points.size();
  std::array<std::array<double, 4>, 3> normal{};
  for (const auto& p : points) {
    const double x = p.x - mean_x, z = p.z - mean_z;
    const double b = x * x + z * z;
    const std::array<double, 3> row{x, z, 1.0};
    for (std::size_t i = 0; i < 3; ++i) {
      for (std::size_t j = 0; j < 3; ++j) normal[i][j] += row[i] * row[j];
      normal[i][3] += row[i] * b;
    }
  }
  std::array<double, 3> initial{};
  if (!solve3(normal, initial))
    return invalid(std::move(result), "degenerate circle geometry");
  double cx = initial[0] / 2 + mean_x;
  double cz = initial[1] / 2 + mean_z;
  double radius_squared = initial[2] +
                          (initial[0] * initial[0] + initial[1] * initial[1]) / 4;
  if (!std::isfinite(radius_squared) || radius_squared <= 0)
    return invalid(std::move(result), "invalid initial radius");
  double radius = std::sqrt(radius_squared);
  result.angular_coverage_deg = angular_coverage(points, cx, cz);
  if (result.angular_coverage_deg < method.minimum_coverage_deg)
    return invalid(std::move(result), "insufficient angular coverage");

  double current_cost = cost(points, cx, cz, radius);
  double damping = 1e-9;
  for (std::size_t iter = 0; iter < method.maximum_iterations; ++iter) {
    std::array<std::array<double, 4>, 3> h{};
    bool singular_point = false;
    for (const auto& p : points) {
      const double dx = p.x - cx, dz = p.z - cz;
      const double distance = std::hypot(dx, dz);
      if (distance <= 1e-14) { singular_point = true; break; }
      const double residual = distance - radius;
      const std::array<double, 3> jac{-dx / distance, -dz / distance, -1.0};
      for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) h[i][j] += jac[i] * jac[j];
        h[i][3] -= jac[i] * residual;
      }
    }
    if (singular_point)
      return invalid(std::move(result), "point coincides with fitted center");
    for (std::size_t i = 0; i < 3; ++i) h[i][i] += damping;
    std::array<double, 3> delta{};
    if (!solve3(h, delta))
      return invalid(std::move(result), "geometric fit is singular");
    const double next_radius = radius + delta[2];
    const double next_cost = next_radius > 0
        ? cost(points, cx + delta[0], cz + delta[1], next_radius)
        : std::numeric_limits<double>::infinity();
    if (std::isfinite(next_cost) && next_cost <= current_cost + 1e-20) {
      cx += delta[0]; cz += delta[1]; radius = next_radius;
      current_cost = next_cost;
      damping = std::max(damping / 10, 1e-12);
      if (std::hypot(delta[0], delta[1]) < method.convergence_mm &&
          std::abs(delta[2]) < method.convergence_mm) {
        result.converged = true;
        break;
      }
    } else {
      damping *= 10;
    }
  }
  if (!result.converged)
    return invalid(std::move(result), "geometric fit did not converge");
  result.residual_rms_mm = std::sqrt(current_cost / points.size());
  if (!std::isfinite(result.residual_rms_mm) ||
      result.residual_rms_mm > method.maximum_rms_mm)
    return invalid(std::move(result), "fit residual exceeds method limit");
  result.diameter_mm = 2 * radius;
  result.center_x_mm = cx;
  result.center_z_mm = cz;
  result.validity = profile.synthetic ? MeasurementValidity::synthetic
                                      : MeasurementValidity::valid;
  return result;
}

Decision verification_decision(double error_mm, double uncertainty_mm,
                               double limit_mm) {
  if (!std::isfinite(error_mm) || !std::isfinite(uncertainty_mm) ||
      !std::isfinite(limit_mm) || uncertainty_mm < 0 || limit_mm <= 0)
    return Decision::not_evaluated;
  const double magnitude = std::abs(error_mm);
  if (magnitude + uncertainty_mm <= limit_mm) return Decision::pass;
  if (magnitude - uncertainty_mm > limit_mm) return Decision::fail;
  return Decision::indeterminate;
}

}  // namespace univision::industrial
