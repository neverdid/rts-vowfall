#include "ashen/benchmark/SelfPlay.hpp"

#include "ashen/core/Catalog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ashen::benchmark {
namespace {

inline constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
inline constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;
inline constexpr core::Tick kRetreatEvaluationTicks = 200;

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept {
  hash ^= value;
  hash *= kFnvPrime;
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    hash_byte(hash, static_cast<std::uint8_t>(value & 0xffU));
    value >>= 8U;
  }
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    hash_byte(hash, static_cast<std::uint8_t>(value & 0xffU));
    value >>= 8U;
  }
}

void hash_i32(std::uint64_t& hash, const std::int32_t value) noexcept {
  hash_u32(hash, static_cast<std::uint32_t>(value));
}

void hash_bool(std::uint64_t& hash, const bool value) noexcept {
  hash_byte(hash, value ? 1U : 0U);
}

void hash_vec(std::uint64_t& hash, const core::Vec2 value) noexcept {
  hash_i32(hash, value.x);
  hash_i32(hash, value.y);
}

[[nodiscard]] std::uint64_t text_hash(const std::string_view value) noexcept {
  auto hash = kFnvOffset;
  for (const auto character : value) {
    hash_byte(hash, static_cast<std::uint8_t>(character));
  }
  return hash;
}

void hash_command(std::uint64_t& hash, const core::Command& command) noexcept {
  hash_u64(hash, command.execute_tick);
  hash_u64(hash, command.sequence);
  hash_byte(hash, static_cast<std::uint8_t>(command.player));
  hash_byte(hash, static_cast<std::uint8_t>(command.type));
  hash_u64(hash, static_cast<std::uint64_t>(command.entities.size()));
  for (const auto entity : command.entities) {
    hash_u32(hash, entity.value);
  }
  hash_vec(hash, command.target);
  hash_u32(hash, command.target_entity.value);
  hash_u32(hash, command.resource.value);
  hash_u32(hash, command.producer.value);
  hash_byte(hash, static_cast<std::uint8_t>(command.train_type));
  hash_byte(hash, static_cast<std::uint8_t>(command.building_type));
  hash_byte(hash, static_cast<std::uint8_t>(command.research));
  hash_byte(hash, static_cast<std::uint8_t>(command.stance));
  hash_bool(hash, command.queue);
}

void hash_ai_candidate(std::uint64_t& hash, const core::AICandidateScore& candidate) noexcept {
  hash_byte(hash, static_cast<std::uint8_t>(candidate.action));
  hash_u32(hash, candidate.target_entity.value);
  hash_u32(hash, candidate.target_objective.value);
  hash_vec(hash, candidate.target_position);
  hash_bool(hash, candidate.entity_type.has_value());
  if (candidate.entity_type.has_value()) {
    hash_byte(hash, static_cast<std::uint8_t>(*candidate.entity_type));
  }
  hash_bool(hash, candidate.research.has_value());
  if (candidate.research.has_value()) {
    hash_byte(hash, static_cast<std::uint8_t>(*candidate.research));
  }
  hash_u64(hash, candidate.influence_map_hash);
  hash_bool(hash, candidate.influence_sample.has_value());
  if (candidate.influence_sample.has_value()) {
    const auto& sample = *candidate.influence_sample;
    hash_i32(hash, sample.column);
    hash_i32(hash, sample.row);
    hash_vec(hash, sample.center);
    hash_i32(hash, sample.cell.friendly_power);
    hash_i32(hash, sample.cell.observed_enemy_power);
    hash_i32(hash, sample.cell.static_danger);
    hash_i32(hash, sample.cell.objective_value);
    hash_i32(hash, sample.cell.travel_cost);
    hash_i32(hash, sample.cell.terror_pressure);
    hash_i32(hash, sample.cell.friendly_terror);
    hash_i32(hash, sample.cell.friendly_ward);
    hash_i32(hash, sample.cell.resolve_vulnerability);
    hash_i32(hash, sample.cell.uncertainty);
    hash_bool(hash, sample.cell.navigable);
  }
  hash_i32(hash, candidate.total_score);
  hash_u64(hash, static_cast<std::uint64_t>(candidate.components.size()));
  for (const auto& component : candidate.components) {
    hash_byte(hash, static_cast<std::uint8_t>(component.reason));
    hash_i32(hash, component.score);
  }
}

[[nodiscard]] constexpr bool is_army_unit(const core::EntityType type) noexcept {
  return type == core::EntityType::Vanguard || type == core::EntityType::Skirmisher;
}

[[nodiscard]] std::size_t army_count(const core::PlayerObservation& observation) noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      observation.owned_entities(), [](const core::Entity& entity) {
        return is_army_unit(entity.type) && entity.alive() && !entity.under_construction;
      }));
}

[[nodiscard]] std::int32_t army_value(const core::PlayerObservation& observation) noexcept {
  std::int64_t value = 0;
  for (const auto& entity : observation.owned_entities()) {
    if (is_army_unit(entity.type) && entity.alive() && !entity.under_construction) {
      value += core::entity_definition(observation.self().faction, entity.type).cost;
    }
  }
  return static_cast<std::int32_t>(value);
}

[[nodiscard]] std::size_t worker_count(const core::PlayerObservation& observation) noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      observation.owned_entities(), [](const core::Entity& entity) {
        return entity.type == core::EntityType::Worker && entity.alive() && !entity.under_construction;
      }));
}

[[nodiscard]] const core::Entity* first_entity(const core::PlayerObservation& observation,
                                                const core::EntityType type,
                                                const bool completed_only) noexcept {
  const auto found = std::ranges::find_if(observation.owned_entities(), [=](const core::Entity& entity) {
    return entity.type == type && entity.alive() && (!completed_only || !entity.under_construction);
  });
  return found == observation.owned_entities().end() ? nullptr : &*found;
}

[[nodiscard]] core::Tick count_avoidable_idle_producers(
    const core::PlayerObservation& observation) noexcept {
  std::vector<core::EntityId> producers;
  for (const auto& capability : observation.capabilities()) {
    if (capability.type != core::CommandType::Train || !capability.actor) {
      continue;
    }
    const auto entity = std::ranges::find(observation.owned_entities(), capability.actor,
                                          &core::Entity::id);
    if (entity != observation.owned_entities().end() && !entity->under_construction &&
        entity->production_queue.empty()) {
      producers.push_back(entity->id);
    }
  }
  std::ranges::sort(producers, {}, &core::EntityId::value);
  const auto unique_end = std::ranges::unique(producers).begin();
  return static_cast<core::Tick>(std::distance(producers.begin(), unique_end));
}

[[nodiscard]] bool has_visible_enemy(const core::PlayerObservation& observation) noexcept {
  return std::ranges::any_of(observation.known_enemies(), [](const core::ObservedEnemy& enemy) {
    return enemy.currently_visible;
  });
}

[[nodiscard]] std::string_view player_name(const core::PlayerId player) noexcept {
  return player == core::PlayerId::One ? "one" : "two";
}

[[nodiscard]] std::string_view faction_name(const core::FactionId faction) noexcept {
  switch (faction) {
    case core::FactionId::Compact:
      return "compact";
    case core::FactionId::Ascendancy:
      return "ascendancy";
    case core::FactionId::Concord:
      return "concord";
  }
  return "unknown";
}

[[nodiscard]] std::string_view spawn_name(const SpawnSide spawn) noexcept {
  return spawn == SpawnSide::Left ? "left" : "right";
}

[[nodiscard]] std::string_view command_error_name(const core::CommandError error) noexcept {
  switch (error) {
    case core::CommandError::None:
      return "none";
    case core::CommandError::InvalidOwner:
      return "invalid_owner";
    case core::CommandError::InvalidEntity:
      return "invalid_entity";
    case core::CommandError::InvalidTarget:
      return "invalid_target";
    case core::CommandError::InvalidProducer:
      return "invalid_producer";
    case core::CommandError::InvalidUnitType:
      return "invalid_unit_type";
    case core::CommandError::InsufficientOre:
      return "insufficient_ore";
    case core::CommandError::SupplyBlocked:
      return "supply_blocked";
    case core::CommandError::PlacementBlocked:
      return "placement_blocked";
    case core::CommandError::UnderConstruction:
      return "under_construction";
    case core::CommandError::QueueFull:
      return "queue_full";
    case core::CommandError::PrerequisiteMissing:
      return "prerequisite_missing";
    case core::CommandError::AlreadyResearched:
      return "already_researched";
    case core::CommandError::ResearchBusy:
      return "research_busy";
    case core::CommandError::PowerCooldown:
      return "power_cooldown";
  }
  return "unknown";
}

void record_rejection(PlayerMatchReport& report, const core::CommandError error) {
  const auto found = std::ranges::find(report.rejection_reasons, error, &CommandErrorCount::error);
  if (found == report.rejection_reasons.end()) {
    report.rejection_reasons.push_back(CommandErrorCount{error, 1});
  } else {
    ++found->count;
  }
}

struct PendingRetreat {
  core::PlayerId owner{core::PlayerId::One};
  core::EntityId entity{};
  core::Tick evaluation_tick{};
};

void evaluate_retreat(const core::Simulation& simulation, const PendingRetreat& retreat,
                      std::array<PlayerMatchReport, 2>& reports) noexcept {
  auto& report = reports[core::player_index(retreat.owner)];
  ++report.retreat_units_evaluated;
  const auto* entity = simulation.find_entity(retreat.entity);
  if (entity != nullptr && entity->alive() && entity->owner == retreat.owner) {
    ++report.retreat_survivors;
  }
}

void process_trace(const core::Simulation& simulation, std::size_t& trace_cursor,
                   std::vector<PendingRetreat>& pending_retreats,
                   std::array<PlayerMatchReport, 2>& reports) {
  const auto& trace = simulation.command_trace();
  while (trace_cursor < trace.size()) {
    const auto& entry = trace[trace_cursor++];
    if (entry.source != core::CommandSource::CommanderAI) {
      continue;
    }
    auto& report = reports[core::player_index(entry.command.player)];
    ++report.commands_issued;
    if (entry.accepted) {
      ++report.commands_accepted;
    } else {
      ++report.commands_rejected;
      record_rejection(report, entry.error);
    }
    if (entry.observation_hash == 0) {
      ++report.commands_without_observation;
    }
    if (entry.accepted && entry.command.type == core::CommandType::Retreat) {
      for (const auto entity : entry.command.entities) {
        pending_retreats.push_back(
            PendingRetreat{entry.command.player, entity, entry.applied_tick + kRetreatEvaluationTicks});
      }
    }
  }
}

void update_player_metrics(const core::PlayerObservation& observation,
                           const std::size_t initial_army_count,
                           PlayerMatchReport& report) noexcept {
  if (!report.first_barracks_started.has_value() &&
      first_entity(observation, core::EntityType::Barracks, false) != nullptr) {
    report.first_barracks_started = observation.tick();
  }
  if (!report.first_barracks_completed.has_value() &&
      first_entity(observation, core::EntityType::Barracks, true) != nullptr) {
    report.first_barracks_completed = observation.tick();
  }
  if (!report.tier_two_completed.has_value() && observation.self().tech_tier >= 2) {
    report.tier_two_completed = observation.tick();
  }
  if (!report.fifth_worker.has_value() && worker_count(observation) >= 5) {
    report.fifth_worker = observation.tick();
  }
  if (!report.first_reinforcement.has_value() && army_count(observation) > initial_army_count) {
    report.first_reinforcement = observation.tick();
  }
  if (!report.first_contact.has_value() && has_visible_enemy(observation)) {
    report.first_contact = observation.tick();
    report.ore_at_first_contact = observation.self().ore;
  }

  report.peak_ore = std::max(report.peak_ore, observation.self().ore);
  report.peak_army_value = std::max(report.peak_army_value, army_value(observation));
  report.avoidable_idle_production_ticks += count_avoidable_idle_producers(observation);
}

void add_checkpoint(const core::Simulation& simulation, MatchReport& report) {
  report.checkpoints.push_back(
      CheckpointReport{simulation.tick(), simulation.state_hash(),
                       command_trace_hash(simulation.command_trace()),
                       ai_decision_trace_hash(simulation.ai_decision_trace())});
}

void add_failure(SuiteReport& suite, const MatchReport& match, std::string code,
                 std::string message) {
  suite.hard_failures.push_back(
      HardFailure{match.scenario, match.seed, std::move(code), std::move(message)});
}

void audit_match(SuiteReport& suite, const MatchReport& match) {
  if (match.timed_out || !match.winner.has_value()) {
    add_failure(suite, match, "match_timeout",
                "The benchmark reached its tick budget without an "
                "authoritative winner.");
  }
  if (match.players[0].commands_issued + match.players[1].commands_issued == 0) {
    add_failure(suite, match, "empty_command_trace",
                "The commanders produced no applied command trace.");
  }
  if (match.invalid_ai_decisions != 0 || match.unlinked_ai_commands != 0) {
    add_failure(suite, match, "invalid_ai_decision_trace",
                "AI decision scores, provenance, or command-result links "
                "failed validation.");
  }
  if (!match.timed_out && match.unresolved_ai_decisions != 0) {
    add_failure(suite, match, "unresolved_ai_decision",
                "A completed match retained an AI decision without an "
                "authoritative command result.");
  }
  constexpr std::array<std::string_view, 3> layer_names{"strategic", "tactical", "micro"};
  for (std::size_t layer = 0; layer < layer_names.size(); ++layer) {
    if (match.players[0].ai_decisions_by_layer[layer] +
            match.players[1].ai_decisions_by_layer[layer] ==
        0) {
      add_failure(suite, match, "missing_ai_layer",
                  "The full match produced no " + std::string{layer_names[layer]} +
                      " decision record.");
    }
  }
  if (!match.first_contact_tick.has_value()) {
    add_failure(suite, match, "missing_first_contact",
                "The opponents never established visible contact.");
  } else if (*match.first_contact_tick > match.maximum_ticks * 3U / 4U) {
    add_failure(suite, match, "late_first_contact",
                "First contact occurred after three quarters of the match budget.");
  }

  for (const auto& player : match.players) {
    const auto prefix = std::string{"Player "} + std::string{player_name(player.player)};
    if (!player.first_barracks_completed.has_value()) {
      add_failure(suite, match, "opening_barracks_missing",
                  prefix + " never completed its first production building.");
    }
    if (!player.first_reinforcement.has_value()) {
      add_failure(suite, match, "reinforcement_missing",
                  prefix + " never produced a combat reinforcement.");
    }
    if (player.commands_without_observation != 0) {
      add_failure(suite, match, "missing_observation_hash",
                  prefix + " issued commands without an auditable observation hash.");
    }
    if (player.commands_issued >= 20 && player.commands_rejected * 20U > player.commands_issued) {
      add_failure(suite, match, "command_rejection_rate",
                  prefix + " had more than five percent of commands rejected by the core.");
    }
  }
}

struct CohortAccumulator {
  std::string name{};
  std::uint64_t games{};
  std::uint64_t wins{};
  std::uint64_t losses{};
  std::uint64_t draws{};
};

[[nodiscard]] std::vector<WinRateReport> calculate_win_rates(
    const std::vector<MatchReport>& matches) {
  std::array<CohortAccumulator, 5> cohorts = {
      CohortAccumulator{"faction:compact"}, CohortAccumulator{"faction:ascendancy"},
      CohortAccumulator{"faction:concord"}, CohortAccumulator{"spawn:left"},
      CohortAccumulator{"spawn:right"},
  };

  for (const auto& match : matches) {
    for (const auto& player : match.players) {
      const auto faction_index = static_cast<std::size_t>(player.faction);
      const auto spawn_index = player.spawn == SpawnSide::Left ? std::size_t{3} : std::size_t{4};
      for (const auto index : {faction_index, spawn_index}) {
        auto& cohort = cohorts[index];
        ++cohort.games;
        if (!match.winner.has_value()) {
          ++cohort.draws;
        } else if (*match.winner == player.player) {
          ++cohort.wins;
        } else {
          ++cohort.losses;
        }
      }
    }
  }

  std::vector<WinRateReport> result;
  result.reserve(cohorts.size());
  for (const auto& cohort : cohorts) {
    const auto basis_points = cohort.games == 0
                                  ? 0U
                                  : static_cast<std::uint32_t>(cohort.wins * 10'000U / cohort.games);
    result.push_back(
        WinRateReport{cohort.name, cohort.games, cohort.wins, cohort.losses, cohort.draws, basis_points});
  }
  return result;
}

[[nodiscard]] std::vector<BalanceAlert> calculate_balance_alerts(
    const std::vector<WinRateReport>& win_rates) {
  std::vector<BalanceAlert> alerts;
  for (const auto& rate : win_rates) {
    if (rate.games < 4) {
      continue;
    }
    const auto is_spawn = rate.cohort.starts_with("spawn:");
    const auto lower = is_spawn ? 3'500U : 2'500U;
    const auto upper = is_spawn ? 6'500U : 7'500U;
    if (rate.win_rate_basis_points < lower || rate.win_rate_basis_points > upper) {
      alerts.push_back(BalanceAlert{
          rate.cohort,
          "Observed win rate of " + std::to_string(rate.win_rate_basis_points) +
              " basis points is outside the tuning band; this is an alert, not "
              "a correctness failure.",
      });
    }
  }
  return alerts;
}

[[nodiscard]] std::string hex_hash(const std::uint64_t value) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return output.str();
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    switch (character) {
      case '\"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (byte < 0x20U) {
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<unsigned int>(byte) << std::dec;
        } else {
          output << character;
        }
        break;
    }
  }
  return output.str();
}

void write_indent(std::ostringstream& output, const std::size_t spaces) {
  output << std::string(spaces, ' ');
}

void write_optional_tick(std::ostringstream& output, const std::optional<core::Tick> tick) {
  if (tick.has_value()) {
    output << *tick;
  } else {
    output << "null";
  }
}

void write_player_json(std::ostringstream& output, const PlayerMatchReport& player,
                       const std::size_t indent) {
  write_indent(output, indent);
  output << "{\n";
  write_indent(output, indent + 2);
  output << "\"player\": \"" << player_name(player.player) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"faction\": \"" << faction_name(player.faction) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"difficulty\": \"" << core::to_string(player.difficulty)
         << "\",\n";
  write_indent(output, indent + 2);
  output << "\"spawn\": \"" << spawn_name(player.spawn) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"first_barracks_started_tick\": ";
  write_optional_tick(output, player.first_barracks_started);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"first_barracks_completed_tick\": ";
  write_optional_tick(output, player.first_barracks_completed);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"tier_two_completed_tick\": ";
  write_optional_tick(output, player.tier_two_completed);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"fifth_worker_tick\": ";
  write_optional_tick(output, player.fifth_worker);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"first_reinforcement_tick\": ";
  write_optional_tick(output, player.first_reinforcement);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"first_contact_tick\": ";
  write_optional_tick(output, player.first_contact);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"ore_at_first_contact\": " << player.ore_at_first_contact << ",\n";
  write_indent(output, indent + 2);
  output << "\"peak_ore\": " << player.peak_ore << ",\n";
  write_indent(output, indent + 2);
  output << "\"peak_army_value\": " << player.peak_army_value << ",\n";
  write_indent(output, indent + 2);
  output << "\"final_army_value\": " << player.final_army_value << ",\n";
  write_indent(output, indent + 2);
  output << "\"avoidable_idle_production_ticks\": "
         << player.avoidable_idle_production_ticks << ",\n";
  write_indent(output, indent + 2);
  output << "\"commands_issued\": " << player.commands_issued << ",\n";
  write_indent(output, indent + 2);
  output << "\"commands_accepted\": " << player.commands_accepted << ",\n";
  write_indent(output, indent + 2);
  output << "\"commands_rejected\": " << player.commands_rejected << ",\n";
  write_indent(output, indent + 2);
  output << "\"commands_without_observation\": " << player.commands_without_observation << ",\n";
  write_indent(output, indent + 2);
  output << "\"ai_decisions_by_layer\": {\"strategic\": "
         << player.ai_decisions_by_layer[static_cast<std::size_t>(core::AIDecisionLayer::Strategic)]
         << ", \"tactical\": "
         << player.ai_decisions_by_layer[static_cast<std::size_t>(core::AIDecisionLayer::Tactical)]
         << ", \"micro\": "
         << player.ai_decisions_by_layer[static_cast<std::size_t>(core::AIDecisionLayer::Micro)]
         << "},\n";
  write_indent(output, indent + 2);
  output << "\"ai_decisions_accepted\": " << player.ai_decisions_accepted << ",\n";
  write_indent(output, indent + 2);
  output << "\"ai_decisions_rejected\": " << player.ai_decisions_rejected << ",\n";
  write_indent(output, indent + 2);
  output << "\"ai_decisions_unresolved\": " << player.ai_decisions_unresolved << ",\n";
  write_indent(output, indent + 2);
  output << "\"ai_candidates_available\": " << player.ai_candidates_available
         << ",\n";
  write_indent(output, indent + 2);
  output << "\"ai_candidates_evaluated\": " << player.ai_candidates_evaluated
         << ",\n";
  write_indent(output, indent + 2);
  output << "\"ai_mistakes\": " << player.ai_mistakes << ",\n";
  write_indent(output, indent + 2);
  output << "\"average_ai_quality_basis_points\": "
         << player.average_ai_quality_basis_points << ",\n";
  write_indent(output, indent + 2);
  output << "\"average_knowledge_delay_ticks\": "
         << player.average_knowledge_delay_ticks << ",\n";
  write_indent(output, indent + 2);
  output << "\"rejection_reasons\": [";
  for (std::size_t index = 0; index < player.rejection_reasons.size(); ++index) {
    const auto& reason = player.rejection_reasons[index];
    if (index != 0) {
      output << ", ";
    }
    output << "{\"error\": \"" << command_error_name(reason.error) << "\", \"count\": "
           << reason.count << '}';
  }
  output << "],\n";
  write_indent(output, indent + 2);
  output << "\"commands_per_minute_milli\": " << player.commands_per_minute_milli << ",\n";
  write_indent(output, indent + 2);
  output << "\"retreat_units_evaluated\": " << player.retreat_units_evaluated << ",\n";
  write_indent(output, indent + 2);
  output << "\"retreat_survivors\": " << player.retreat_survivors << ",\n";
  write_indent(output, indent + 2);
  output << "\"retreat_survival_basis_points\": ";
  if (player.retreat_survival_basis_points.has_value()) {
    output << *player.retreat_survival_basis_points;
  } else {
    output << "null";
  }
  output << '\n';
  write_indent(output, indent);
  output << '}';
}

void write_match_json(std::ostringstream& output, const MatchReport& match,
                      const std::size_t indent) {
  write_indent(output, indent);
  output << "{\n";
  write_indent(output, indent + 2);
  output << "\"scenario\": \"" << json_escape(match.scenario) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"seed\": " << match.seed << ",\n";
  write_indent(output, indent + 2);
  output << "\"maximum_ticks\": " << match.maximum_ticks << ",\n";
  write_indent(output, indent + 2);
  output << "\"duration_ticks\": " << match.duration_ticks << ",\n";
  write_indent(output, indent + 2);
  output << "\"timed_out\": " << (match.timed_out ? "true" : "false") << ",\n";
  write_indent(output, indent + 2);
  output << "\"winner\": ";
  if (match.winner.has_value()) {
    output << '\"' << player_name(*match.winner) << '\"';
  } else {
    output << "null";
  }
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"first_contact_tick\": ";
  write_optional_tick(output, match.first_contact_tick);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"final_state_hash\": \"" << hex_hash(match.final_state_hash) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"command_trace_hash\": \"" << hex_hash(match.command_trace_hash) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"ai_decision_trace_hash\": \"" << hex_hash(match.ai_decision_trace_hash)
         << "\",\n";
  write_indent(output, indent + 2);
  output << "\"invalid_ai_decisions\": " << match.invalid_ai_decisions << ",\n";
  write_indent(output, indent + 2);
  output << "\"unresolved_ai_decisions\": " << match.unresolved_ai_decisions << ",\n";
  write_indent(output, indent + 2);
  output << "\"unlinked_ai_commands\": " << match.unlinked_ai_commands << ",\n";
  write_indent(output, indent + 2);
  output << "\"checkpoints\": [";
  if (!match.checkpoints.empty()) {
    output << '\n';
    for (std::size_t index = 0; index < match.checkpoints.size(); ++index) {
      const auto& checkpoint = match.checkpoints[index];
      write_indent(output, indent + 4);
      output << "{\"tick\": " << checkpoint.tick << ", \"state_hash\": \""
             << hex_hash(checkpoint.state_hash) << "\", \"command_trace_hash\": \""
             << hex_hash(checkpoint.command_trace_hash) << "\", \"ai_decision_trace_hash\": \""
             << hex_hash(checkpoint.ai_decision_trace_hash) << "\"}";
      output << (index + 1 == match.checkpoints.size() ? "\n" : ",\n");
    }
    write_indent(output, indent + 2);
  }
  output << "],\n";
  write_indent(output, indent + 2);
  output << "\"players\": [\n";
  write_player_json(output, match.players[0], indent + 4);
  output << ",\n";
  write_player_json(output, match.players[1], indent + 4);
  output << '\n';
  write_indent(output, indent + 2);
  output << "]\n";
  write_indent(output, indent);
  output << '}';
}

void write_fixed_scenario_json(std::ostringstream& output,
                               const FixedScenarioReport& scenario,
                               const std::size_t indent) {
  write_indent(output, indent);
  output << "{\n";
  write_indent(output, indent + 2);
  output << "\"scenario\": \"" << json_escape(scenario.scenario) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"seed\": " << scenario.seed << ",\n";
  write_indent(output, indent + 2);
  output << "\"faction\": \"" << faction_name(scenario.faction) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"maximum_ticks\": " << scenario.maximum_ticks << ",\n";
  write_indent(output, indent + 2);
  output << "\"duration_ticks\": " << scenario.duration_ticks << ",\n";
  write_indent(output, indent + 2);
  output << "\"passed\": " << (scenario.passed ? "true" : "false") << ",\n";
  write_indent(output, indent + 2);
  output << "\"first_contact_tick\": ";
  write_optional_tick(output, scenario.first_contact_tick);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"first_reinforcement_tick\": ";
  write_optional_tick(output, scenario.first_reinforcement_tick);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"first_barracks_completed_tick\": ";
  write_optional_tick(output, scenario.first_barracks_completed_tick);
  output << ",\n";
  write_indent(output, indent + 2);
  output << "\"final_workers\": " << scenario.final_workers << ",\n";
  write_indent(output, indent + 2);
  output << "\"final_army_units\": " << scenario.final_army_units << ",\n";
  write_indent(output, indent + 2);
  output << "\"final_barracks\": " << scenario.final_barracks << ",\n";
  write_indent(output, indent + 2);
  output << "\"final_barracks_including_construction\": "
         << scenario.final_barracks_including_construction << ",\n";
  write_indent(output, indent + 2);
  output << "\"final_command_hit_points\": " << scenario.final_command_hit_points << ",\n";
  write_indent(output, indent + 2);
  output << "\"final_state_hash\": \"" << hex_hash(scenario.final_state_hash) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"command_trace_hash\": \"" << hex_hash(scenario.command_trace_hash) << "\",\n";
  write_indent(output, indent + 2);
  output << "\"ai_decision_trace_hash\": \"" << hex_hash(scenario.ai_decision_trace_hash)
         << "\",\n";
  write_indent(output, indent + 2);
  output << "\"invalid_ai_decisions\": " << scenario.invalid_ai_decisions << ",\n";
  write_indent(output, indent + 2);
  output << "\"unresolved_ai_decisions\": " << scenario.unresolved_ai_decisions << ",\n";
  write_indent(output, indent + 2);
  output << "\"unlinked_ai_commands\": " << scenario.unlinked_ai_commands << ",\n";
  write_indent(output, indent + 2);
  output << "\"checks\": [";
  if (!scenario.checks.empty()) {
    output << '\n';
    for (std::size_t index = 0; index < scenario.checks.size(); ++index) {
      const auto& check = scenario.checks[index];
      write_indent(output, indent + 4);
      output << "{\"id\": \"" << json_escape(check.id) << "\", \"passed\": "
             << (check.passed ? "true" : "false") << ", \"message\": \""
             << json_escape(check.message) << "\"}";
      output << (index + 1 == scenario.checks.size() ? "\n" : ",\n");
    }
    write_indent(output, indent + 2);
  }
  output << "]\n";
  write_indent(output, indent);
  output << '}';
}

}  // namespace

const std::vector<BenchmarkCase>& standard_cases() {
  static const std::vector<BenchmarkCase> cases = {
      {"compact-left__ascendancy-right", core::FactionId::Compact,
       core::FactionId::Ascendancy},
      {"ascendancy-left__compact-right", core::FactionId::Ascendancy,
       core::FactionId::Compact},
      {"compact-left__concord-right", core::FactionId::Compact, core::FactionId::Concord},
      {"concord-left__compact-right", core::FactionId::Concord, core::FactionId::Compact},
      {"ascendancy-left__concord-right", core::FactionId::Ascendancy,
       core::FactionId::Concord},
      {"concord-left__ascendancy-right", core::FactionId::Concord,
       core::FactionId::Ascendancy},
  };
  return cases;
}

const std::vector<FixedScenarioCase>& standard_fixed_scenarios() {
  static const std::vector<FixedScenarioCase> scenarios = {
      {FixedScenarioId::EconomyDeficitRecovery, "economy-deficit-recovery__compact",
       core::FactionId::Compact, 5'000},
      {FixedScenarioId::EconomyDeficitRecovery, "economy-deficit-recovery__ascendancy",
       core::FactionId::Ascendancy, 5'000},
      {FixedScenarioId::EconomyDeficitRecovery, "economy-deficit-recovery__concord",
       core::FactionId::Concord, 5'000},
      {FixedScenarioId::BlockedOpeningRecovery, "blocked-opening-recovery__compact",
       core::FactionId::Compact, 3'000},
      {FixedScenarioId::BlockedOpeningRecovery, "blocked-opening-recovery__ascendancy",
       core::FactionId::Ascendancy, 3'000},
      {FixedScenarioId::BlockedOpeningRecovery, "blocked-opening-recovery__concord",
       core::FactionId::Concord, 3'000},
      {FixedScenarioId::EarlyRushSurvival, "early-rush-survival__compact",
       core::FactionId::Compact, 3'200},
      {FixedScenarioId::EarlyRushSurvival, "early-rush-survival__ascendancy",
       core::FactionId::Ascendancy, 3'200},
      {FixedScenarioId::EarlyRushSurvival, "early-rush-survival__concord",
       core::FactionId::Concord, 3'200},
      {FixedScenarioId::TacticalFlankChoice, "tactical-flank-choice__compact",
       core::FactionId::Compact, 32},
      {FixedScenarioId::TacticalFlankChoice,
       "tactical-flank-choice__ascendancy", core::FactionId::Ascendancy, 32},
      {FixedScenarioId::TacticalFlankChoice, "tactical-flank-choice__concord",
       core::FactionId::Concord, 32},
      {FixedScenarioId::TacticalDangerAvoidance,
       "tactical-danger-avoidance__compact", core::FactionId::Compact, 32},
      {FixedScenarioId::TacticalDangerAvoidance,
       "tactical-danger-avoidance__ascendancy", core::FactionId::Ascendancy,
       32},
      {FixedScenarioId::TacticalDangerAvoidance,
       "tactical-danger-avoidance__concord", core::FactionId::Concord, 32},
      {FixedScenarioId::TacticalReinforcement,
       "tactical-reinforcement__compact", core::FactionId::Compact, 32},
      {FixedScenarioId::TacticalReinforcement,
       "tactical-reinforcement__ascendancy", core::FactionId::Ascendancy, 32},
      {FixedScenarioId::TacticalReinforcement,
       "tactical-reinforcement__concord", core::FactionId::Concord, 32},
      {FixedScenarioId::TacticalRetreatDestination,
       "tactical-retreat-destination__compact", core::FactionId::Compact, 32},
      {FixedScenarioId::TacticalRetreatDestination,
       "tactical-retreat-destination__ascendancy", core::FactionId::Ascendancy,
       32},
      {FixedScenarioId::TacticalRetreatDestination,
       "tactical-retreat-destination__concord", core::FactionId::Concord, 32},
      {FixedScenarioId::FactionLossTolerance,
       "faction-loss-tolerance__compact", core::FactionId::Compact, 32},
      {FixedScenarioId::FactionLossTolerance,
       "faction-loss-tolerance__ascendancy", core::FactionId::Ascendancy, 32},
      {FixedScenarioId::FactionLossTolerance,
       "faction-loss-tolerance__concord", core::FactionId::Concord, 32},
  };
  return scenarios;
}

std::uint64_t command_trace_hash(const std::vector<core::CommandTraceEntry>& trace) noexcept {
  auto hash = kFnvOffset;
  hash_u64(hash, static_cast<std::uint64_t>(trace.size()));
  for (const auto& entry : trace) {
    hash_u64(hash, entry.issued_tick);
    hash_u64(hash, entry.applied_tick);
    hash_byte(hash, static_cast<std::uint8_t>(entry.source));
    hash_u64(hash, entry.observation_hash);
    hash_u64(hash, entry.ai_decision_id);
    hash_command(hash, entry.command);
    hash_bool(hash, entry.accepted);
    hash_byte(hash, static_cast<std::uint8_t>(entry.error));
  }
  return hash;
}

std::uint64_t ai_decision_trace_hash(
    const std::vector<core::AIDecisionRecord>& trace) noexcept {
  auto hash = kFnvOffset;
  hash_u64(hash, static_cast<std::uint64_t>(trace.size()));
  for (const auto& record : trace) {
    hash_u64(hash, record.id);
    hash_u64(hash, record.observation_tick);
    hash_u64(hash, record.observation_hash);
    hash_byte(hash, static_cast<std::uint8_t>(record.player));
    hash_byte(hash, static_cast<std::uint8_t>(record.layer));
    hash_u64(hash, record.cadence_ticks);
    hash_byte(hash, static_cast<std::uint8_t>(record.difficulty));
    hash_u64(hash, record.difficulty_hash);
    hash_u64(hash, record.knowledge_tick);
    hash_byte(hash, static_cast<std::uint8_t>(record.doctrine_faction));
    hash_byte(hash, static_cast<std::uint8_t>(record.temperament));
    hash_u64(hash, record.doctrine_hash);
    hash_u64(hash, static_cast<std::uint64_t>(record.candidates.size()));
    for (const auto& candidate : record.candidates) {
      hash_ai_candidate(hash, candidate);
    }
    hash_u64(hash, static_cast<std::uint64_t>(record.selected_candidate));
    hash_u64(hash, static_cast<std::uint64_t>(record.evaluated_candidates));
    hash_i32(hash, record.selected_quality_basis_points);
    hash_bool(hash, record.mistake_applied);
    hash_byte(hash, static_cast<std::uint8_t>(record.selected_action));
    hash_byte(hash, static_cast<std::uint8_t>(record.winning_reason));
    hash_vec(hash, record.command_precision_offset);
    hash_u64(hash, record.command_latency_ticks);
    hash_command(hash, record.command);
    hash_u64(hash, record.command_sequence);
    hash_u64(hash, record.applied_tick);
    hash_byte(hash, static_cast<std::uint8_t>(record.command_status));
    hash_byte(hash, static_cast<std::uint8_t>(record.command_error));
  }
  return hash;
}

namespace {

struct DecisionAudit {
  std::uint64_t invalid_records{};
  std::uint64_t unresolved_records{};
  std::uint64_t unlinked_commands{};
};

[[nodiscard]] core::AIUtilityReason expected_winning_reason(
    const core::AICandidateScore& candidate) noexcept {
  auto reason = core::AIUtilityReason::Baseline;
  auto best_score = std::numeric_limits<std::int32_t>::min();
  for (const auto& component : candidate.components) {
    if (component.score > best_score ||
        (component.score == best_score && component.reason < reason)) {
      reason = component.reason;
      best_score = component.score;
    }
  }
  return reason;
}

[[nodiscard]] bool uses_point_precision(
    const core::CommandType type) noexcept {
  switch (type) {
    case core::CommandType::Move:
    case core::CommandType::AttackMove:
    case core::CommandType::Patrol:
    case core::CommandType::SetRallyPoint:
    case core::CommandType::Retreat:
      return true;
    case core::CommandType::Attack:
    case core::CommandType::Gather:
    case core::CommandType::Train:
    case core::CommandType::Stop:
    case core::CommandType::Hold:
    case core::CommandType::Build:
    case core::CommandType::Research:
    case core::CommandType::ActivatePower:
    case core::CommandType::SetStance:
      return false;
  }
  return false;
}

[[nodiscard]] DecisionAudit audit_decisions(
    const std::vector<core::AIDecisionRecord>& decisions,
    const std::vector<core::CommandTraceEntry>& commands) {
  DecisionAudit audit{};
  std::vector<std::uint64_t> ids;
  ids.reserve(decisions.size());
  std::vector<const core::CommandTraceEntry*> ai_commands;
  for (const auto& command : commands) {
    if (command.source == core::CommandSource::CommanderAI) {
      ai_commands.push_back(&command);
    }
  }
  std::ranges::sort(ai_commands, {}, [](const core::CommandTraceEntry* command) {
    return command->ai_decision_id;
  });

  std::vector<const core::AIDecisionRecord*> decisions_by_id;
  decisions_by_id.reserve(decisions.size());
  for (const auto& decision : decisions) {
    decisions_by_id.push_back(&decision);
  }
  std::ranges::sort(decisions_by_id, {}, [](const core::AIDecisionRecord* decision) {
    return decision->id;
  });

  for (const auto& decision : decisions) {
    ids.push_back(decision.id);
    const auto& difficulty =
        core::ai_difficulty_profile(decision.difficulty);
    auto valid = decision.id != 0 && decision.observation_hash != 0 &&
                 decision.cadence_ticks ==
                     core::ai_decision_cadence(decision.layer, difficulty) &&
                 core::ai_decision_due(decision.layer,
                                       decision.observation_tick,
                                       difficulty) &&
                 decision.difficulty_hash ==
                     core::ai_difficulty_hash(difficulty) &&
                 decision.knowledge_tick <= decision.observation_tick &&
                 (decision.knowledge_tick == 0 ||
                  decision.observation_tick - decision.knowledge_tick >=
                      difficulty.reaction_delay_ticks) &&
                 decision.doctrine_hash != 0 &&
                 !decision.candidates.empty() &&
                 decision.selected_candidate < decision.candidates.size() &&
                 decision.evaluated_candidates > 0 &&
                 decision.evaluated_candidates <= decision.candidates.size() &&
                 decision.selected_quality_basis_points >= 0 &&
                 decision.selected_quality_basis_points <= 10'000 &&
                 decision.command_latency_ticks ==
                     difficulty.command_latency_ticks &&
                 decision.command.player == decision.player &&
                 decision.command_sequence != 0 &&
                 decision.command.sequence == decision.command_sequence &&
                 decision.command.execute_tick ==
                     decision.observation_tick +
                         difficulty.command_latency_ticks;

    for (const auto& candidate : decision.candidates) {
      std::int64_t component_total = 0;
      for (const auto& component : candidate.components) {
        component_total += component.score;
      }
      valid = valid && !candidate.components.empty() &&
              component_total == static_cast<std::int64_t>(candidate.total_score);
      if (decision.layer == core::AIDecisionLayer::Tactical) {
        valid = valid && candidate.influence_map_hash != 0 &&
                candidate.influence_sample.has_value();
      }
    }

    if (decision.selected_candidate < decision.candidates.size()) {
      const auto& selected = decision.candidates[decision.selected_candidate];
      const auto precision_x =
          static_cast<std::int64_t>(decision.command_precision_offset.x);
      const auto precision_y =
          static_cast<std::int64_t>(decision.command_precision_offset.y);
      const auto precision_radius =
          static_cast<std::int64_t>(difficulty.command_precision_radius);
      valid = valid && selected.action == decision.selected_action &&
              expected_winning_reason(selected) == decision.winning_reason &&
              precision_x * precision_x + precision_y * precision_y <=
                  precision_radius * precision_radius;
      if (uses_point_precision(decision.command.type)) {
        valid =
            valid &&
            decision.command.target.x ==
                selected.target_position.x +
                    decision.command_precision_offset.x &&
            decision.command.target.y ==
                selected.target_position.y +
                    decision.command_precision_offset.y;
      } else {
        valid = valid && decision.command_precision_offset == core::Vec2{};
      }
      auto best_score = selected.total_score;
      for (const auto& candidate : decision.candidates) {
        best_score = std::max(best_score, candidate.total_score);
      }
      const auto expected_quality =
          best_score <= 0
              ? 0
              : static_cast<std::int32_t>(
                    std::clamp<std::int64_t>(
                        static_cast<std::int64_t>(selected.total_score) *
                            10'000 / best_score,
                        0, 10'000));
      valid = valid &&
              expected_quality == decision.selected_quality_basis_points;
      if (difficulty.utility_search_breadth == 0) {
        valid =
            valid &&
            decision.evaluated_candidates == decision.candidates.size();
      } else {
        valid =
            valid &&
            (decision.evaluated_candidates <=
                 difficulty.utility_search_breadth ||
             decision.evaluated_candidates == decision.candidates.size());
      }
      if (decision.mistake_applied) {
        valid =
            valid &&
            decision.selected_quality_basis_points >=
                difficulty.minimum_mistake_quality_basis_points;
      }
      if (difficulty.utility_search_breadth == 0 &&
          difficulty.mistake_rate_basis_points == 0) {
        valid = valid && selected.total_score == best_score &&
                !decision.mistake_applied;
      }
    }

    const auto command_at = std::ranges::lower_bound(
        ai_commands, decision.id, {}, [](const core::CommandTraceEntry* command) {
          return command->ai_decision_id;
        });
    const auto* command = command_at != ai_commands.end() &&
                                  (*command_at)->ai_decision_id == decision.id
                              ? *command_at
                              : nullptr;
    if (decision.command_status == core::AICommandStatus::Queued) {
      ++audit.unresolved_records;
      valid = valid && command == nullptr && decision.applied_tick == 0 &&
              decision.command_error == core::CommandError::None;
    } else {
      valid = valid && command != nullptr;
      if (command != nullptr) {
        const auto accepted = decision.command_status == core::AICommandStatus::Accepted;
        valid = valid && command->observation_hash == decision.observation_hash &&
                command->command == decision.command &&
                command->command.sequence == decision.command_sequence &&
                command->applied_tick == decision.applied_tick &&
                command->accepted == accepted && command->error == decision.command_error;
      }
    }
    if (!valid) {
      ++audit.invalid_records;
    }
  }

  std::ranges::sort(ids);
  for (std::size_t index = 1; index < ids.size(); ++index) {
    if (ids[index] == ids[index - 1]) {
      ++audit.invalid_records;
    }
  }

  for (const auto* command : ai_commands) {
    const auto decision_at = std::ranges::lower_bound(
        decisions_by_id, command->ai_decision_id, {},
        [](const core::AIDecisionRecord* decision) { return decision->id; });
    const auto* decision = decision_at != decisions_by_id.end() &&
                                   (*decision_at)->id == command->ai_decision_id
                               ? *decision_at
                               : nullptr;
    if (command->ai_decision_id == 0 || decision == nullptr ||
        decision->command_sequence != command->command.sequence) {
      ++audit.unlinked_commands;
    }
  }
  return audit;
}

void record_decision_metrics(const std::vector<core::AIDecisionRecord>& decisions,
                             std::array<PlayerMatchReport, 2>& reports) noexcept {
  for (const auto& decision : decisions) {
    auto& report = reports[core::player_index(decision.player)];
    ++report.ai_decisions_by_layer[static_cast<std::size_t>(decision.layer)];
    report.ai_candidates_available += decision.candidates.size();
    report.ai_candidates_evaluated += decision.evaluated_candidates;
    report.ai_quality_sum_basis_points += static_cast<std::uint64_t>(
        std::max(0, decision.selected_quality_basis_points));
    report.ai_mistakes += decision.mistake_applied ? 1U : 0U;
    report.knowledge_delay_sum_ticks +=
        decision.observation_tick >= decision.knowledge_tick
            ? decision.observation_tick - decision.knowledge_tick
            : 0;
    switch (decision.command_status) {
      case core::AICommandStatus::Queued:
        ++report.ai_decisions_unresolved;
        break;
      case core::AICommandStatus::Accepted:
        ++report.ai_decisions_accepted;
        break;
      case core::AICommandStatus::Rejected:
        ++report.ai_decisions_rejected;
        break;
    }
  }
}

struct ScenarioExecution {
  FixedScenarioReport report{};
  std::vector<core::CommandTraceEntry> trace{};
  std::vector<core::AIDecisionRecord> decision_trace{};
};

struct MatchExecution {
  MatchReport report{};
  std::vector<core::CommandTraceEntry> trace{};
  std::vector<core::AIDecisionRecord> decision_trace{};
};

[[nodiscard]] std::size_t owned_entity_count(const core::Simulation& simulation,
                                             const core::PlayerId owner,
                                             const core::EntityType type,
                                             const bool completed_only = true) noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      simulation.entities(), [=](const core::Entity& entity) {
        return entity.owner == owner && entity.type == type && entity.alive() &&
               (!completed_only || !entity.under_construction);
      }));
}

[[nodiscard]] std::size_t owned_army_count(const core::Simulation& simulation,
                                           const core::PlayerId owner) noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      simulation.entities(), [=](const core::Entity& entity) {
        return entity.owner == owner && is_army_unit(entity.type) && entity.alive() &&
               !entity.under_construction;
      }));
}

void add_scenario_check(FixedScenarioReport& report, std::string id, const bool passed,
                        std::string message) {
  report.checks.push_back(
      ScenarioCheckReport{std::move(id), passed, std::move(message)});
}

[[nodiscard]] constexpr bool is_tactical_exit_scenario(const FixedScenarioId id) noexcept {
  return id == FixedScenarioId::TacticalFlankChoice ||
         id == FixedScenarioId::TacticalDangerAvoidance ||
         id == FixedScenarioId::TacticalReinforcement ||
         id == FixedScenarioId::TacticalRetreatDestination ||
         id == FixedScenarioId::FactionLossTolerance;
}

[[nodiscard]] ScenarioExecution execute_fixed_scenario(const FixedScenarioCase& scenario,
                                                       const std::uint64_t seed) {
  core::SimulationConfig config{};
  config.mode = core::MatchMode::Skirmish;
  config.player_one_faction = scenario.faction;
  config.player_two_faction = core::FactionId::Compact;
  config.commander_players = {true, false};
  config.match_seed = seed;
  const auto tactical_exit_scenario = is_tactical_exit_scenario(scenario.id);
  if (tactical_exit_scenario) {
    config.seed_starting_forces = false;
    config.starting_ore = {0, 0};
    config.map_size = core::world(1'600, 800);
    config.navigation_obstacles.clear();
  }
  if (scenario.id == FixedScenarioId::EconomyDeficitRecovery) {
    config.starting_ore[core::player_index(core::PlayerId::One)] = 40;
  } else if (scenario.id == FixedScenarioId::BlockedOpeningRecovery) {
    config.navigation_obstacles.push_back(
        core::NavigationObstacle{core::world(445, 520), core::world(535, 615)});
  }

  core::Simulation simulation{config};
  std::vector<core::EntityId> rushing_units;
  if (scenario.id == FixedScenarioId::EarlyRushSurvival) {
    rushing_units = {
        simulation.spawn_entity(core::PlayerId::Two, core::EntityType::Vanguard,
                                core::world(900, 635)),
        simulation.spawn_entity(core::PlayerId::Two, core::EntityType::Vanguard,
                                core::world(900, 765)),
    };
    simulation.enqueue(core::Command{.execute_tick = 100,
                                     .player = core::PlayerId::Two,
                                     .type = core::CommandType::AttackMove,
                                     .entities = rushing_units,
                                     .target = core::world(300, 700)});
  } else if (tactical_exit_scenario) {
    static_cast<void>(simulation.spawn_entity(
        core::PlayerId::One, core::EntityType::Command, core::world(160, 400)));
    static_cast<void>(simulation.spawn_entity(core::PlayerId::Two,
                                              core::EntityType::Command,
                                              core::world(1'520, 400)));
    // The hostile fixture must exist for the full Competitive reaction window
    // before the tactical evaluation at tick 30.
    simulation.run(25);

    if (scenario.id == FixedScenarioId::TacticalFlankChoice) {
      const auto first = simulation.spawn_entity(core::PlayerId::One,
                                                 core::EntityType::Vanguard,
                                                 core::world(360, 370));
      const auto second = simulation.spawn_entity(core::PlayerId::One,
                                                  core::EntityType::Vanguard,
                                                  core::world(360, 430));
      const auto ranged = simulation.spawn_entity(core::PlayerId::One,
                                                  core::EntityType::Skirmisher,
                                                  core::world(390, 400));
      const auto contact = simulation.spawn_entity(core::PlayerId::Two,
                                                   core::EntityType::Vanguard,
                                                   core::world(590, 400));
      static_cast<void>(simulation.spawn_entity(core::PlayerId::Two,
                                                core::EntityType::Turret,
                                                core::world(570, 245)));
      static_cast<void>(simulation.execute_now(
          core::Command{.player = core::PlayerId::One,
                        .type = core::CommandType::Hold,
                        .entities = {first, second, ranged}}));
      static_cast<void>(
          simulation.execute_now(core::Command{.player = core::PlayerId::Two,
                                               .type = core::CommandType::Hold,
                                               .entities = {contact}}));
    } else if (scenario.id == FixedScenarioId::TacticalDangerAvoidance) {
      static_cast<void>(simulation.spawn_entity(core::PlayerId::One,
                                                core::EntityType::Vanguard,
                                                core::world(360, 360)));
      static_cast<void>(simulation.spawn_entity(core::PlayerId::One,
                                                core::EntityType::Vanguard,
                                                core::world(360, 400)));
      static_cast<void>(simulation.spawn_entity(core::PlayerId::One,
                                                core::EntityType::Skirmisher,
                                                core::world(360, 440)));
      static_cast<void>(simulation.spawn_entity(core::PlayerId::Two,
                                                core::EntityType::Turret,
                                                core::world(620, 400)));
      static_cast<void>(simulation.add_control_point(core::world(1'300, 400)));
    } else if (scenario.id == FixedScenarioId::TacticalReinforcement) {
      const auto first = simulation.spawn_entity(core::PlayerId::One,
                                                 core::EntityType::Vanguard,
                                                 core::world(330, 370));
      const auto second = simulation.spawn_entity(core::PlayerId::One,
                                                  core::EntityType::Vanguard,
                                                  core::world(330, 430));
      const auto reinforcement = simulation.spawn_entity(
          core::PlayerId::One, core::EntityType::Vanguard,
          core::world(250, 400));
      static_cast<void>(simulation.spawn_entity(core::PlayerId::Two,
                                                core::EntityType::Turret,
                                                core::world(540, 245)));
      static_cast<void>(simulation.execute_now(
          core::Command{.player = core::PlayerId::One,
                        .type = core::CommandType::AttackMove,
                        .entities = {first, second},
                        .target = core::world(700, 400)}));
      static_cast<void>(
          simulation.execute_now(core::Command{.player = core::PlayerId::One,
                                               .type = core::CommandType::Hold,
                                               .entities = {reinforcement}}));
    } else if (scenario.id == FixedScenarioId::FactionLossTolerance) {
      const auto defender = simulation.spawn_entity(
          core::PlayerId::One, core::EntityType::Vanguard,
          core::world(360, 400));
      const auto first = simulation.spawn_entity(
          core::PlayerId::Two, core::EntityType::Vanguard,
          core::world(540, 350));
      const auto second = simulation.spawn_entity(
          core::PlayerId::Two, core::EntityType::Vanguard,
          core::world(540, 450));
      static_cast<void>(
          simulation.execute_now(core::Command{
              .player = core::PlayerId::One,
              .type = core::CommandType::Hold,
              .entities = {defender}}));
      static_cast<void>(
          simulation.execute_now(core::Command{
              .player = core::PlayerId::Two,
              .type = core::CommandType::Hold,
              .entities = {first, second}}));
    } else {
      const auto defender = simulation.spawn_entity(core::PlayerId::One,
                                                    core::EntityType::Vanguard,
                                                    core::world(380, 400));
      const auto first = simulation.spawn_entity(core::PlayerId::Two,
                                                 core::EntityType::Vanguard,
                                                 core::world(540, 350));
      const auto second = simulation.spawn_entity(core::PlayerId::Two,
                                                  core::EntityType::Vanguard,
                                                  core::world(540, 400));
      const auto third = simulation.spawn_entity(core::PlayerId::Two,
                                                 core::EntityType::Vanguard,
                                                 core::world(540, 450));
      static_cast<void>(simulation.spawn_entity(core::PlayerId::Two,
                                                core::EntityType::Turret,
                                                core::world(330, 235)));
      static_cast<void>(
          simulation.execute_now(core::Command{.player = core::PlayerId::One,
                                               .type = core::CommandType::Hold,
                                               .entities = {defender}}));
      static_cast<void>(simulation.execute_now(
          core::Command{.player = core::PlayerId::Two,
                        .type = core::CommandType::Hold,
                        .entities = {first, second, third}}));
    }
  }

  const auto initial_army = owned_army_count(simulation, core::PlayerId::One);
  std::optional<core::Tick> first_contact_tick;
  std::optional<core::Tick> first_reinforcement_tick;
  std::optional<core::Tick> first_barracks_completed_tick;
  bool fifth_worker_observed = false;
  while (simulation.status() == core::MatchStatus::Playing &&
         simulation.tick() < scenario.maximum_ticks) {
    simulation.step();
    const auto observation = simulation.observe(core::PlayerId::One);
    if (!first_contact_tick.has_value() && has_visible_enemy(observation)) {
      first_contact_tick = simulation.tick();
    }
    fifth_worker_observed = fifth_worker_observed || worker_count(observation) >= 5;
    if (!first_reinforcement_tick.has_value() && army_count(observation) > initial_army) {
      first_reinforcement_tick = simulation.tick();
    }
    if (!first_barracks_completed_tick.has_value() &&
        first_entity(observation, core::EntityType::Barracks, true) != nullptr) {
      first_barracks_completed_tick = simulation.tick();
    }
  }

  FixedScenarioReport report{};
  report.scenario = scenario.name;
  report.seed = seed;
  report.faction = scenario.faction;
  report.maximum_ticks = scenario.maximum_ticks;
  report.duration_ticks = simulation.tick();
  report.first_contact_tick = first_contact_tick;
  report.first_reinforcement_tick = first_reinforcement_tick;
  report.first_barracks_completed_tick = first_barracks_completed_tick;
  report.final_workers = static_cast<std::uint32_t>(
      owned_entity_count(simulation, core::PlayerId::One, core::EntityType::Worker));
  report.final_army_units =
      static_cast<std::uint32_t>(owned_army_count(simulation, core::PlayerId::One));
  report.final_barracks = static_cast<std::uint32_t>(
      owned_entity_count(simulation, core::PlayerId::One, core::EntityType::Barracks));
  report.final_barracks_including_construction = static_cast<std::uint32_t>(
      owned_entity_count(simulation, core::PlayerId::One, core::EntityType::Barracks, false));
  const auto command = std::ranges::find_if(simulation.entities(), [](const core::Entity& entity) {
    return entity.owner == core::PlayerId::One && entity.type == core::EntityType::Command &&
           entity.alive();
  });
  if (command != simulation.entities().end()) {
    report.final_command_hit_points = command->hit_points;
  }
  report.final_state_hash = simulation.state_hash();
  report.command_trace_hash = command_trace_hash(simulation.command_trace());
  report.ai_decision_trace_hash = ai_decision_trace_hash(simulation.ai_decision_trace());
  const auto decision_audit =
      audit_decisions(simulation.ai_decision_trace(), simulation.command_trace());
  report.invalid_ai_decisions = decision_audit.invalid_records;
  report.unresolved_ai_decisions = decision_audit.unresolved_records;
  report.unlinked_ai_commands = decision_audit.unlinked_commands;

  const auto ai_trace_present = std::ranges::any_of(
      simulation.command_trace(), [](const core::CommandTraceEntry& entry) {
        return entry.source == core::CommandSource::CommanderAI;
      });
  const auto ai_trace_auditable =
      ai_trace_present &&
      std::ranges::all_of(simulation.command_trace(), [](const core::CommandTraceEntry& entry) {
        return entry.source != core::CommandSource::CommanderAI || entry.observation_hash != 0;
      });
  add_scenario_check(report, "auditable_commander_trace", ai_trace_auditable,
                     "Every commander action must retain a nonzero observation hash.");
  add_scenario_check(report, "auditable_ai_decisions",
                     !simulation.ai_decision_trace().empty() &&
                         report.invalid_ai_decisions == 0 && report.unlinked_ai_commands == 0,
                     "Every AI choice must preserve valid utility scores and "
                     "its command-result link.");

  if (tactical_exit_scenario) {
    const auto decision = std::ranges::find_if(
        simulation.ai_decision_trace(),
        [](const core::AIDecisionRecord& record) {
          return record.layer == core::AIDecisionLayer::Tactical;
        });
    const auto has_influence_evidence =
        decision != simulation.ai_decision_trace().end() &&
        std::ranges::all_of(decision->candidates,
                            [](const core::AICandidateScore& candidate) {
                              return candidate.influence_map_hash != 0 &&
                                     candidate.influence_sample.has_value();
                            });
    add_scenario_check(report, "tactical_influence_evidence",
                       has_influence_evidence,
                       "Every tactical candidate must retain its deterministic "
                       "map hash and target cell.");

    if (decision != simulation.ai_decision_trace().end()) {
      const auto& command_target = decision->command.target;
      if (scenario.id == FixedScenarioId::TacticalFlankChoice) {
        add_scenario_check(report, "safer_flank_selected",
                           decision->selected_action ==
                                   core::AIAction::EngageForce &&
                               command_target.y > core::world(0, 400).y,
                           "The engagement must flank below the "
                           "turret-defended upper approach.");
      } else if (scenario.id == FixedScenarioId::TacticalDangerAvoidance) {
        const auto observation = simulation.observe(core::PlayerId::One);
        const auto threat_observed = std::ranges::any_of(
            observation.known_enemies(), [](const core::ObservedEnemy& enemy) {
              return enemy.currently_visible &&
                     enemy.type == core::EntityType::Turret;
            });
        add_scenario_check(
            report, "static_threat_observed", threat_observed,
            "The fixture must reveal its turret through ordinary faction vision.");
        add_scenario_check(
            report, "static_danger_avoided",
            threat_observed &&
                decision->selected_action == core::AIAction::CaptureObjective &&
                std::abs(command_target.y - core::world(0, 400).y) >=
                    core::kAIInfluenceCellSize / 2,
            "The objective approach must leave the defended center line; "
            "selected target=(" +
                std::to_string(command_target.x) + "," +
                std::to_string(command_target.y) + ").");
      } else if (scenario.id == FixedScenarioId::TacticalReinforcement) {
        add_scenario_check(report, "reinforcement_staged_safely",
                           decision->selected_action ==
                                   core::AIAction::ReinforceFront &&
                               command_target.y > core::world(0, 400).y,
                           "Reinforcements must stage on the protected side of "
                           "the active front.");
      } else if (scenario.id == FixedScenarioId::FactionLossTolerance) {
        const auto expected =
            scenario.faction == core::FactionId::Ascendancy
                ? core::AIAction::EngageForce
                : core::AIAction::Retreat;
        const auto doctrine = core::ai_doctrine_profile(
            scenario.faction, seed, core::PlayerId::One);
        add_scenario_check(
            report, "faction_loss_tolerance",
            decision->selected_action == expected,
            "The faction must apply its own acceptable-loss threshold to "
            "the same disadvantaged fight.");
        add_scenario_check(
            report, "faction_doctrine_fingerprint",
            decision->doctrine_faction == scenario.faction &&
                decision->temperament == doctrine.temperament &&
                decision->doctrine_hash == core::ai_doctrine_hash(doctrine),
            "The tactical choice must retain the deterministic doctrine "
            "that produced it.");
      } else {
        const auto accepted_retreat = std::ranges::any_of(
            simulation.command_trace(),
            [&](const core::CommandTraceEntry& entry) {
              return entry.source == core::CommandSource::CommanderAI &&
                     entry.accepted &&
                     entry.command.type == core::CommandType::Retreat &&
                     entry.command.target == command_target;
            });
        add_scenario_check(
            report, "retreat_destination_applied",
            decision->selected_action == core::AIAction::Retreat &&
                command_target != core::Vec2{} && accepted_retreat,
            "The influence-selected shelter must reach the authoritative "
            "retreat order.");
      }
    }
  }

  if (scenario.id == FixedScenarioId::EconomyDeficitRecovery) {
    add_scenario_check(report, "production_recovered",
                       owned_entity_count(simulation, core::PlayerId::One,
                                          core::EntityType::Barracks) > 0,
                       "The commander must gather out of a 40-ore deficit and "
                       "complete a production building.");
    add_scenario_check(report, "worker_target_recovered", fifth_worker_observed,
                       "The commander must reach its five-worker opening "
                       "target after the deficit.");
    add_scenario_check(report, "reinforcement_recovered",
                       first_reinforcement_tick.has_value(),
                       "The recovered economy must produce at least one combat "
                       "reinforcement.");
  } else if (scenario.id == FixedScenarioId::BlockedOpeningRecovery) {
    const auto expected_rejection = std::ranges::any_of(
        simulation.command_trace(), [](const core::CommandTraceEntry& entry) {
          return entry.source == core::CommandSource::CommanderAI && !entry.accepted &&
                 entry.command.type == core::CommandType::Build &&
                 entry.error == core::CommandError::PlacementBlocked;
        });
    const auto accepted_recovery = std::ranges::any_of(
        simulation.command_trace(), [](const core::CommandTraceEntry& entry) {
          return entry.source == core::CommandSource::CommanderAI && entry.accepted &&
                 entry.command.type == core::CommandType::Build;
        });
    add_scenario_check(report, "blocked_site_detected", expected_rejection,
                       "The fixture must reject the commander's preferred opening site.");
    add_scenario_check(
        report, "alternate_site_completed",
        accepted_recovery &&
            owned_entity_count(simulation, core::PlayerId::One, core::EntityType::Barracks) > 0,
                       "The commander must retry at an alternate site and "
                       "complete the barracks.");
    add_scenario_check(report, "blocked_opening_reinforced", first_reinforcement_tick.has_value(),
                       "The recovered opening must still produce a combat reinforcement.");
  } else if (scenario.id == FixedScenarioId::EarlyRushSurvival) {
    const auto command_survived =
        owned_entity_count(simulation, core::PlayerId::One, core::EntityType::Command) > 0;
    const auto rush_applied = std::ranges::any_of(
        simulation.command_trace(), [](const core::CommandTraceEntry& entry) {
          return entry.source == core::CommandSource::External && entry.accepted &&
                 entry.command.type == core::CommandType::AttackMove;
        });
    add_scenario_check(report, "rush_order_applied", rush_applied,
                       "The scripted early attack must enter the authoritative command path.");
    add_scenario_check(report, "rush_established_contact", first_contact_tick.has_value(),
                       "The commander must observe the incoming rush through normal vision.");
    add_scenario_check(report, "command_survived_rush", command_survived,
                       "The command structure must survive the bounded early rush.");
    add_scenario_check(report, "rush_reinforcement_produced", first_reinforcement_tick.has_value(),
                       "The pressured opening must produce at least one combat "
                       "reinforcement.");
  }

  report.passed = std::ranges::all_of(report.checks, &ScenarioCheckReport::passed);
  return ScenarioExecution{std::move(report), simulation.command_trace(),
                           simulation.ai_decision_trace()};
}

[[nodiscard]] MatchExecution execute_match(const BenchmarkCase& benchmark_case,
                                           const std::uint64_t seed,
                                           const core::Tick maximum_ticks,
                                           const core::Tick checkpoint_interval) {
  core::SimulationConfig config{};
  config.mode = core::MatchMode::Skirmish;
  config.player_one_faction = benchmark_case.left_faction;
  config.player_two_faction = benchmark_case.right_faction;
  config.commander_players = {true, true};
  config.match_seed = seed;

  core::Simulation simulation{config};
  MatchReport report{};
  report.scenario = benchmark_case.name;
  report.seed = seed;
  report.maximum_ticks = maximum_ticks;
  report.players = {
      PlayerMatchReport{.player = core::PlayerId::One,
                        .faction = benchmark_case.left_faction,
                        .difficulty = config.commander_difficulties[0],
                        .spawn = SpawnSide::Left},
      PlayerMatchReport{.player = core::PlayerId::Two,
                        .faction = benchmark_case.right_faction,
                        .difficulty = config.commander_difficulties[1],
                        .spawn = SpawnSide::Right},
  };

  std::array<std::size_t, 2> initial_army_counts{};
  for (const auto player : {core::PlayerId::One, core::PlayerId::Two}) {
    const auto observation = simulation.observe(player);
    const auto index = core::player_index(player);
    initial_army_counts[index] = army_count(observation);
    report.players[index].peak_ore = observation.self().ore;
    report.players[index].peak_army_value = army_value(observation);
  }

  std::size_t trace_cursor = 0;
  std::vector<PendingRetreat> pending_retreats;
  while (simulation.status() == core::MatchStatus::Playing && simulation.tick() < maximum_ticks) {
    simulation.step();
    process_trace(simulation, trace_cursor, pending_retreats, report.players);

    for (const auto player : {core::PlayerId::One, core::PlayerId::Two}) {
      const auto index = core::player_index(player);
      update_player_metrics(simulation.observe(player), initial_army_counts[index],
                            report.players[index]);
    }

    auto retreat = pending_retreats.begin();
    while (retreat != pending_retreats.end()) {
      if (retreat->evaluation_tick <= simulation.tick()) {
        evaluate_retreat(simulation, *retreat, report.players);
        retreat = pending_retreats.erase(retreat);
      } else {
        ++retreat;
      }
    }

    if (checkpoint_interval != 0 && simulation.tick() % checkpoint_interval == 0) {
      add_checkpoint(simulation, report);
    }
  }

  process_trace(simulation, trace_cursor, pending_retreats, report.players);
  for (const auto& retreat : pending_retreats) {
    evaluate_retreat(simulation, retreat, report.players);
  }
  if (report.checkpoints.empty() || report.checkpoints.back().tick != simulation.tick()) {
    add_checkpoint(simulation, report);
  }

  report.duration_ticks = simulation.tick();
  report.timed_out = simulation.status() == core::MatchStatus::Playing;
  report.winner = simulation.winner();
  report.final_state_hash = simulation.state_hash();
  report.command_trace_hash = command_trace_hash(simulation.command_trace());
  report.ai_decision_trace_hash = ai_decision_trace_hash(simulation.ai_decision_trace());
  const auto decision_audit =
      audit_decisions(simulation.ai_decision_trace(), simulation.command_trace());
  report.invalid_ai_decisions = decision_audit.invalid_records;
  report.unresolved_ai_decisions = decision_audit.unresolved_records;
  report.unlinked_ai_commands = decision_audit.unlinked_commands;
  record_decision_metrics(simulation.ai_decision_trace(), report.players);
  for (const auto player : {core::PlayerId::One, core::PlayerId::Two}) {
    const auto index = core::player_index(player);
    const auto observation = simulation.observe(player);
    auto& player_report = report.players[index];
    player_report.final_army_value = army_value(observation);
    const auto decision_count =
        player_report.ai_decisions_accepted +
        player_report.ai_decisions_rejected +
        player_report.ai_decisions_unresolved;
    if (decision_count != 0) {
      player_report.average_ai_quality_basis_points =
          static_cast<std::uint32_t>(
              player_report.ai_quality_sum_basis_points / decision_count);
      player_report.average_knowledge_delay_ticks =
          static_cast<std::uint32_t>(
              player_report.knowledge_delay_sum_ticks / decision_count);
    }
    if (report.duration_ticks != 0) {
      player_report.commands_per_minute_milli =
          player_report.commands_issued * 60U *
          static_cast<std::uint64_t>(core::kTicksPerSecond) * 1'000U / report.duration_ticks;
    }
    if (player_report.retreat_units_evaluated != 0) {
      player_report.retreat_survival_basis_points = static_cast<std::uint32_t>(
          player_report.retreat_survivors * 10'000U / player_report.retreat_units_evaluated);
    }
    std::ranges::sort(player_report.rejection_reasons, {}, &CommandErrorCount::error);
  }

  const auto left_contact = report.players[0].first_contact;
  const auto right_contact = report.players[1].first_contact;
  if (left_contact.has_value() && right_contact.has_value()) {
    report.first_contact_tick = std::min(*left_contact, *right_contact);
  } else if (left_contact.has_value()) {
    report.first_contact_tick = left_contact;
  } else {
    report.first_contact_tick = right_contact;
  }
  return MatchExecution{std::move(report), simulation.command_trace(),
                        simulation.ai_decision_trace()};
}

}  // namespace

MatchReport run_match(const BenchmarkCase& benchmark_case, const std::uint64_t seed,
                      const core::Tick maximum_ticks, const core::Tick checkpoint_interval) {
  return execute_match(benchmark_case, seed, maximum_ticks, checkpoint_interval).report;
}

FixedScenarioReport run_fixed_scenario(const FixedScenarioCase& scenario,
                                       const std::uint64_t seed) {
  return execute_fixed_scenario(scenario, seed).report;
}

SuiteReport run_suite(const SuiteOptions& options) {
  SuiteReport report{};
  report.options = options;
  if (options.seed_count == 0) {
    report.hard_failures.push_back(
        HardFailure{"suite", options.first_seed, "invalid_seed_count",
                    "At least one deterministic seed is required."});
  }
  if (options.maximum_match_ticks == 0) {
    report.hard_failures.push_back(
        HardFailure{"suite", options.first_seed, "invalid_tick_budget",
                    "The match tick budget must be greater than zero."});
  }

  if (report.hard_failures.empty()) {
    report.matches.reserve(standard_cases().size() * options.seed_count);
    report.fixed_scenarios.reserve(standard_fixed_scenarios().size() * options.seed_count);
    for (std::uint32_t offset = 0; offset < options.seed_count; ++offset) {
      const auto seed = options.first_seed + offset;
      for (const auto& benchmark_case : standard_cases()) {
        auto execution = execute_match(benchmark_case, seed, options.maximum_match_ticks,
                                       options.checkpoint_interval);
        if (options.verify_determinism) {
          const auto replay = execute_match(benchmark_case, seed, options.maximum_match_ticks,
                                            options.checkpoint_interval);
          if (execution.report != replay.report || execution.trace != replay.trace ||
              execution.decision_trace != replay.decision_trace) {
            add_failure(report, execution.report, "nondeterministic_replay",
                        "Duplicate runs produced different telemetry, traces, "
                        "checkpoints, or outcomes.");
          }
        }
        audit_match(report, execution.report);
        report.matches.push_back(std::move(execution.report));
      }
      for (const auto& scenario : standard_fixed_scenarios()) {
        auto execution = execute_fixed_scenario(scenario, seed);
        if (options.verify_determinism) {
          const auto replay = execute_fixed_scenario(scenario, seed);
          if (execution.report != replay.report || execution.trace != replay.trace ||
              execution.decision_trace != replay.decision_trace) {
            report.hard_failures.push_back(HardFailure{
                execution.report.scenario, execution.report.seed, "nondeterministic_scenario_replay",
                "Duplicate fixed scenarios produced different checks, traces, "
                "or final state.",
            });
          }
        }
        for (const auto& check : execution.report.checks) {
          ++report.scenario_checks_total;
          if (check.passed) {
            ++report.scenario_checks_passed;
          } else {
            report.hard_failures.push_back(HardFailure{
                execution.report.scenario, execution.report.seed, "scenario_" + check.id,
                check.message,
            });
          }
        }
        report.fixed_scenarios.push_back(std::move(execution.report));
      }
    }
  }

  if (report.scenario_checks_total != 0) {
    report.scenario_pass_basis_points = static_cast<std::uint32_t>(
        report.scenario_checks_passed * 10'000U / report.scenario_checks_total);
  }

  report.win_rates = calculate_win_rates(report.matches);
  report.balance_alerts = calculate_balance_alerts(report.win_rates);
  report.suite_hash = 0;
  report.suite_hash = text_hash(to_json(report));
  return report;
}

std::string to_json(const SuiteReport& report) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << "{\n"
         << "  \"schema_version\": " << report.schema_version << ",\n"
         << "  \"suite_hash\": \"" << hex_hash(report.suite_hash) << "\",\n"
         << "  \"options\": {\n"
         << "    \"seed_count\": " << report.options.seed_count << ",\n"
         << "    \"first_seed\": " << report.options.first_seed << ",\n"
         << "    \"maximum_match_ticks\": " << report.options.maximum_match_ticks << ",\n"
         << "    \"checkpoint_interval\": " << report.options.checkpoint_interval << ",\n"
         << "    \"verify_determinism\": "
         << (report.options.verify_determinism ? "true" : "false") << "\n"
         << "  },\n"
         << "  \"matches\": [";
  if (!report.matches.empty()) {
    output << '\n';
    for (std::size_t index = 0; index < report.matches.size(); ++index) {
      write_match_json(output, report.matches[index], 4);
      output << (index + 1 == report.matches.size() ? "\n" : ",\n");
    }
    output << "  ";
  }
  output << "],\n"
         << "  \"fixed_scenarios\": [";
  if (!report.fixed_scenarios.empty()) {
    output << '\n';
    for (std::size_t index = 0; index < report.fixed_scenarios.size(); ++index) {
      write_fixed_scenario_json(output, report.fixed_scenarios[index], 4);
      output << (index + 1 == report.fixed_scenarios.size() ? "\n" : ",\n");
    }
    output << "  ";
  }
  output << "],\n"
         << "  \"scenario_summary\": {\n"
         << "    \"checks_passed\": " << report.scenario_checks_passed << ",\n"
         << "    \"checks_total\": " << report.scenario_checks_total << ",\n"
         << "    \"pass_basis_points\": " << report.scenario_pass_basis_points << "\n"
         << "  },\n"
         << "  \"win_rates\": [";
  if (!report.win_rates.empty()) {
    output << '\n';
    for (std::size_t index = 0; index < report.win_rates.size(); ++index) {
      const auto& rate = report.win_rates[index];
      output << "    {\"cohort\": \"" << json_escape(rate.cohort) << "\", \"games\": "
             << rate.games << ", \"wins\": " << rate.wins << ", \"losses\": " << rate.losses
             << ", \"draws\": " << rate.draws << ", \"win_rate_basis_points\": "
             << rate.win_rate_basis_points << '}';
      output << (index + 1 == report.win_rates.size() ? "\n" : ",\n");
    }
    output << "  ";
  }
  output << "],\n"
         << "  \"hard_failures\": [";
  if (!report.hard_failures.empty()) {
    output << '\n';
    for (std::size_t index = 0; index < report.hard_failures.size(); ++index) {
      const auto& failure = report.hard_failures[index];
      output << "    {\"scenario\": \"" << json_escape(failure.scenario) << "\", \"seed\": "
             << failure.seed << ", \"code\": \"" << json_escape(failure.code)
             << "\", \"message\": \"" << json_escape(failure.message) << "\"}";
      output << (index + 1 == report.hard_failures.size() ? "\n" : ",\n");
    }
    output << "  ";
  }
  output << "],\n"
         << "  \"balance_alerts\": [";
  if (!report.balance_alerts.empty()) {
    output << '\n';
    for (std::size_t index = 0; index < report.balance_alerts.size(); ++index) {
      const auto& alert = report.balance_alerts[index];
      output << "    {\"cohort\": \"" << json_escape(alert.cohort) << "\", \"message\": \""
             << json_escape(alert.message) << "\"}";
      output << (index + 1 == report.balance_alerts.size() ? "\n" : ",\n");
    }
    output << "  ";
  }
  output << "]\n}\n";
  return output.str();
}

}  // namespace ashen::benchmark
