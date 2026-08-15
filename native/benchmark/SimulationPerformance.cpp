#include "ashen/core/CommanderAI.hpp"
#include "ashen/core/Simulation.hpp"
#include "ashen/core/SpatialGrid.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using namespace ashen::core;
using Clock = std::chrono::steady_clock;

template <typename Operation>
[[nodiscard]] std::uint64_t average_microseconds(const std::size_t iterations,
                                                 Operation&& operation) {
  const auto start = Clock::now();
  for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
    operation();
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      Clock::now() - start);
  return static_cast<std::uint64_t>(elapsed.count()) /
         std::max<std::size_t>(1, iterations);
}

[[nodiscard]] Simulation make_simulation(const std::size_t population) {
  SimulationConfig config{};
  config.seed_starting_forces = false;
  config.map_size = world(6'000, 6'000);
  config.navigation_obstacles.clear();
  config.starting_ore = {20'000, 20'000};
  Simulation simulation{config};

  static_cast<void>(
      simulation.spawn_entity(PlayerId::One, EntityType::Command, world(300, 300)));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                            world(5'700, 5'700)));
  constexpr std::int32_t columns = 40;
  for (std::size_t index = 2; index < population; ++index) {
    const auto player = index % 2 == 0 ? PlayerId::One : PlayerId::Two;
    const auto column = static_cast<std::int32_t>(index % columns);
    const auto row = static_cast<std::int32_t>(index / columns);
    const auto offset = player == PlayerId::One ? 0 : 2'900;
    const Vec2 position =
        world(160 + offset + column * 65, 700 + (row % 38) * 65);
    const auto type = index % 5 == 0 ? EntityType::Skirmisher
                                    : EntityType::Vanguard;
    static_cast<void>(simulation.spawn_entity(player, type, position));
  }
  return simulation;
}

struct Measurement {
  std::size_t population{};
  std::uint64_t step_us{};
  std::uint64_t navigation_us{};
  std::uint64_t ai_us{};
  std::uint64_t spatial_query_us{};
  std::uint64_t visibility_us{};
  std::uint64_t state_hash_us{};
  std::uint64_t approximate_bytes{};
  std::uint64_t guard{};
};

[[nodiscard]] Measurement measure(const std::size_t population) {
  auto simulation = make_simulation(population);
  simulation.step();

  std::uint64_t guard = 0;
  CommanderAI commander{PlayerId::One};
  const auto observation = simulation.observe(PlayerId::One);
  const auto ai_us = average_microseconds(8, [&] {
    const auto plan = commander.plan(observation);
    guard ^= plan.decisions.size();
  });

  const auto step_us = average_microseconds(3, [&] {
    simulation.step();
    guard ^= simulation.tick();
  });

  std::vector<EntityId> movers;
  for (const auto& entity : simulation.entities()) {
    if (entity.owner == PlayerId::One && entity.kind == EntityKind::Unit &&
        movers.size() < 32) {
      movers.push_back(entity.id);
    }
  }
  const auto navigation_us = average_microseconds(3, [&] {
    const auto result = simulation.execute_now(
        Command{.player = PlayerId::One,
                .type = CommandType::Move,
                .entities = movers,
                .target = world(2'600, 2'600)});
    guard ^= result.ok ? 1U : 0U;
    simulation.step();
  });

  constexpr std::int32_t query_radius = 260'000;
  std::vector<SpatialQueryHit> query_hits;
  query_hits.reserve(population);
  const auto spatial_query_us = average_microseconds(64, [&] {
    const auto sample = static_cast<std::int32_t>((guard % 24U) * 110U);
    const auto center = world(700 + sample, 1'100 + sample / 2);
    simulation.spatial_grid().query_radius(center, query_radius, query_hits);
    guard ^= query_hits.size() + 1U;
  });

  const auto visibility_us = average_microseconds(2'048, [&] {
    const auto sample = static_cast<std::int32_t>(guard % 5'800U);
    guard ^= static_cast<std::uint8_t>(simulation.visibility_state_at(
        world(100 + sample, 100 + (sample * 7) % 5'800), PlayerId::One));
  });

  const auto state_hash_us = average_microseconds(8, [&] {
    guard ^= simulation.state_hash();
  });

  const auto& one_visibility = simulation.visibility(PlayerId::One);
  const auto& two_visibility = simulation.visibility(PlayerId::Two);
  const auto approximate_bytes =
      static_cast<std::uint64_t>(simulation.entities().capacity()) *
          sizeof(Entity) +
      static_cast<std::uint64_t>(one_visibility.cells().capacity() +
                                 two_visibility.cells().capacity()) *
          sizeof(VisibilityState) +
      static_cast<std::uint64_t>(simulation.command_trace().capacity()) *
          sizeof(CommandTraceEntry) +
      static_cast<std::uint64_t>(simulation.ai_decision_trace().capacity()) *
          sizeof(AIDecisionRecord) +
      static_cast<std::uint64_t>(simulation.events().capacity()) *
          sizeof(SimulationEvent) +
      static_cast<std::uint64_t>(simulation.casualties().size()) *
          sizeof(CasualtyRecord) +
      static_cast<std::uint64_t>(simulation.casualty_history().size()) *
          sizeof(CasualtyTransition) +
      simulation.spatial_grid().approximate_memory_bytes();

  return {population,        step_us,       navigation_us, ai_us,
          spatial_query_us,  visibility_us, state_hash_us, approximate_bytes,
          guard};
}

}  // namespace

int main() {
  constexpr std::array<std::size_t, 4> populations{100, 250, 500, 1'000};
  std::cout << "population,step_us,navigation_us,ai_us,spatial_query_us,"
               "visibility_us,state_hash_us,approximate_bytes,guard\n";
  for (const auto population : populations) {
    const auto result = measure(population);
    std::cout << result.population << ',' << result.step_us << ','
              << result.navigation_us << ',' << result.ai_us << ','
              << result.spatial_query_us << ',' << result.visibility_us << ','
              << result.state_hash_us << ',' << result.approximate_bytes << ','
              << result.guard << '\n';
  }
  return 0;
}
