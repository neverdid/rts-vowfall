#include "ashen/benchmark/SelfPlay.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ashen;

int failures = 0;

#define CHECK(condition)                                                                            \
  do {                                                                                              \
    if (!(condition)) {                                                                             \
      std::cerr << "  check failed at line " << __LINE__ << ": " #condition "\n";               \
      ++failures;                                                                                   \
    }                                                                                               \
  } while (false)

template <typename Test>
void run_test(const std::string_view name, Test&& test) {
  const auto before = failures;
  test();
  std::cout << (failures == before ? "[pass] " : "[fail] ") << name << '\n';
}

void fixed_cases_cover_every_faction_and_spawn() {
  const auto& cases = benchmark::standard_cases();
  CHECK(cases.size() == 6);
  std::array<std::uint32_t, 3> left_counts{};
  std::array<std::uint32_t, 3> right_counts{};
  std::set<std::string> names;
  for (const auto& benchmark_case : cases) {
    ++left_counts[static_cast<std::size_t>(benchmark_case.left_faction)];
    ++right_counts[static_cast<std::size_t>(benchmark_case.right_faction)];
    names.insert(benchmark_case.name);
  }
  constexpr std::array<std::uint32_t, 3> expected_counts{2, 2, 2};
  CHECK(names.size() == cases.size());
  CHECK(left_counts == expected_counts);
  CHECK(right_counts == expected_counts);
}

void fixed_behavior_scenarios_cover_every_faction_and_pass() {
  const auto& scenarios = benchmark::standard_fixed_scenarios();
  CHECK(scenarios.size() == 24);
  std::array<std::uint32_t, 3> faction_counts{};
  std::set<std::string> names;
  const std::vector<core::CommandTraceEntry> empty_trace;
  const std::vector<core::AIDecisionRecord> empty_decision_trace;
  for (const auto& scenario : scenarios) {
    ++faction_counts[static_cast<std::size_t>(scenario.faction)];
    names.insert(scenario.name);
    const auto report = benchmark::run_fixed_scenario(scenario, 1);
    CHECK(report.passed);
    CHECK(!report.checks.empty());
    CHECK(report.command_trace_hash != benchmark::command_trace_hash(empty_trace));
    CHECK(report.ai_decision_trace_hash !=
          benchmark::ai_decision_trace_hash(empty_decision_trace));
    CHECK(report.invalid_ai_decisions == 0);
    CHECK(report.unlinked_ai_commands == 0);
  }
  constexpr std::array<std::uint32_t, 3> expected_counts{8, 8, 8};
  CHECK(names.size() == scenarios.size());
  CHECK(faction_counts == expected_counts);
}

void duplicate_matches_have_identical_traces_and_checkpoints() {
  const auto& benchmark_case = benchmark::standard_cases().front();
  const auto first = benchmark::run_match(benchmark_case, 1);
  const auto replay = benchmark::run_match(benchmark_case, 1);
  const std::vector<core::CommandTraceEntry> empty_trace;
  const std::vector<core::AIDecisionRecord> empty_decision_trace;

  CHECK(first == replay);
  CHECK(!first.timed_out);
  CHECK(first.winner.has_value());
  CHECK(first.first_contact_tick.has_value());
  CHECK(!first.checkpoints.empty());
  CHECK(first.checkpoints.back().tick == first.duration_ticks);
  CHECK(first.final_state_hash != 0);
  CHECK(first.command_trace_hash != benchmark::command_trace_hash(empty_trace));
  CHECK(first.ai_decision_trace_hash !=
        benchmark::ai_decision_trace_hash(empty_decision_trace));
  CHECK(first.invalid_ai_decisions == 0);
  CHECK(first.unresolved_ai_decisions == 0);
  CHECK(first.unlinked_ai_commands == 0);
  for (std::size_t layer = 0; layer < 3; ++layer) {
    CHECK(first.players[0].ai_decisions_by_layer[layer] +
              first.players[1].ai_decisions_by_layer[layer] >
          0);
  }
  for (const auto& player : first.players) {
    CHECK(player.commands_issued > 0);
    CHECK(player.commands_without_observation == 0);
    CHECK(player.first_barracks_completed.has_value());
    CHECK(player.first_reinforcement.has_value());
  }
}

void match_seed_is_part_of_simulation_and_trace_identity() {
  const auto& benchmark_case = benchmark::standard_cases().front();
  const auto first_seed = benchmark::run_match(benchmark_case, 1);
  const auto second_seed = benchmark::run_match(benchmark_case, 2);
  CHECK(first_seed.seed != second_seed.seed);
  CHECK(first_seed.final_state_hash != second_seed.final_state_hash);
  CHECK(first_seed.command_trace_hash != second_seed.command_trace_hash);
  CHECK(first_seed.ai_decision_trace_hash != second_seed.ai_decision_trace_hash);
}

void report_json_is_stable_and_invalid_options_fail_closed() {
  benchmark::SuiteReport sample{};
  sample.options.seed_count = 1;
  sample.options.verify_determinism = false;
  sample.suite_hash = 0x1234U;
  sample.matches.push_back(benchmark::run_match(benchmark::standard_cases().front(), 7));
  const auto first_json = benchmark::to_json(sample);
  const auto second_json = benchmark::to_json(sample);
  CHECK(first_json == second_json);
  CHECK(first_json.find("\"schema_version\": 4") != std::string::npos);
  CHECK(first_json.find("\"command_trace_hash\"") != std::string::npos);
  CHECK(first_json.find("\"ai_decision_trace_hash\"") != std::string::npos);
  CHECK(first_json.find("\"ai_decisions_by_layer\"") != std::string::npos);
  CHECK(first_json.find("\"difficulty\": \"competitive\"") != std::string::npos);
  CHECK(first_json.find("\"ai_candidates_evaluated\"") != std::string::npos);
  CHECK(first_json.find("\"average_ai_quality_basis_points\"") !=
        std::string::npos);
  CHECK(first_json.find("\"average_knowledge_delay_ticks\"") !=
        std::string::npos);
  CHECK(first_json.find("\"avoidable_idle_production_ticks\"") != std::string::npos);
  CHECK(first_json.find("\"fixed_scenarios\"") != std::string::npos);
  CHECK(first_json.find("\"scenario_summary\"") != std::string::npos);

  benchmark::SuiteOptions invalid{};
  invalid.seed_count = 0;
  const auto rejected = benchmark::run_suite(invalid);
  CHECK(rejected.matches.empty());
  CHECK(!rejected.hard_failures.empty());
  CHECK(rejected.hard_failures.front().code == "invalid_seed_count");
}

}  // namespace

int main() {
  run_test("fixed cases cover every faction and spawn", fixed_cases_cover_every_faction_and_spawn);
  run_test("fixed behavior scenarios cover every faction and pass",
           fixed_behavior_scenarios_cover_every_faction_and_pass);
  run_test("duplicate matches have identical traces and checkpoints",
           duplicate_matches_have_identical_traces_and_checkpoints);
  run_test("match seed is part of simulation and trace identity",
           match_seed_is_part_of_simulation_and_trace_identity);
  run_test("report JSON is stable and invalid options fail closed",
           report_json_is_stable_and_invalid_options_fail_closed);

  if (failures != 0) {
    std::cerr << failures << " self-play check(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All self-play checks passed.\n";
  return EXIT_SUCCESS;
}
