#include "ashen/core/AIDifficulty.hpp"
#include "ashen/core/CommanderAI.hpp"
#include "ashen/core/Simulation.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <ranges>
#include <string_view>

namespace {

using namespace ashen::core;

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "  check failed at line " << __LINE__ << ": " #condition    \
                << "\n";                                                        \
      ++failures;                                                               \
    }                                                                           \
  } while (false)

template <typename Test>
void run_test(const std::string_view name, Test&& test) {
  const auto before = failures;
  test();
  std::cout << (failures == before ? "[pass] " : "[fail] ") << name << '\n';
}

[[nodiscard]] SimulationConfig open_config(const std::uint64_t seed = 1) {
  SimulationConfig config{};
  config.seed_starting_forces = false;
  config.starting_ore = {2'000, 2'000};
  config.match_seed = seed;
  config.map_size = world(2'000, 800);
  config.navigation_obstacles.clear();
  return config;
}

void hold(Simulation& simulation, const PlayerId player,
          const EntityId entity) {
  CHECK(simulation
            .execute_now(Command{.player = player,
                                 .type = CommandType::Hold,
                                 .entities = {entity}})
            .ok);
}

[[nodiscard]] bool has_visible_enemy(
    const PlayerObservation& observation) noexcept {
  return std::ranges::any_of(
      observation.known_enemies(),
      [](const ObservedEnemy& enemy) { return enemy.currently_visible; });
}

[[nodiscard]] bool same_player_state(const PlayerState& left,
                                     const PlayerState& right) noexcept {
  const auto same_research_queue =
      left.research_queue.size() == right.research_queue.size() &&
      std::ranges::equal(
          left.research_queue, right.research_queue,
          [](const ResearchTask& left_task,
             const ResearchTask& right_task) {
            return left_task.id == right_task.id &&
                   left_task.remaining_ticks == right_task.remaining_ticks &&
                   left_task.total_ticks == right_task.total_ticks;
          });
  return left.id == right.id && left.faction == right.faction &&
         left.ore == right.ore && left.supply_used == right.supply_used &&
         left.supply_cap == right.supply_cap &&
         left.resolve == right.resolve &&
         left.power_cooldown_ticks == right.power_cooldown_ticks &&
         left.tech_tier == right.tech_tier &&
         left.researched == right.researched &&
         same_research_queue;
}

[[nodiscard]] bool same_owned_entities(
    const std::vector<Entity>& left,
    const std::vector<Entity>& right) noexcept {
  return left.size() == right.size() &&
         std::ranges::equal(
             left, right, [](const Entity& left_entity,
                             const Entity& right_entity) {
               return left_entity.id == right_entity.id &&
                      left_entity.owner == right_entity.owner &&
                      left_entity.type == right_entity.type &&
                      left_entity.kind == right_entity.kind &&
                      left_entity.position == right_entity.position &&
                      left_entity.hit_points == right_entity.hit_points &&
                      left_entity.max_hit_points ==
                          right_entity.max_hit_points &&
                      left_entity.cooldown_ticks ==
                          right_entity.cooldown_ticks &&
                      left_entity.resolve == right_entity.resolve &&
                      left_entity.carrying == right_entity.carrying &&
                      left_entity.order.type == right_entity.order.type &&
                      left_entity.order.target == right_entity.order.target &&
                      left_entity.order.target_entity ==
                          right_entity.order.target_entity &&
                      left_entity.order.resource ==
                          right_entity.order.resource &&
                      left_entity.production_queue.size() ==
                          right_entity.production_queue.size() &&
                      left_entity.under_construction ==
                          right_entity.under_construction &&
                      left_entity.construction_ticks ==
                          right_entity.construction_ticks;
             });
}

[[nodiscard]] const AIPlannedDecision* broadest_decision(
    const CommanderPlan& plan) noexcept {
  if (plan.decisions.empty()) {
    return nullptr;
  }
  return &*std::ranges::max_element(
      plan.decisions, {}, [](const AIPlannedDecision& decision) {
        return decision.candidates.size();
      });
}

[[nodiscard]] Simulation candidate_rich_match(const std::uint64_t seed) {
  auto config = open_config(seed);
  Simulation simulation{config};
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                            world(180, 400)));
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Barracks,
                                            world(340, 400)));
  for (const auto y : {300, 350, 400, 450, 500}) {
    static_cast<void>(simulation.spawn_entity(
        PlayerId::One, EntityType::Worker, world(250, y)));
  }
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Vanguard,
                                            world(430, 400)));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                            world(1'820, 400)));
  static_cast<void>(simulation.add_resource(world(430, 620), 2'000));
  simulation.step();
  return simulation;
}

void profiles_are_monotonic_and_stably_fingerprinted() {
  constexpr std::array difficulties{
      AIDifficulty::Story,
      AIDifficulty::Standard,
      AIDifficulty::Veteran,
      AIDifficulty::Competitive,
  };
  std::array<std::uint64_t, difficulties.size()> hashes{};
  for (std::size_t index = 0; index < difficulties.size(); ++index) {
    const auto& profile = ai_difficulty_profile(difficulties[index]);
    hashes[index] = ai_difficulty_hash(profile);
    CHECK(profile.difficulty == difficulties[index]);
    CHECK(hashes[index] != 0);
    CHECK(profile.reaction_delay_ticks > 0);
    CHECK(profile.strategic_cadence_ticks > 0);
    CHECK(profile.tactical_cadence_ticks > 0);
    CHECK(profile.micro_cadence_ticks > 0);
    CHECK(profile.planning_horizon_cells > 0);
    CHECK(profile.mistake_rate_basis_points >= 0);
    CHECK(profile.mistake_rate_basis_points <= 10'000);
    CHECK(profile.minimum_mistake_quality_basis_points > 0);
    CHECK(profile.minimum_mistake_quality_basis_points <= 10'000);
  }
  CHECK(std::ranges::adjacent_find(hashes) == hashes.end());

  const auto& story = ai_difficulty_profile(AIDifficulty::Story);
  const auto& standard = ai_difficulty_profile(AIDifficulty::Standard);
  const auto& veteran = ai_difficulty_profile(AIDifficulty::Veteran);
  const auto& competitive = ai_difficulty_profile(AIDifficulty::Competitive);
  CHECK(story.reaction_delay_ticks > standard.reaction_delay_ticks);
  CHECK(standard.reaction_delay_ticks > veteran.reaction_delay_ticks);
  CHECK(veteran.reaction_delay_ticks > competitive.reaction_delay_ticks);
  CHECK(story.command_latency_ticks > standard.command_latency_ticks);
  CHECK(standard.command_latency_ticks > veteran.command_latency_ticks);
  CHECK(veteran.command_latency_ticks > competitive.command_latency_ticks);
  CHECK(story.command_precision_radius > standard.command_precision_radius);
  CHECK(standard.command_precision_radius > veteran.command_precision_radius);
  CHECK(veteran.command_precision_radius > competitive.command_precision_radius);
  CHECK(story.planning_horizon_cells < standard.planning_horizon_cells);
  CHECK(standard.planning_horizon_cells < veteran.planning_horizon_cells);
  CHECK(veteran.planning_horizon_cells < competitive.planning_horizon_cells);
  CHECK(story.mistake_rate_basis_points >
        standard.mistake_rate_basis_points);
  CHECK(standard.mistake_rate_basis_points >
        veteran.mistake_rate_basis_points);
  CHECK(veteran.mistake_rate_basis_points >
        competitive.mistake_rate_basis_points);
  CHECK(story.mobile_memory_ticks < standard.mobile_memory_ticks);
  CHECK(standard.mobile_memory_ticks < veteran.mobile_memory_ticks);
  CHECK(veteran.mobile_memory_ticks < competitive.mobile_memory_ticks);
  CHECK(competitive.utility_search_breadth == 0);
}

void difficulty_never_changes_raw_resources_units_or_vision() {
  auto story_config = SimulationConfig{};
  auto competitive_config = story_config;
  story_config.commander_difficulties = {AIDifficulty::Story,
                                         AIDifficulty::Story};
  competitive_config.commander_difficulties = {
      AIDifficulty::Competitive, AIDifficulty::Competitive};
  Simulation story{story_config};
  Simulation competitive{competitive_config};

  for (Tick tick = 0; tick < 240; ++tick) {
    for (const auto player : {PlayerId::One, PlayerId::Two}) {
      const auto story_view = story.observe(player);
      const auto competitive_view = competitive.observe(player);
      CHECK(story_view.hash() == competitive_view.hash());
      CHECK(same_player_state(story_view.self(), competitive_view.self()));
      CHECK(same_owned_entities(story_view.owned_entities(),
                                competitive_view.owned_entities()));
      CHECK(story_view.known_resources() ==
            competitive_view.known_resources());
      CHECK(story_view.explored_map().cells() ==
            competitive_view.explored_map().cells());
    }
    story.step();
    competitive.step();
  }
}

void hostile_facts_wait_for_each_profiles_reaction_window() {
  Simulation simulation{open_config()};
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                            world(100, 400)));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                            world(1'900, 400)));
  const auto observer = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(800, 400));
  const auto enemy = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(980, 400));
  hold(simulation, PlayerId::One, observer);
  hold(simulation, PlayerId::Two, enemy);

  CommanderAI story{PlayerId::One, AIDifficulty::Story};
  CommanderAI competitive{PlayerId::One, AIDifficulty::Competitive};
  CommanderAI replay{PlayerId::One, AIDifficulty::Competitive};
  const auto competitive_delay =
      ai_difficulty_profile(AIDifficulty::Competitive).reaction_delay_ticks;
  const auto story_delay =
      ai_difficulty_profile(AIDifficulty::Story).reaction_delay_ticks;

  for (Tick tick = 0; tick <= story_delay; ++tick) {
    const auto raw = simulation.observe(PlayerId::One);
    CHECK(raw.tick() == tick);
    CHECK(has_visible_enemy(raw));
    CHECK(raw.permits(CommandType::Attack, observer));

    const auto story_view = story.perceive(raw);
    const auto competitive_view = competitive.perceive(raw);
    const auto replay_view = replay.perceive(raw);
    CHECK(competitive_view.hash() == replay_view.hash());
    CHECK(competitive.state_hash() == replay.state_hash());
    CHECK(competitive.plan(competitive_view) == replay.plan(replay_view));

    CHECK(same_player_state(story_view.self(), raw.self()));
    CHECK(same_owned_entities(story_view.owned_entities(),
                              raw.owned_entities()));
    CHECK(story_view.known_resources() == raw.known_resources());
    CHECK(story_view.explored_map().cells() == raw.explored_map().cells());
    CHECK(has_visible_enemy(competitive_view) ==
          (tick >= competitive_delay));
    CHECK(has_visible_enemy(story_view) == (tick >= story_delay));
    CHECK(competitive_view.permits(CommandType::Attack, observer) ==
          (tick >= competitive_delay));
    CHECK(story_view.permits(CommandType::Attack, observer) ==
          (tick >= story_delay));

    if (tick != story_delay) {
      simulation.step();
    }
  }
}

void mobile_contact_memory_decays_by_profile() {
  Simulation simulation{open_config()};
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                            world(100, 400)));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                            world(1'900, 400)));
  const auto scout = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(800, 400));
  const auto enemy = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(980, 400));
  hold(simulation, PlayerId::One, scout);
  hold(simulation, PlayerId::Two, enemy);

  CommanderAI story{PlayerId::One, AIDifficulty::Story};
  CommanderAI competitive{PlayerId::One, AIDifficulty::Competitive};
  static_cast<void>(story.perceive(simulation.observe(PlayerId::One)));
  static_cast<void>(competitive.perceive(simulation.observe(PlayerId::One)));

  CHECK(simulation
            .execute_now(Command{.player = PlayerId::One,
                                 .type = CommandType::Move,
                                 .entities = {scout},
                                 .target = world(400, 400)})
            .ok);

  Tick last_visible_tick = 0;
  bool lost_contact = false;
  const auto story_profile = ai_difficulty_profile(AIDifficulty::Story);
  const auto competitive_profile =
      ai_difficulty_profile(AIDifficulty::Competitive);
  const auto end_tick = story_profile.mobile_memory_ticks +
                        story_profile.reaction_delay_ticks + 120;
  std::optional<PlayerObservation> story_view;
  std::optional<PlayerObservation> competitive_view;
  for (Tick count = 0; count < end_tick; ++count) {
    simulation.step();
    const auto raw = simulation.observe(PlayerId::One);
    if (has_visible_enemy(raw)) {
      last_visible_tick = raw.tick();
    } else if (last_visible_tick != 0) {
      lost_contact = true;
    }
    story_view = story.perceive(raw);
    competitive_view = competitive.perceive(raw);
  }

  CHECK(lost_contact);
  CHECK(simulation.tick() >= last_visible_tick +
                                 story_profile.mobile_memory_ticks +
                                 story_profile.reaction_delay_ticks);
  CHECK(story_view.has_value());
  CHECK(competitive_view.has_value());
  CHECK(story_view.has_value() && story_view->known_enemies().empty());
  CHECK(competitive_view.has_value() &&
        !competitive_view->known_enemies().empty());
  CHECK(competitive_view.has_value() &&
        !competitive_view->known_enemies().empty() &&
        competitive_view->known_enemies().front().last_observed_tick ==
            last_visible_tick);
  CHECK(simulation.tick() <
        last_visible_tick + competitive_profile.mobile_memory_ticks);
}

void easier_profiles_are_bounded_but_measurably_less_exact() {
  std::uint64_t story_quality = 0;
  std::uint64_t competitive_quality = 0;
  std::uint64_t story_samples = 0;
  std::uint64_t competitive_samples = 0;
  std::uint64_t story_mistakes = 0;

  for (std::uint64_t seed = 1; seed <= 128; ++seed) {
    auto simulation = candidate_rich_match(seed);
    const auto observation = simulation.observe(PlayerId::One);
    const auto story_plan =
        CommanderAI{PlayerId::One, AIDifficulty::Story}.plan(observation);
    const auto competitive_plan =
        CommanderAI{PlayerId::One, AIDifficulty::Competitive}.plan(observation);
    const auto* story = broadest_decision(story_plan);
    const auto* competitive = broadest_decision(competitive_plan);
    CHECK(story != nullptr);
    CHECK(competitive != nullptr);
    if (story == nullptr || competitive == nullptr) {
      continue;
    }
    CHECK(story->candidates.size() >= 3);
    CHECK(story->selected_quality_basis_points > 0);
    CHECK(story->selected_quality_basis_points <= 10'000);
    CHECK(story->evaluated_candidates > 0);
    CHECK(story->evaluated_candidates <=
          ai_difficulty_profile(AIDifficulty::Story).utility_search_breadth);
    CHECK(story->command_latency_ticks ==
          ai_difficulty_profile(AIDifficulty::Story).command_latency_ticks);
    CHECK(competitive->evaluated_candidates ==
          competitive->candidates.size());
    CHECK(competitive->selected_quality_basis_points == 10'000);
    CHECK(!competitive->mistake_applied);
    CHECK(competitive->command_latency_ticks == 0);
    story_quality +=
        static_cast<std::uint64_t>(story->selected_quality_basis_points);
    competitive_quality +=
        static_cast<std::uint64_t>(competitive->selected_quality_basis_points);
    ++story_samples;
    ++competitive_samples;
    story_mistakes += story->mistake_applied ? 1U : 0U;
  }

  CHECK(story_samples == 128);
  CHECK(competitive_samples == 128);
  CHECK(story_quality / story_samples <
        competitive_quality / competitive_samples);
  CHECK(story_mistakes > 0);
}

void command_precision_is_deterministic_radial_and_never_affects_builds() {
  auto story_match = candidate_rich_match(91);
  auto story_observation = story_match.observe(PlayerId::One);
  const auto first_story =
      CommanderAI{PlayerId::One, AIDifficulty::Story}.plan(story_observation);
  const auto replay_story =
      CommanderAI{PlayerId::One, AIDifficulty::Story}.plan(story_observation);
  CHECK(first_story == replay_story);
  CHECK(std::ranges::all_of(
      first_story.decisions, [](const AIPlannedDecision& decision) {
        return decision.command.type != CommandType::Build ||
               decision.command_precision_offset == Vec2{};
      }));

  auto tactical_config = open_config(91);
  tactical_config.starting_ore = {0, 0};
  Simulation tactical{tactical_config};
  static_cast<void>(tactical.spawn_entity(PlayerId::One, EntityType::Command,
                                          world(180, 400)));
  static_cast<void>(tactical.spawn_entity(PlayerId::Two, EntityType::Command,
                                          world(1'820, 400)));
  for (const auto y : {310, 370, 430, 490}) {
    const auto unit = tactical.spawn_entity(
        PlayerId::One, EntityType::Vanguard, world(430, y));
    hold(tactical, PlayerId::One, unit);
  }
  static_cast<void>(tactical.add_control_point(world(900, 610)));
  tactical.run(
      ai_difficulty_profile(AIDifficulty::Story).tactical_phase_ticks);

  const auto story_plan =
      CommanderAI{PlayerId::One, AIDifficulty::Story}.plan(
          tactical.observe(PlayerId::One));
  const auto point_decision = std::ranges::find_if(
      story_plan.decisions, [](const AIPlannedDecision& decision) {
        return decision.command.type == CommandType::Move ||
               decision.command.type == CommandType::AttackMove ||
               decision.command.type == CommandType::Retreat;
      });
  CHECK(point_decision != story_plan.decisions.end());
  if (point_decision != story_plan.decisions.end()) {
    const auto radius =
        ai_difficulty_profile(AIDifficulty::Story).command_precision_radius;
    const auto x = static_cast<std::int64_t>(
        point_decision->command_precision_offset.x);
    const auto y = static_cast<std::int64_t>(
        point_decision->command_precision_offset.y);
    CHECK(x * x + y * y <=
          static_cast<std::int64_t>(radius) * radius);
  }
}

}  // namespace

int main() {
  run_test("profiles are monotonic and stably fingerprinted",
           profiles_are_monotonic_and_stably_fingerprinted);
  run_test("difficulty never changes raw resources, units, or vision",
           difficulty_never_changes_raw_resources_units_or_vision);
  run_test("hostile facts wait for each profile reaction window",
           hostile_facts_wait_for_each_profiles_reaction_window);
  run_test("mobile contact memory decays by profile",
           mobile_contact_memory_decays_by_profile);
  run_test("easier profiles are bounded but measurably less exact",
           easier_profiles_are_bounded_but_measurably_less_exact);
  run_test("command precision is deterministic, radial, and build safe",
           command_precision_is_deterministic_radial_and_never_affects_builds);

  if (failures != 0) {
    std::cerr << failures << " honest difficulty check(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All honest difficulty checks passed.\n";
  return EXIT_SUCCESS;
}
