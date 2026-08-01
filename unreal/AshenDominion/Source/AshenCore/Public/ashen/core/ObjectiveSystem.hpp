#pragma once

#include "ashen/core/Scenario.hpp"
#include "ashen/core/SimulationEvent.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ashen::core {

struct MissionObjectiveState {
  StableContentId content_id{};
  MissionObjectiveStatus status{MissionObjectiveStatus::Inactive};

  auto operator<=>(const MissionObjectiveState&) const = default;
};

struct MissionObjectiveView {
  StableContentId content_id{};
  std::string_view label{};
  MissionObjectiveStatus status{MissionObjectiveStatus::Inactive};
  bool primary{};
  Tick current_tick{};
  Tick target_tick{};
};

struct MissionObjectiveContext {
  Tick tick{};
  std::array<bool, 2> command_seen{};
  std::array<bool, 2> command_alive{};
};

struct MissionObjectiveTransition {
  StableContentId content_id{};
  MissionObjectiveStatus previous{MissionObjectiveStatus::Inactive};
  MissionObjectiveStatus current{MissionObjectiveStatus::Inactive};
  bool primary{};
};

class ASHENCORE_API ObjectiveSystem final {
 public:
  void reset(const SimulationConfig& config);
  [[nodiscard]] std::vector<MissionObjectiveTransition> evaluate(
      const MissionObjectiveContext& context);
  void rebuild(MatchStatus match_status, std::optional<PlayerId> winner);

  [[nodiscard]] bool has_scenario() const noexcept {
    return scenario_ != nullptr;
  }
  [[nodiscard]] std::span<const MissionObjectiveState> states() const noexcept {
    return states_;
  }
  [[nodiscard]] std::vector<MissionObjectiveView> views(Tick tick) const;
  [[nodiscard]] std::optional<MissionObjectiveView> primary_view(
      Tick tick) const;
  [[nodiscard]] std::uint64_t state_hash() const noexcept;

 private:
  const ScenarioDefinition* scenario_{};
  std::vector<MissionObjectiveState> states_{};
};

}  // namespace ashen::core
