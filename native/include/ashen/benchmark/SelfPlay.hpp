#pragma once

#include "ashen/core/Simulation.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ashen::benchmark {

enum class SpawnSide : std::uint8_t { Left, Right };
enum class FixedScenarioId : std::uint8_t {
  EconomyDeficitRecovery,
  BlockedOpeningRecovery,
  EarlyRushSurvival,
  TacticalFlankChoice,
  TacticalDangerAvoidance,
  TacticalReinforcement,
  TacticalRetreatDestination,
  FactionLossTolerance,
};

struct BenchmarkCase {
  std::string name{};
  core::FactionId left_faction{core::FactionId::Compact};
  core::FactionId right_faction{core::FactionId::Ascendancy};

  auto operator<=>(const BenchmarkCase&) const = default;
};

struct CheckpointReport {
  core::Tick tick{};
  std::uint64_t state_hash{};
  std::uint64_t command_trace_hash{};
  std::uint64_t ai_decision_trace_hash{};

  auto operator<=>(const CheckpointReport&) const = default;
};

struct CommandErrorCount {
  core::CommandError error{core::CommandError::None};
  std::uint64_t count{};

  auto operator<=>(const CommandErrorCount&) const = default;
};

struct FixedScenarioCase {
  FixedScenarioId id{FixedScenarioId::EconomyDeficitRecovery};
  std::string name{};
  core::FactionId faction{core::FactionId::Compact};
  core::Tick maximum_ticks{};

  auto operator<=>(const FixedScenarioCase&) const = default;
};

struct ScenarioCheckReport {
  std::string id{};
  bool passed{};
  std::string message{};

  auto operator<=>(const ScenarioCheckReport&) const = default;
};

struct FixedScenarioReport {
  std::string scenario{};
  std::uint64_t seed{};
  core::FactionId faction{core::FactionId::Compact};
  core::Tick maximum_ticks{};
  core::Tick duration_ticks{};
  bool passed{};
  std::optional<core::Tick> first_contact_tick{};
  std::optional<core::Tick> first_reinforcement_tick{};
  std::optional<core::Tick> first_barracks_completed_tick{};
  std::uint32_t final_workers{};
  std::uint32_t final_army_units{};
  std::uint32_t final_barracks{};
  std::uint32_t final_barracks_including_construction{};
  std::int32_t final_command_hit_points{-1};
  std::uint64_t final_state_hash{};
  std::uint64_t command_trace_hash{};
  std::uint64_t ai_decision_trace_hash{};
  std::uint64_t invalid_ai_decisions{};
  std::uint64_t unresolved_ai_decisions{};
  std::uint64_t unlinked_ai_commands{};
  std::vector<ScenarioCheckReport> checks{};

  auto operator<=>(const FixedScenarioReport&) const = default;
};

struct PlayerMatchReport {
  core::PlayerId player{core::PlayerId::One};
  core::FactionId faction{core::FactionId::Compact};
  core::AIDifficulty difficulty{core::AIDifficulty::Competitive};
  SpawnSide spawn{SpawnSide::Left};
  std::optional<core::Tick> first_barracks_started{};
  std::optional<core::Tick> first_barracks_completed{};
  std::optional<core::Tick> tier_two_completed{};
  std::optional<core::Tick> fifth_worker{};
  std::optional<core::Tick> first_reinforcement{};
  std::optional<core::Tick> first_contact{};
  std::int32_t ore_at_first_contact{-1};
  std::int32_t peak_ore{};
  std::int32_t peak_army_value{};
  std::int32_t final_army_value{};
  core::Tick avoidable_idle_production_ticks{};
  std::uint64_t commands_issued{};
  std::uint64_t commands_accepted{};
  std::uint64_t commands_rejected{};
  std::uint64_t commands_without_observation{};
  std::array<std::uint64_t, 3> ai_decisions_by_layer{};
  std::uint64_t ai_decisions_accepted{};
  std::uint64_t ai_decisions_rejected{};
  std::uint64_t ai_decisions_unresolved{};
  std::uint64_t ai_candidates_available{};
  std::uint64_t ai_candidates_evaluated{};
  std::uint64_t ai_mistakes{};
  std::uint64_t ai_quality_sum_basis_points{};
  std::uint32_t average_ai_quality_basis_points{};
  std::uint64_t knowledge_delay_sum_ticks{};
  std::uint32_t average_knowledge_delay_ticks{};
  std::vector<CommandErrorCount> rejection_reasons{};
  std::uint64_t commands_per_minute_milli{};
  std::uint64_t retreat_units_evaluated{};
  std::uint64_t retreat_survivors{};
  std::optional<std::uint32_t> retreat_survival_basis_points{};

  auto operator<=>(const PlayerMatchReport&) const = default;
};

struct MatchReport {
  std::string scenario{};
  std::uint64_t seed{};
  core::Tick maximum_ticks{};
  core::Tick duration_ticks{};
  bool timed_out{};
  std::optional<core::PlayerId> winner{};
  std::optional<core::Tick> first_contact_tick{};
  std::uint64_t final_state_hash{};
  std::uint64_t command_trace_hash{};
  std::uint64_t ai_decision_trace_hash{};
  std::uint64_t invalid_ai_decisions{};
  std::uint64_t unresolved_ai_decisions{};
  std::uint64_t unlinked_ai_commands{};
  std::vector<CheckpointReport> checkpoints{};
  std::array<PlayerMatchReport, 2> players{};

  auto operator<=>(const MatchReport&) const = default;
};

struct HardFailure {
  std::string scenario{};
  std::uint64_t seed{};
  std::string code{};
  std::string message{};

  auto operator<=>(const HardFailure&) const = default;
};

struct WinRateReport {
  std::string cohort{};
  std::uint64_t games{};
  std::uint64_t wins{};
  std::uint64_t losses{};
  std::uint64_t draws{};
  std::uint32_t win_rate_basis_points{};

  auto operator<=>(const WinRateReport&) const = default;
};

struct BalanceAlert {
  std::string cohort{};
  std::string message{};

  auto operator<=>(const BalanceAlert&) const = default;
};

struct SuiteOptions {
  std::uint32_t seed_count{2};
  std::uint64_t first_seed{1};
  core::Tick maximum_match_ticks{16'000};
  core::Tick checkpoint_interval{500};
  bool verify_determinism{true};

  auto operator<=>(const SuiteOptions&) const = default;
};

struct SuiteReport {
  std::uint32_t schema_version{4};
  SuiteOptions options{};
  std::vector<MatchReport> matches{};
  std::vector<FixedScenarioReport> fixed_scenarios{};
  std::uint64_t scenario_checks_passed{};
  std::uint64_t scenario_checks_total{};
  std::uint32_t scenario_pass_basis_points{};
  std::vector<WinRateReport> win_rates{};
  std::vector<HardFailure> hard_failures{};
  std::vector<BalanceAlert> balance_alerts{};
  std::uint64_t suite_hash{};

  auto operator<=>(const SuiteReport&) const = default;
};

[[nodiscard]] const std::vector<BenchmarkCase>& standard_cases();
[[nodiscard]] const std::vector<FixedScenarioCase>& standard_fixed_scenarios();
[[nodiscard]] MatchReport run_match(const BenchmarkCase& benchmark_case, std::uint64_t seed,
                                    core::Tick maximum_ticks = 16'000,
                                    core::Tick checkpoint_interval = 500);
[[nodiscard]] FixedScenarioReport run_fixed_scenario(const FixedScenarioCase& scenario,
                                                     std::uint64_t seed);
[[nodiscard]] SuiteReport run_suite(const SuiteOptions& options = {});
[[nodiscard]] std::uint64_t command_trace_hash(
    const std::vector<core::CommandTraceEntry>& trace) noexcept;
[[nodiscard]] std::uint64_t ai_decision_trace_hash(
    const std::vector<core::AIDecisionRecord>& trace) noexcept;
[[nodiscard]] std::string to_json(const SuiteReport& report);

}  // namespace ashen::benchmark
