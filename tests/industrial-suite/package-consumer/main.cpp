#include <univision/industrial/flow.h>
#include <univision/industrial/replay.h>
#include <univision/industrial/stream.h>
#include <univision/univision.h>

int main() {
  univision::System system;
  univision::industrial::Recorder recorder;
  univision::industrial::ReplayEngine replay;
  univision::industrial::LocalStream stream(
      univision::industrial::DeliveryMode::inspection_bounded, 1, 64);
  univision::industrial::FlowDefinition graph;
  const auto checked = univision::industrial::preflight_flow(graph);
  univision::industrial::Profile profile;
  const auto result = univision::industrial::fit_circle_diameter(profile, nullptr);
  return !checked && result.validity ==
                         univision::industrial::MeasurementValidity::invalid &&
                 stream.queue_depth() == 0 && system.adapters().empty() &&
                 !replay.complete()
             ? 0 : 1;
}
