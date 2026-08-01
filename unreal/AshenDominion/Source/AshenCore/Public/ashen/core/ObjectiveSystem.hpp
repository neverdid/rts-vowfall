#pragma once

#include "ashen/core/Scenario.hpp"
#include "ashen/core/SimulationEvent.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ashen::core {

struct MissionObjectiveState {
  StableContentId content_id{};
  MissionObjectiveStatus status{MissionObjectiveStatus::Inactive};
  Tick activated_tick{};
  Tick resolved_tick{};

  auto operator<=>(const MissionObjectiveState&) const = default;
};

struct MissionObjectiveView {
  StableContentId content_id{};
  std::string_view label{};
  MissionObjectiveStatus status{MissionObjectiveStatus::Inactive};
  bool primary{};
  bool required{};
  std::uint32_t stage_index{};
  std::uint32_t stage_count{};
  Tick current_tick{};
  Tick target_tick{};
};

struct MissionObjectiveContext {
  Tick tick{};
  std::array<bool, 2> command_seen{};
  std::array<bool, 2> command_alive{};
  std::size_t objective_count{};
  std::size_t player_one_controlled_objectives{};
};

struct MissionObjectiveTransition {
  StableContentId content_id{};
  MissionObjectiveStatus previous{MissionObjectiveStatus::Inactive};
  MissionObjectiveStatus current{MissionObjectiveStatus::Inactive};
  bool primary{};
  bool required{};
};

class ASHENCORE_API ObjectiveSystem final {
 public:
  void reset(const SimulationConfig& config);
  [[nodiscard]] std::vector<MissionObjectiveTransition> evaluate(
      const MissionObjectiveContext& context);
  [[nodiscard]] bool rebuild(std::span<const SimulationEvent> events);
  [[nodiscard]] bool event_projection_matches(
      const SimulationConfig& config,
      std::span<const SimulationEvent> events) const;

  [[nodiscard]] bool has_scenario() const noexcept {
    return scenario_ != nullptr;
  }
  [[nodiscard]] std::span<const MissionObjectiveState> states() const noexcept {
    return states_;
  }
  [[nodiscard]] std::vector<MissionObjectiveView> views(Tick tick) const;
  [[nodiscard]] std::optional<MissionObjectiveView> primary_view(
      Tick tick) const;
  [[nodiscard]] bool all_required_succeeded() const noexcept;
  [[nodiscard]] bool outcome_matches(
      MatchMode mode, MatchStatus status,
      std::optional<PlayerId> winner) const noexcept;
  [[nodiscard]] std::uint64_t state_hash() const noexcept;

 private:
  void initialize_states();
  [[nodiscard]] MissionObjectiveState* find_state_mutable(
      StableContentId content_id) noexcept;
  [[nodiscard]] const MissionObjectiveState* find_state(
      StableContentId content_id) const noexcept;

  const ScenarioDefinition* scenario_{};
  std::vector<MissionObjectiveState> states_{};
};

}  // namespace ashen::core
