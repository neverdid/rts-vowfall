#pragma once

#include "ashen/core/AIDecision.hpp"
#include "ashen/core/PlayerObservation.hpp"

#include <cstdint>
#include <deque>
#include <vector>

namespace ashen::core {

// CommanderAI deliberately has no Simulation dependency. Its only input is a sanitized observation and
// its only output is the same Command value a human player submits.
class ASHENCORE_API CommanderAI final {
 public:
  explicit CommanderAI(
      PlayerId player,
      AIDifficulty difficulty = AIDifficulty::Competitive) noexcept;

  [[nodiscard]] PlayerId player() const noexcept { return player_; }
  [[nodiscard]] AIDifficulty difficulty() const noexcept { return difficulty_; }
  [[nodiscard]] const AIDifficultyProfile& difficulty_profile() const noexcept;
  void reset(AIDifficulty difficulty) noexcept;
  [[nodiscard]] PlayerObservation perceive(
      const PlayerObservation& observation);
  [[nodiscard]] CommanderPlan update(const PlayerObservation& observation);
  [[nodiscard]] CommanderPlan plan(const PlayerObservation& observation) const;
  [[nodiscard]] std::vector<Command> decide(const PlayerObservation& observation) const;
  [[nodiscard]] std::uint64_t state_hash() const noexcept;

 private:
  PlayerId player_{PlayerId::One};
  AIDifficulty difficulty_{AIDifficulty::Competitive};
  std::deque<PlayerObservation> observation_history_{};
};

}  // namespace ashen::core
