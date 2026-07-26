#pragma once

#include "ashen/core/AIDecision.hpp"
#include "ashen/core/PlayerObservation.hpp"

#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

namespace ashen::core::ai {

struct ScoredCommand {
  AICandidateScore candidate{};
  Command command{};
};

class CandidateBuilder final {
 public:
  CandidateBuilder(AIAction action, Command command);

  CandidateBuilder& add(AIUtilityReason reason, std::int32_t score);
  CandidateBuilder& target(EntityId entity);
  CandidateBuilder& objective(ControlPointId objective);
  CandidateBuilder& position(Vec2 position);
  CandidateBuilder& entity_type(EntityType type);
  CandidateBuilder& research(ResearchId research);
  CandidateBuilder& influence(const AIInfluenceMap& map, Vec2 position);
  [[nodiscard]] ScoredCommand finish() &&;

 private:
  ScoredCommand value_{};
};

struct PlanningContext {
  explicit PlanningContext(const PlayerObservation& observation_value,
                           bool build_tactical_map,
                           AIDifficultyProfile difficulty_value);

  [[nodiscard]] const Entity* owned(EntityId id) const noexcept;
  [[nodiscard]] const AIInfluenceMap& tactical_map() const noexcept {
    assert(tactical_map_.has_value());
    return *tactical_map_;
  }

  const PlayerObservation& observation;
  const Entity* command_building{};
  std::vector<const Entity*> workers{};
  std::vector<const Entity*> army{};
  std::vector<const Entity*> ready_army{};
  std::vector<const Entity*> vanguards{};
  std::vector<const Entity*> skirmishers{};
  std::vector<const ObservedEnemy*> visible_enemies{};
  AIDifficultyProfile difficulty{};
  AIDoctrineProfile doctrine{};
  std::uint64_t strategy{};
  std::int32_t friendly_power{};
  std::int32_t ready_power{};
  std::int32_t visible_enemy_power{};
  std::int32_t average_army_resolve{100};
  std::int32_t visible_enemy_average_resolve{100};
  std::int32_t friendly_terror{};
  std::int32_t friendly_ward{};
  std::int32_t visible_enemy_terror{};
  std::int32_t visible_enemy_ward{};
  bool attrition_commitment{};
  bool search_commitment{};

 private:
  std::optional<AIInfluenceMap> tactical_map_{};
};

[[nodiscard]] bool is_army_unit(EntityType type) noexcept;
[[nodiscard]] std::uint64_t squared_distance(Vec2 left, Vec2 right) noexcept;
[[nodiscard]] std::uint64_t integer_sqrt(std::uint64_t value) noexcept;
[[nodiscard]] Vec2 centroid(const std::vector<const Entity*>& entities) noexcept;
[[nodiscard]] Vec2 enemy_centroid(const std::vector<const ObservedEnemy*>& enemies) noexcept;
[[nodiscard]] Vec2 clamped_position(const PlayerObservation& observation, Vec2 position,
                                    std::int32_t margin = 12'000) noexcept;
[[nodiscard]] Vec2 position_away_from(const PlayerObservation& observation, Vec2 origin,
                                      Vec2 danger, std::int32_t distance) noexcept;
[[nodiscard]] std::int32_t entity_power(const Entity& entity, FactionId faction) noexcept;
[[nodiscard]] std::int32_t enemy_power(const ObservedEnemy& enemy, FactionId faction) noexcept;
[[nodiscard]] Command command_for(PlayerId player, CommandType type) noexcept;
[[nodiscard]] const CommandCapability* first_capability(
    const PlayerObservation& observation, CommandType type,
    std::optional<EntityType> entity_type = std::nullopt,
    std::optional<ResearchId> research = std::nullopt) noexcept;
[[nodiscard]] const ObservedResource* nearest_usable_resource(
    const PlayerObservation& observation, Vec2 origin) noexcept;
[[nodiscard]] std::optional<ResearchId> faction_doctrine(FactionId faction) noexcept;
[[nodiscard]] std::optional<AIPlannedDecision> select_decision(
    AIDecisionLayer layer, Tick cadence, const PlanningContext& context,
    std::vector<ScoredCommand> candidates);

[[nodiscard]] std::vector<AIPlannedDecision> evaluate_strategic_layer(
    const PlanningContext& context);
[[nodiscard]] std::optional<AIPlannedDecision> evaluate_tactical_layer(
    const PlanningContext& context);
[[nodiscard]] std::optional<AIPlannedDecision> evaluate_micro_layer(
    const PlanningContext& context);

}  // namespace ashen::core::ai
