#include "ashen/core/Snapshot.hpp"

#include "ashen/core/AIDifficulty.hpp"
#include "ashen/core/AIDoctrine.hpp"
#include "ashen/core/Catalog.hpp"
#include "ashen/core/Content.hpp"
#include "ashen/core/Scenario.hpp"
#include "ashen/core/SystemPipeline.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ashen::core {
namespace {

inline constexpr std::array<std::uint8_t, 8> kSnapshotMagic{
    'V', 'O', 'W', 'S', 'N', 'P', '0', '1'};
inline constexpr std::uint64_t kFnvOffset =
    14'695'981'039'346'656'037ULL;
inline constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;
inline constexpr std::uint64_t kMaximumPayloadBytes = 256ULL * 1'024 * 1'024;
inline constexpr std::uint32_t kMaximumCollectionElements = 1'000'000;

class Writer final {
 public:
  template <typename Value>
    requires(std::is_integral_v<Value> &&
             !std::is_same_v<std::remove_cv_t<Value>, bool>)
  void integral(const Value value) {
    using Unsigned = std::make_unsigned_t<Value>;
    const auto bits = [&] {
      if constexpr (std::is_signed_v<Value>) {
        return std::bit_cast<Unsigned>(value);
      } else {
        return static_cast<Unsigned>(value);
      }
    }();
    for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
      bytes_.push_back(
          static_cast<std::uint8_t>((bits >> (byte * 8U)) & 0xffU));
    }
  }

  template <typename Enum>
    requires std::is_enum_v<Enum>
  void enumeration(const Enum value) {
    integral(static_cast<std::underlying_type_t<Enum>>(value));
  }

  void boolean(const bool value) {
    integral(static_cast<std::uint8_t>(value ? 1U : 0U));
  }

  void count(const std::size_t value) {
    if (value > kMaximumCollectionElements) {
      throw std::length_error("Snapshot collection exceeds the V1 limit.");
    }
    integral(static_cast<std::uint32_t>(value));
  }

  void size(const std::size_t value) {
    integral(static_cast<std::uint64_t>(value));
  }

  void text(const std::string_view value) {
    count(value.size());
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  void append(const std::span<const std::uint8_t> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
    return bytes_;
  }

  [[nodiscard]] std::vector<std::uint8_t> release() && {
    return std::move(bytes_);
  }

 private:
  std::vector<std::uint8_t> bytes_{};
};

class Reader final {
 public:
  explicit Reader(const std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  template <typename Value>
    requires(std::is_integral_v<Value> &&
             !std::is_same_v<std::remove_cv_t<Value>, bool>)
  bool integral(Value& value) {
    if (!require(sizeof(Value))) {
      return false;
    }
    using Unsigned = std::make_unsigned_t<Value>;
    Unsigned bits{};
    for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
      bits |= static_cast<Unsigned>(bytes_[offset_++]) << (byte * 8U);
    }
    if constexpr (std::is_signed_v<Value>) {
      value = std::bit_cast<Value>(bits);
    } else {
      value = static_cast<Value>(bits);
    }
    return true;
  }

  template <typename Enum>
    requires std::is_enum_v<Enum>
  bool enumeration(Enum& value, const Enum maximum) {
    using Underlying = std::underlying_type_t<Enum>;
    Underlying raw{};
    if (!integral(raw)) {
      return false;
    }
    if constexpr (std::is_signed_v<Underlying>) {
      if (raw < 0) {
        fail(SnapshotError::InvalidData);
        return false;
      }
    }
    if (raw > static_cast<Underlying>(maximum)) {
      fail(SnapshotError::InvalidData);
      return false;
    }
    value = static_cast<Enum>(raw);
    return true;
  }

  bool boolean(bool& value) {
    std::uint8_t raw{};
    if (!integral(raw)) {
      return false;
    }
    if (raw > 1U) {
      fail(SnapshotError::InvalidData);
      return false;
    }
    value = raw != 0;
    return true;
  }

  bool count(std::size_t& value) {
    std::uint32_t raw{};
    if (!integral(raw)) {
      return false;
    }
    if (raw > kMaximumCollectionElements) {
      fail(SnapshotError::InvalidData);
      return false;
    }
    value = raw;
    return true;
  }

  bool size(std::size_t& value) {
    std::uint64_t raw{};
    if (!integral(raw)) {
      return false;
    }
    if (raw > std::numeric_limits<std::size_t>::max()) {
      fail(SnapshotError::InvalidData);
      return false;
    }
    value = static_cast<std::size_t>(raw);
    return true;
  }

  bool text(std::string_view& value) {
    std::size_t length{};
    if (!count(length) || !require(length)) {
      return false;
    }
    value = std::string_view{
        reinterpret_cast<const char*>(bytes_.data() + offset_), length};
    offset_ += length;
    return true;
  }

  [[nodiscard]] std::span<const std::uint8_t> take(
      const std::size_t count) {
    if (!require(count)) {
      return {};
    }
    const auto result = bytes_.subspan(offset_, count);
    offset_ += count;
    return result;
  }

  void fail(const SnapshotError error) noexcept {
    if (error_ == SnapshotError::None) {
      error_ = error;
    }
  }

  [[nodiscard]] bool ok() const noexcept {
    return error_ == SnapshotError::None;
  }

  [[nodiscard]] SnapshotError error() const noexcept { return error_; }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - offset_;
  }

 private:
  bool require(const std::size_t count) {
    if (count > remaining()) {
      fail(SnapshotError::Truncated);
      return false;
    }
    return true;
  }

  std::span<const std::uint8_t> bytes_{};
  std::size_t offset_{};
  SnapshotError error_{SnapshotError::None};
};

[[nodiscard]] std::uint64_t hash_bytes(
    const std::span<const std::uint8_t> bytes) noexcept {
  auto hash = kFnvOffset;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= kFnvPrime;
  }
  return hash;
}

void fold_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
  for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
    hash ^= value & 0xffU;
    hash *= kFnvPrime;
    value >>= 8U;
  }
}

void write_vec2(Writer& writer, const Vec2 value) {
  writer.integral(value.x);
  writer.integral(value.y);
}

bool read_vec2(Reader& reader, Vec2& value) {
  return reader.integral(value.x) && reader.integral(value.y);
}

template <typename Enum>
void write_optional_enum(Writer& writer, const std::optional<Enum> value) {
  writer.boolean(value.has_value());
  if (value.has_value()) {
    writer.enumeration(*value);
  }
}

template <typename Enum>
bool read_optional_enum(Reader& reader, std::optional<Enum>& value,
                        const Enum maximum) {
  bool present{};
  if (!reader.boolean(present)) {
    return false;
  }
  if (!present) {
    value.reset();
    return true;
  }
  Enum decoded{};
  if (!reader.enumeration(decoded, maximum)) {
    return false;
  }
  value = decoded;
  return true;
}

void write_order(Writer& writer, const Order& order) {
  writer.enumeration(order.type);
  write_vec2(writer, order.target);
  write_vec2(writer, order.secondary_target);
  writer.integral(order.target_entity.value);
  writer.integral(order.resource.value);
  writer.enumeration(order.gather_phase);
  writer.integral(order.phase_ticks);
  write_vec2(writer, order.route_goal);
  writer.count(order.route.size());
  for (const auto point : order.route) {
    write_vec2(writer, point);
  }
  writer.size(order.route_index);
}

bool read_order(Reader& reader, Order& order) {
  if (!reader.enumeration(order.type, OrderType::Hold) ||
      !read_vec2(reader, order.target) ||
      !read_vec2(reader, order.secondary_target) ||
      !reader.integral(order.target_entity.value) ||
      !reader.integral(order.resource.value) ||
      !reader.enumeration(order.gather_phase, GatherPhase::Return) ||
      !reader.integral(order.phase_ticks) ||
      !read_vec2(reader, order.route_goal)) {
    return false;
  }
  std::size_t count{};
  if (!reader.count(count)) {
    return false;
  }
  order.route.resize(count);
  for (auto& point : order.route) {
    if (!read_vec2(reader, point)) {
      return false;
    }
  }
  if (!reader.size(order.route_index) ||
      order.route_index > order.route.size()) {
    reader.fail(SnapshotError::InvalidData);
    return false;
  }
  return true;
}

void write_production_task(Writer& writer, const ProductionTask& task) {
  writer.enumeration(task.type);
  writer.integral(task.remaining_ticks);
  writer.integral(task.total_ticks);
}

bool read_production_task(Reader& reader, ProductionTask& task) {
  return reader.enumeration(task.type, EntityType::Turret) &&
         reader.integral(task.remaining_ticks) &&
         reader.integral(task.total_ticks);
}

void write_research_task(Writer& writer, const ResearchTask& task) {
  writer.enumeration(task.id);
  writer.integral(task.remaining_ticks);
  writer.integral(task.total_ticks);
}

bool read_research_task(Reader& reader, ResearchTask& task) {
  return reader.enumeration(task.id, ResearchId::SiegeLiturgy) &&
         reader.integral(task.remaining_ticks) &&
         reader.integral(task.total_ticks);
}

void write_player_state(Writer& writer, const PlayerState& player) {
  writer.enumeration(player.id);
  writer.enumeration(player.faction);
  writer.integral(player.ore);
  writer.integral(player.supply_used);
  writer.integral(player.supply_cap);
  writer.integral(player.resolve);
  writer.integral(player.power_cooldown_ticks);
  writer.integral(player.tech_tier);
  for (const auto researched : player.researched) {
    writer.boolean(researched);
  }
  writer.count(player.research_queue.size());
  for (const auto& task : player.research_queue) {
    write_research_task(writer, task);
  }
}

bool read_player_state(Reader& reader, PlayerState& player) {
  if (!reader.enumeration(player.id, PlayerId::Two) ||
      !reader.enumeration(player.faction, FactionId::Concord) ||
      !reader.integral(player.ore) ||
      !reader.integral(player.supply_used) ||
      !reader.integral(player.supply_cap) ||
      !reader.integral(player.resolve) ||
      !reader.integral(player.power_cooldown_ticks) ||
      !reader.integral(player.tech_tier)) {
    return false;
  }
  for (auto& researched : player.researched) {
    if (!reader.boolean(researched)) {
      return false;
    }
  }
  std::size_t count{};
  if (!reader.count(count)) {
    return false;
  }
  player.research_queue.resize(count);
  for (auto& task : player.research_queue) {
    if (!read_research_task(reader, task)) {
      return false;
    }
  }
  return true;
}

void write_entity(Writer& writer, const Entity& entity) {
  writer.integral(entity.id.value);
  writer.enumeration(entity.owner);
  writer.enumeration(entity.faction);
  writer.enumeration(entity.type);
  writer.enumeration(entity.kind);
  write_vec2(writer, entity.position);
  writer.integral(entity.radius);
  writer.integral(entity.hit_points);
  writer.integral(entity.max_hit_points);
  writer.integral(entity.speed_per_tick);
  writer.integral(entity.attack_range);
  writer.integral(entity.damage);
  writer.integral(entity.attack_cooldown_ticks);
  writer.integral(entity.cooldown_ticks);
  writer.enumeration(entity.armor);
  writer.enumeration(entity.bonus_against);
  writer.boolean(entity.has_damage_bonus);
  writer.integral(entity.bonus_damage);
  writer.integral(entity.sight);
  writer.integral(entity.terror);
  writer.integral(entity.ward);
  writer.integral(entity.resolve);
  writer.enumeration(entity.resolve_state);
  writer.integral(entity.supply_cost);
  writer.integral(entity.supply_provided);
  writer.integral(entity.carrying);
  write_order(writer, entity.order);
  writer.count(entity.order_queue.size());
  for (const auto& order : entity.order_queue) {
    write_order(writer, order);
  }
  write_vec2(writer, entity.rally_point);
  writer.count(entity.production_queue.size());
  for (const auto& task : entity.production_queue) {
    write_production_task(writer, task);
  }
  writer.enumeration(entity.stance);
  write_vec2(writer, entity.guard_position);
  writer.boolean(entity.under_construction);
  writer.integral(entity.construction_ticks);
  writer.integral(entity.construction_total_ticks);
}

bool read_entity(Reader& reader, Entity& entity) {
  if (!reader.integral(entity.id.value) ||
      !reader.enumeration(entity.owner, PlayerId::Two) ||
      !reader.enumeration(entity.faction, FactionId::Concord) ||
      !reader.enumeration(entity.type, EntityType::Turret) ||
      !reader.enumeration(entity.kind, EntityKind::Building) ||
      !read_vec2(reader, entity.position) ||
      !reader.integral(entity.radius) ||
      !reader.integral(entity.hit_points) ||
      !reader.integral(entity.max_hit_points) ||
      !reader.integral(entity.speed_per_tick) ||
      !reader.integral(entity.attack_range) ||
      !reader.integral(entity.damage) ||
      !reader.integral(entity.attack_cooldown_ticks) ||
      !reader.integral(entity.cooldown_ticks) ||
      !reader.enumeration(entity.armor, ArmorClass::Structure) ||
      !reader.enumeration(entity.bonus_against, ArmorClass::Structure) ||
      !reader.boolean(entity.has_damage_bonus) ||
      !reader.integral(entity.bonus_damage) ||
      !reader.integral(entity.sight) ||
      !reader.integral(entity.terror) ||
      !reader.integral(entity.ward) ||
      !reader.integral(entity.resolve) ||
      !reader.enumeration(entity.resolve_state, ResolveState::Rallied) ||
      !reader.integral(entity.supply_cost) ||
      !reader.integral(entity.supply_provided) ||
      !reader.integral(entity.carrying) ||
      !read_order(reader, entity.order)) {
    return false;
  }
  std::size_t order_count{};
  if (!reader.count(order_count)) {
    return false;
  }
  entity.order_queue.resize(order_count);
  for (auto& order : entity.order_queue) {
    if (!read_order(reader, order)) {
      return false;
    }
  }
  if (!read_vec2(reader, entity.rally_point)) {
    return false;
  }
  std::size_t production_count{};
  if (!reader.count(production_count)) {
    return false;
  }
  entity.production_queue.resize(production_count);
  for (auto& task : entity.production_queue) {
    if (!read_production_task(reader, task)) {
      return false;
    }
  }
  return reader.enumeration(entity.stance, UnitStance::Hold) &&
         read_vec2(reader, entity.guard_position) &&
         reader.boolean(entity.under_construction) &&
         reader.integral(entity.construction_ticks) &&
         reader.integral(entity.construction_total_ticks);
}

void write_resource(Writer& writer, const ResourceNode& resource) {
  writer.integral(resource.id.value);
  write_vec2(writer, resource.position);
  writer.integral(resource.radius);
  writer.integral(resource.amount);
}

bool read_resource(Reader& reader, ResourceNode& resource) {
  return reader.integral(resource.id.value) &&
         read_vec2(reader, resource.position) &&
         reader.integral(resource.radius) &&
         reader.integral(resource.amount);
}

void write_control_point(Writer& writer, const ControlPoint& point) {
  writer.integral(point.id.value);
  write_vec2(writer, point.position);
  writer.integral(point.radius);
  write_optional_enum(writer, point.owner);
  writer.integral(point.influence);
  writer.integral(point.income_progress);
  writer.boolean(point.contested);
}

bool read_control_point(Reader& reader, ControlPoint& point) {
  return reader.integral(point.id.value) &&
         read_vec2(reader, point.position) &&
         reader.integral(point.radius) &&
         read_optional_enum(reader, point.owner, PlayerId::Two) &&
         reader.integral(point.influence) &&
         reader.integral(point.income_progress) &&
         reader.boolean(point.contested);
}

void write_navigation_obstacle(Writer& writer,
                               const NavigationObstacle& obstacle) {
  write_vec2(writer, obstacle.minimum);
  write_vec2(writer, obstacle.maximum);
}

bool read_navigation_obstacle(Reader& reader,
                              NavigationObstacle& obstacle) {
  return read_vec2(reader, obstacle.minimum) &&
         read_vec2(reader, obstacle.maximum);
}

void write_config(Writer& writer, const SimulationConfig& config) {
  writer.enumeration(config.mode);
  writer.enumeration(config.story_mission);
  writer.enumeration(config.player_one_faction);
  writer.enumeration(config.player_two_faction);
  for (const auto enabled : config.commander_players) {
    writer.boolean(enabled);
  }
  for (const auto difficulty : config.commander_difficulties) {
    writer.enumeration(difficulty);
  }
  for (const auto ore : config.starting_ore) {
    writer.integral(ore);
  }
  writer.integral(config.match_seed);
  write_vec2(writer, config.map_size);
  writer.integral(config.visibility_cell_size);
  writer.integral(config.navigation_cell_size);
  writer.integral(config.spatial_cell_size);
  writer.count(config.navigation_obstacles.size());
  for (const auto& obstacle : config.navigation_obstacles) {
    write_navigation_obstacle(writer, obstacle);
  }
  writer.boolean(config.seed_starting_forces);
}

bool read_config(Reader& reader, SimulationConfig& config) {
  if (!reader.enumeration(config.mode, MatchMode::PvP) ||
      !reader.enumeration(config.story_mission,
                          StoryMissionId::NamesAtTheWater) ||
      !reader.enumeration(config.player_one_faction, FactionId::Concord) ||
      !reader.enumeration(config.player_two_faction, FactionId::Concord)) {
    return false;
  }
  for (auto& enabled : config.commander_players) {
    if (!reader.boolean(enabled)) {
      return false;
    }
  }
  for (auto& difficulty : config.commander_difficulties) {
    if (!reader.enumeration(difficulty, AIDifficulty::Competitive)) {
      return false;
    }
  }
  for (auto& ore : config.starting_ore) {
    if (!reader.integral(ore)) {
      return false;
    }
  }
  if (!reader.integral(config.match_seed) ||
      !read_vec2(reader, config.map_size) ||
      !reader.integral(config.visibility_cell_size) ||
      !reader.integral(config.navigation_cell_size) ||
      !reader.integral(config.spatial_cell_size)) {
    return false;
  }
  std::size_t obstacle_count{};
  if (!reader.count(obstacle_count)) {
    return false;
  }
  config.navigation_obstacles.resize(obstacle_count);
  for (auto& obstacle : config.navigation_obstacles) {
    if (!read_navigation_obstacle(reader, obstacle)) {
      return false;
    }
  }
  return reader.boolean(config.seed_starting_forces);
}

void write_command(Writer& writer, const Command& command) {
  writer.integral(command.execute_tick);
  writer.integral(command.sequence);
  writer.enumeration(command.player);
  writer.enumeration(command.type);
  writer.count(command.entities.size());
  for (const auto id : command.entities) {
    writer.integral(id.value);
  }
  write_vec2(writer, command.target);
  writer.integral(command.target_entity.value);
  writer.integral(command.resource.value);
  writer.integral(command.producer.value);
  writer.enumeration(command.train_type);
  writer.enumeration(command.building_type);
  writer.enumeration(command.research);
  writer.enumeration(command.stance);
  writer.integral(command.vow.value);
  writer.boolean(command.queue);
}

bool read_command(Reader& reader, Command& command) {
  if (!reader.integral(command.execute_tick) ||
      !reader.integral(command.sequence) ||
      !reader.enumeration(command.player, PlayerId::Two) ||
      !reader.enumeration(command.type, CommandType::AmendVow)) {
    return false;
  }
  std::size_t entity_count{};
  if (!reader.count(entity_count)) {
    return false;
  }
  command.entities.resize(entity_count);
  for (auto& id : command.entities) {
    if (!reader.integral(id.value)) {
      return false;
    }
  }
  return read_vec2(reader, command.target) &&
         reader.integral(command.target_entity.value) &&
         reader.integral(command.resource.value) &&
         reader.integral(command.producer.value) &&
         reader.enumeration(command.train_type, EntityType::Turret) &&
         reader.enumeration(command.building_type, EntityType::Turret) &&
         reader.enumeration(command.research, ResearchId::SiegeLiturgy) &&
         reader.enumeration(command.stance, UnitStance::Hold) &&
         reader.integral(command.vow.value) &&
         reader.boolean(command.queue);
}

void write_command_trace(Writer& writer, const CommandTraceEntry& trace) {
  writer.integral(trace.issued_tick);
  writer.integral(trace.applied_tick);
  writer.enumeration(trace.source);
  writer.integral(trace.observation_hash);
  writer.integral(trace.ai_decision_id);
  write_command(writer, trace.command);
  writer.boolean(trace.accepted);
  writer.enumeration(trace.error);
}

bool read_command_trace(Reader& reader, CommandTraceEntry& trace) {
  return reader.integral(trace.issued_tick) &&
         reader.integral(trace.applied_tick) &&
         reader.enumeration(trace.source, CommandSource::CommanderAI) &&
         reader.integral(trace.observation_hash) &&
         reader.integral(trace.ai_decision_id) &&
         read_command(reader, trace.command) &&
         reader.boolean(trace.accepted) &&
         reader.enumeration(trace.error, CommandError::VowAuthorityRequired);
}

void write_vow(Writer& writer, const VowState& vow) {
  writer.integral(vow.id.value);
  writer.enumeration(vow.maker);
  writer.enumeration(vow.resolution);
  writer.integral(vow.made_tick);
  writer.integral(vow.resolved_tick);
  writer.integral(vow.revision);
  write_optional_enum(writer, vow.participating_affected_player);
}

bool read_vow(Reader& reader, VowState& vow) {
  return reader.integral(vow.id.value) &&
         reader.enumeration(vow.maker, PlayerId::Two) &&
         reader.enumeration(vow.resolution, VowResolution::Amended) &&
         reader.integral(vow.made_tick) &&
         reader.integral(vow.resolved_tick) &&
         reader.integral(vow.revision) &&
         read_optional_enum(reader, vow.participating_affected_player,
                            PlayerId::Two);
}

void write_observed_enemy(Writer& writer, const ObservedEnemy& enemy) {
  writer.integral(enemy.id.value);
  writer.enumeration(enemy.owner);
  writer.enumeration(enemy.faction);
  writer.enumeration(enemy.type);
  writer.enumeration(enemy.kind);
  write_vec2(writer, enemy.position);
  writer.integral(enemy.radius);
  writer.integral(enemy.hit_points);
  writer.integral(enemy.max_hit_points);
  writer.integral(enemy.resolve);
  writer.boolean(enemy.under_construction);
  writer.boolean(enemy.currently_visible);
  writer.integral(enemy.last_observed_tick);
}

bool read_observed_enemy(Reader& reader, ObservedEnemy& enemy) {
  return reader.integral(enemy.id.value) &&
         reader.enumeration(enemy.owner, PlayerId::Two) &&
         reader.enumeration(enemy.faction, FactionId::Concord) &&
         reader.enumeration(enemy.type, EntityType::Turret) &&
         reader.enumeration(enemy.kind, EntityKind::Building) &&
         read_vec2(reader, enemy.position) &&
         reader.integral(enemy.radius) &&
         reader.integral(enemy.hit_points) &&
         reader.integral(enemy.max_hit_points) &&
         reader.integral(enemy.resolve) &&
         reader.boolean(enemy.under_construction) &&
         reader.boolean(enemy.currently_visible) &&
         reader.integral(enemy.last_observed_tick);
}

void write_observed_resource(Writer& writer,
                             const ObservedResource& resource) {
  writer.integral(resource.id.value);
  write_vec2(writer, resource.position);
  writer.integral(resource.radius);
  writer.integral(resource.last_observed_amount);
  writer.enumeration(resource.visibility);
  writer.integral(resource.last_observed_tick);
}

bool read_observed_resource(Reader& reader, ObservedResource& resource) {
  return reader.integral(resource.id.value) &&
         read_vec2(reader, resource.position) &&
         reader.integral(resource.radius) &&
         reader.integral(resource.last_observed_amount) &&
         reader.enumeration(resource.visibility, VisibilityState::Visible) &&
         reader.integral(resource.last_observed_tick);
}

void write_observed_control_point(
    Writer& writer, const ObservedControlPoint& point) {
  writer.integral(point.id.value);
  write_vec2(writer, point.position);
  writer.integral(point.radius);
  writer.enumeration(point.visibility);
  writer.boolean(point.has_observed_state);
  write_optional_enum(writer, point.last_observed_owner);
  writer.integral(point.last_observed_influence);
  writer.integral(point.last_observed_tick);
}

bool read_observed_control_point(Reader& reader,
                                 ObservedControlPoint& point) {
  return reader.integral(point.id.value) &&
         read_vec2(reader, point.position) &&
         reader.integral(point.radius) &&
         reader.enumeration(point.visibility, VisibilityState::Visible) &&
         reader.boolean(point.has_observed_state) &&
         read_optional_enum(reader, point.last_observed_owner,
                            PlayerId::Two) &&
         reader.integral(point.last_observed_influence) &&
         reader.integral(point.last_observed_tick);
}

void write_capability(Writer& writer, const CommandCapability& capability) {
  writer.enumeration(capability.type);
  writer.integral(capability.actor.value);
  write_optional_enum(writer, capability.entity_type);
  write_optional_enum(writer, capability.research);
}

bool read_capability(Reader& reader, CommandCapability& capability) {
  return reader.enumeration(capability.type, CommandType::AmendVow) &&
         reader.integral(capability.actor.value) &&
         read_optional_enum(reader, capability.entity_type,
                            EntityType::Turret) &&
         read_optional_enum(reader, capability.research,
                            ResearchId::SiegeLiturgy);
}

void write_ai_influence_cell(Writer& writer,
                             const AIInfluenceCell& cell) {
  writer.integral(cell.friendly_power);
  writer.integral(cell.observed_enemy_power);
  writer.integral(cell.static_danger);
  writer.integral(cell.objective_value);
  writer.integral(cell.travel_cost);
  writer.integral(cell.terror_pressure);
  writer.integral(cell.friendly_terror);
  writer.integral(cell.friendly_ward);
  writer.integral(cell.resolve_vulnerability);
  writer.integral(cell.uncertainty);
  writer.boolean(cell.navigable);
}

bool read_ai_influence_cell(Reader& reader, AIInfluenceCell& cell) {
  return reader.integral(cell.friendly_power) &&
         reader.integral(cell.observed_enemy_power) &&
         reader.integral(cell.static_danger) &&
         reader.integral(cell.objective_value) &&
         reader.integral(cell.travel_cost) &&
         reader.integral(cell.terror_pressure) &&
         reader.integral(cell.friendly_terror) &&
         reader.integral(cell.friendly_ward) &&
         reader.integral(cell.resolve_vulnerability) &&
         reader.integral(cell.uncertainty) &&
         reader.boolean(cell.navigable);
}

void write_ai_influence_sample(Writer& writer,
                               const AIInfluenceSample& sample) {
  writer.integral(sample.column);
  writer.integral(sample.row);
  write_vec2(writer, sample.center);
  write_ai_influence_cell(writer, sample.cell);
}

bool read_ai_influence_sample(Reader& reader,
                              AIInfluenceSample& sample) {
  return reader.integral(sample.column) &&
         reader.integral(sample.row) &&
         read_vec2(reader, sample.center) &&
         read_ai_influence_cell(reader, sample.cell);
}

void write_utility_component(Writer& writer,
                             const AIUtilityComponent& component) {
  writer.enumeration(component.reason);
  writer.integral(component.score);
}

bool read_utility_component(Reader& reader,
                            AIUtilityComponent& component) {
  return reader.enumeration(component.reason,
                            AIUtilityReason::CombatRecovery) &&
         reader.integral(component.score);
}

void write_candidate(Writer& writer, const AICandidateScore& candidate) {
  writer.enumeration(candidate.action);
  writer.integral(candidate.target_entity.value);
  writer.integral(candidate.target_objective.value);
  write_vec2(writer, candidate.target_position);
  write_optional_enum(writer, candidate.entity_type);
  write_optional_enum(writer, candidate.research);
  writer.integral(candidate.influence_map_hash);
  writer.boolean(candidate.influence_sample.has_value());
  if (candidate.influence_sample.has_value()) {
    write_ai_influence_sample(writer, *candidate.influence_sample);
  }
  writer.integral(candidate.total_score);
  writer.count(candidate.components.size());
  for (const auto& component : candidate.components) {
    write_utility_component(writer, component);
  }
}

bool read_candidate(Reader& reader, AICandidateScore& candidate) {
  if (!reader.enumeration(candidate.action, AIAction::RejoinFormation) ||
      !reader.integral(candidate.target_entity.value) ||
      !reader.integral(candidate.target_objective.value) ||
      !read_vec2(reader, candidate.target_position) ||
      !read_optional_enum(reader, candidate.entity_type,
                          EntityType::Turret) ||
      !read_optional_enum(reader, candidate.research,
                          ResearchId::SiegeLiturgy) ||
      !reader.integral(candidate.influence_map_hash)) {
    return false;
  }
  bool has_sample{};
  if (!reader.boolean(has_sample)) {
    return false;
  }
  if (has_sample) {
    AIInfluenceSample sample{};
    if (!read_ai_influence_sample(reader, sample)) {
      return false;
    }
    candidate.influence_sample = sample;
  } else {
    candidate.influence_sample.reset();
  }
  if (!reader.integral(candidate.total_score)) {
    return false;
  }
  std::size_t component_count{};
  if (!reader.count(component_count)) {
    return false;
  }
  candidate.components.resize(component_count);
  for (auto& component : candidate.components) {
    if (!read_utility_component(reader, component)) {
      return false;
    }
  }
  return true;
}

void write_ai_decision(Writer& writer, const AIDecisionRecord& decision) {
  writer.integral(decision.id);
  writer.integral(decision.observation_tick);
  writer.integral(decision.observation_hash);
  writer.enumeration(decision.player);
  writer.enumeration(decision.layer);
  writer.integral(decision.cadence_ticks);
  writer.enumeration(decision.difficulty);
  writer.integral(decision.difficulty_hash);
  writer.integral(decision.knowledge_tick);
  writer.enumeration(decision.doctrine_faction);
  writer.enumeration(decision.temperament);
  writer.integral(decision.doctrine_hash);
  writer.integral(decision.strategy_state_hash);
  writer.count(decision.candidates.size());
  for (const auto& candidate : decision.candidates) {
    write_candidate(writer, candidate);
  }
  writer.size(decision.selected_candidate);
  writer.size(decision.evaluated_candidates);
  writer.integral(decision.selected_quality_basis_points);
  writer.boolean(decision.mistake_applied);
  writer.enumeration(decision.selected_action);
  writer.enumeration(decision.winning_reason);
  write_vec2(writer, decision.command_precision_offset);
  writer.integral(decision.command_latency_ticks);
  write_command(writer, decision.command);
  writer.integral(decision.command_sequence);
  writer.integral(decision.applied_tick);
  writer.enumeration(decision.command_status);
  writer.enumeration(decision.command_error);
}

bool read_ai_decision(Reader& reader, AIDecisionRecord& decision) {
  if (!reader.integral(decision.id) ||
      !reader.integral(decision.observation_tick) ||
      !reader.integral(decision.observation_hash) ||
      !reader.enumeration(decision.player, PlayerId::Two) ||
      !reader.enumeration(decision.layer, AIDecisionLayer::Micro) ||
      !reader.integral(decision.cadence_ticks) ||
      !reader.enumeration(decision.difficulty,
                          AIDifficulty::Competitive) ||
      !reader.integral(decision.difficulty_hash) ||
      !reader.integral(decision.knowledge_tick) ||
      !reader.enumeration(decision.doctrine_faction,
                          FactionId::Concord) ||
      !reader.enumeration(decision.temperament,
                          AITemperament::Watchful) ||
      !reader.integral(decision.doctrine_hash) ||
      !reader.integral(decision.strategy_state_hash)) {
    return false;
  }
  std::size_t candidate_count{};
  if (!reader.count(candidate_count)) {
    return false;
  }
  decision.candidates.resize(candidate_count);
  for (auto& candidate : decision.candidates) {
    if (!read_candidate(reader, candidate)) {
      return false;
    }
  }
  if (!reader.size(decision.selected_candidate) ||
      !reader.size(decision.evaluated_candidates) ||
      !reader.integral(decision.selected_quality_basis_points) ||
      !reader.boolean(decision.mistake_applied) ||
      !reader.enumeration(decision.selected_action,
                          AIAction::RejoinFormation) ||
      !reader.enumeration(decision.winning_reason,
                          AIUtilityReason::CombatRecovery) ||
      !read_vec2(reader, decision.command_precision_offset) ||
      !reader.integral(decision.command_latency_ticks) ||
      !read_command(reader, decision.command) ||
      !reader.integral(decision.command_sequence) ||
      !reader.integral(decision.applied_tick) ||
      !reader.enumeration(decision.command_status,
                          AICommandStatus::Rejected) ||
      !reader.enumeration(decision.command_error,
                          CommandError::VowAuthorityRequired)) {
    return false;
  }
  if ((!decision.candidates.empty() &&
       decision.selected_candidate >= decision.candidates.size()) ||
      decision.evaluated_candidates > decision.candidates.size()) {
    reader.fail(SnapshotError::InvalidData);
    return false;
  }
  return true;
}

void write_strategy_state(Writer& writer, const AIStrategyState& state) {
  writer.enumeration(state.intention);
  writer.enumeration(state.opening);
  writer.integral(state.desired_workers);
  writer.integral(state.desired_vanguards);
  writer.integral(state.desired_skirmishers);
  writer.integral(state.timing_window_start);
  writer.integral(state.timing_window_end);
  writer.enumeration(state.preferred_route);
  writer.enumeration(state.known_opponent_behavior);
  writer.integral(state.confidence_basis_points);
  writer.integral(state.abort_conditions);
  writer.enumeration(state.contingency);
  writer.enumeration(state.evidence);
  writer.integral(state.last_updated_tick);
}

bool read_strategy_state(Reader& reader, AIStrategyState& state) {
  return reader.enumeration(state.intention,
                            AIStrategicIntention::RecoverForce) &&
         reader.enumeration(state.opening,
                            AIOpeningPlan::TreatyPosition) &&
         reader.integral(state.desired_workers) &&
         reader.integral(state.desired_vanguards) &&
         reader.integral(state.desired_skirmishers) &&
         reader.integral(state.timing_window_start) &&
         reader.integral(state.timing_window_end) &&
         reader.enumeration(state.preferred_route,
                            AIPreferredRoute::South) &&
         reader.enumeration(state.known_opponent_behavior,
                            AIKnownOpponentBehavior::Fortified) &&
         reader.integral(state.confidence_basis_points) &&
         reader.integral(state.abort_conditions) &&
         reader.enumeration(state.contingency,
                            AIContingency::Retreat) &&
         reader.enumeration(state.evidence,
                            AIStrategyEvidence::ObjectiveState) &&
         reader.integral(state.last_updated_tick);
}

void write_event(Writer& writer, const SimulationEvent& event) {
  const auto type = event_type(event);
  writer.integral(event.id.value);
  writer.integral(event.tick);
  writer.enumeration(type);
  switch (type) {
    case SimulationEventType::EntitySpawned: {
      const auto& payload = std::get<EntitySpawnedEvent>(event.payload);
      writer.integral(payload.entity.value);
      writer.enumeration(payload.owner);
      writer.enumeration(payload.faction);
      writer.enumeration(payload.archetype);
      break;
    }
    case SimulationEventType::EntityDestroyed: {
      const auto& payload = std::get<EntityDestroyedEvent>(event.payload);
      writer.integral(payload.entity.value);
      writer.enumeration(payload.owner);
      writer.enumeration(payload.faction);
      writer.enumeration(payload.archetype);
      break;
    }
    case SimulationEventType::UnitDamaged: {
      const auto& payload = std::get<UnitDamagedEvent>(event.payload);
      writer.integral(payload.source.value);
      writer.integral(payload.target.value);
      writer.integral(payload.amount);
      writer.integral(payload.remaining_hit_points);
      break;
    }
    case SimulationEventType::UnitWounded: {
      const auto& payload = std::get<UnitWoundedEvent>(event.payload);
      writer.integral(payload.entity.value);
      writer.integral(payload.source.value);
      writer.integral(payload.remaining_hit_points);
      break;
    }
    case SimulationEventType::UnitKilled: {
      const auto& payload = std::get<UnitKilledEvent>(event.payload);
      writer.integral(payload.entity.value);
      writer.integral(payload.killer.value);
      break;
    }
    case SimulationEventType::UnitRecovered: {
      const auto& payload = std::get<UnitRecoveredEvent>(event.payload);
      writer.integral(payload.entity.value);
      writer.integral(payload.recovery_source.value);
      break;
    }
    case SimulationEventType::FormationCreated: {
      const auto& payload = std::get<FormationCreatedEvent>(event.payload);
      writer.integral(payload.formation.value);
      writer.enumeration(payload.owner);
      break;
    }
    case SimulationEventType::FormationBroken: {
      const auto& payload = std::get<FormationBrokenEvent>(event.payload);
      writer.integral(payload.formation.value);
      break;
    }
    case SimulationEventType::ResolveThresholdChanged: {
      const auto& payload =
          std::get<ResolveThresholdChangedEvent>(event.payload);
      writer.integral(payload.entity.value);
      writer.enumeration(payload.previous);
      writer.enumeration(payload.current);
      writer.integral(payload.resolve);
      break;
    }
    case SimulationEventType::SupplyConnected: {
      const auto& payload = std::get<SupplyConnectedEvent>(event.payload);
      writer.integral(payload.entity.value);
      break;
    }
    case SimulationEventType::SupplyDisconnected: {
      const auto& payload =
          std::get<SupplyDisconnectedEvent>(event.payload);
      writer.integral(payload.entity.value);
      break;
    }
    case SimulationEventType::VowMade: {
      const auto& payload = std::get<VowMadeEvent>(event.payload);
      writer.integral(payload.vow.value);
      writer.enumeration(payload.maker);
      break;
    }
    case SimulationEventType::VowKept: {
      const auto& payload = std::get<VowKeptEvent>(event.payload);
      writer.integral(payload.vow.value);
      writer.enumeration(payload.maker);
      break;
    }
    case SimulationEventType::VowAmended: {
      const auto& payload = std::get<VowAmendedEvent>(event.payload);
      writer.integral(payload.vow.value);
      writer.enumeration(payload.maker);
      writer.enumeration(payload.participating_affected_player);
      writer.integral(payload.revision);
      break;
    }
    case SimulationEventType::VowBroken: {
      const auto& payload = std::get<VowBrokenEvent>(event.payload);
      writer.integral(payload.vow.value);
      writer.enumeration(payload.maker);
      break;
    }
    case SimulationEventType::TransformationStarted: {
      const auto& payload =
          std::get<TransformationStartedEvent>(event.payload);
      writer.integral(payload.transformation.value);
      writer.integral(payload.entity.value);
      writer.integral(payload.definition);
      break;
    }
    case SimulationEventType::TransformationCompleted: {
      const auto& payload =
          std::get<TransformationCompletedEvent>(event.payload);
      writer.integral(payload.transformation.value);
      writer.integral(payload.entity.value);
      writer.integral(payload.definition);
      break;
    }
    case SimulationEventType::TestimonyDiscovered: {
      const auto& payload =
          std::get<TestimonyDiscoveredEvent>(event.payload);
      writer.integral(payload.testimony);
      writer.enumeration(payload.discoverer);
      break;
    }
    case SimulationEventType::ObjectiveContested: {
      const auto& payload =
          std::get<ObjectiveContestedEvent>(event.payload);
      writer.integral(payload.objective.value);
      break;
    }
    case SimulationEventType::ObjectiveCaptured: {
      const auto& payload =
          std::get<ObjectiveCapturedEvent>(event.payload);
      writer.integral(payload.objective.value);
      write_optional_enum(writer, payload.previous_owner);
      writer.enumeration(payload.owner);
      break;
    }
    case SimulationEventType::ProjectileLaunched: {
      const auto& payload =
          std::get<ProjectileLaunchedEvent>(event.payload);
      writer.integral(payload.projectile);
      writer.integral(payload.source.value);
      writer.integral(payload.target.value);
      break;
    }
    case SimulationEventType::AbilityStarted: {
      const auto& payload = std::get<AbilityStartedEvent>(event.payload);
      writer.integral(payload.ability);
      writer.enumeration(payload.owner);
      writer.integral(payload.source.value);
      break;
    }
    case SimulationEventType::AbilityInterrupted: {
      const auto& payload =
          std::get<AbilityInterruptedEvent>(event.payload);
      writer.integral(payload.ability);
      writer.integral(payload.source.value);
      writer.integral(payload.interrupter.value);
      break;
    }
    case SimulationEventType::MissionObjectiveChanged: {
      const auto& payload =
          std::get<MissionObjectiveChangedEvent>(event.payload);
      writer.integral(payload.objective);
      writer.enumeration(payload.previous);
      writer.enumeration(payload.current);
      break;
    }
  }
}

bool read_event(Reader& reader, SimulationEvent& event) {
  SimulationEventType type{};
  if (!reader.integral(event.id.value) ||
      !reader.integral(event.tick) ||
      !reader.enumeration(type, SimulationEventType::MissionObjectiveChanged)) {
    return false;
  }
  switch (type) {
    case SimulationEventType::EntitySpawned: {
      EntitySpawnedEvent payload{};
      if (!reader.integral(payload.entity.value) ||
          !reader.enumeration(payload.owner, PlayerId::Two) ||
          !reader.enumeration(payload.faction, FactionId::Concord) ||
          !reader.enumeration(payload.archetype, EntityType::Turret)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::EntityDestroyed: {
      EntityDestroyedEvent payload{};
      if (!reader.integral(payload.entity.value) ||
          !reader.enumeration(payload.owner, PlayerId::Two) ||
          !reader.enumeration(payload.faction, FactionId::Concord) ||
          !reader.enumeration(payload.archetype, EntityType::Turret)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::UnitDamaged: {
      UnitDamagedEvent payload{};
      if (!reader.integral(payload.source.value) ||
          !reader.integral(payload.target.value) ||
          !reader.integral(payload.amount) ||
          !reader.integral(payload.remaining_hit_points)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::UnitWounded: {
      UnitWoundedEvent payload{};
      if (!reader.integral(payload.entity.value) ||
          !reader.integral(payload.source.value) ||
          !reader.integral(payload.remaining_hit_points)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::UnitKilled: {
      UnitKilledEvent payload{};
      if (!reader.integral(payload.entity.value) ||
          !reader.integral(payload.killer.value)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::UnitRecovered: {
      UnitRecoveredEvent payload{};
      if (!reader.integral(payload.entity.value) ||
          !reader.integral(payload.recovery_source.value)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::FormationCreated: {
      FormationCreatedEvent payload{};
      if (!reader.integral(payload.formation.value) ||
          !reader.enumeration(payload.owner, PlayerId::Two)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::FormationBroken: {
      FormationBrokenEvent payload{};
      if (!reader.integral(payload.formation.value)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::ResolveThresholdChanged: {
      ResolveThresholdChangedEvent payload{};
      if (!reader.integral(payload.entity.value) ||
          !reader.enumeration(payload.previous, ResolveState::Rallied) ||
          !reader.enumeration(payload.current, ResolveState::Rallied) ||
          !reader.integral(payload.resolve)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::SupplyConnected: {
      SupplyConnectedEvent payload{};
      if (!reader.integral(payload.entity.value)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::SupplyDisconnected: {
      SupplyDisconnectedEvent payload{};
      if (!reader.integral(payload.entity.value)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::VowMade: {
      VowMadeEvent payload{};
      if (!reader.integral(payload.vow.value) ||
          !reader.enumeration(payload.maker, PlayerId::Two)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::VowKept: {
      VowKeptEvent payload{};
      if (!reader.integral(payload.vow.value) ||
          !reader.enumeration(payload.maker, PlayerId::Two)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::VowAmended: {
      VowAmendedEvent payload{};
      if (!reader.integral(payload.vow.value) ||
          !reader.enumeration(payload.maker, PlayerId::Two) ||
          !reader.enumeration(payload.participating_affected_player,
                              PlayerId::Two) ||
          !reader.integral(payload.revision)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::VowBroken: {
      VowBrokenEvent payload{};
      if (!reader.integral(payload.vow.value) ||
          !reader.enumeration(payload.maker, PlayerId::Two)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::TransformationStarted: {
      TransformationStartedEvent payload{};
      if (!reader.integral(payload.transformation.value) ||
          !reader.integral(payload.entity.value) ||
          !reader.integral(payload.definition)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::TransformationCompleted: {
      TransformationCompletedEvent payload{};
      if (!reader.integral(payload.transformation.value) ||
          !reader.integral(payload.entity.value) ||
          !reader.integral(payload.definition)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::TestimonyDiscovered: {
      TestimonyDiscoveredEvent payload{};
      if (!reader.integral(payload.testimony) ||
          !reader.enumeration(payload.discoverer, PlayerId::Two)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::ObjectiveContested: {
      ObjectiveContestedEvent payload{};
      if (!reader.integral(payload.objective.value)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::ObjectiveCaptured: {
      ObjectiveCapturedEvent payload{};
      if (!reader.integral(payload.objective.value) ||
          !read_optional_enum(reader, payload.previous_owner,
                              PlayerId::Two) ||
          !reader.enumeration(payload.owner, PlayerId::Two)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::ProjectileLaunched: {
      ProjectileLaunchedEvent payload{};
      if (!reader.integral(payload.projectile) ||
          !reader.integral(payload.source.value) ||
          !reader.integral(payload.target.value)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::AbilityStarted: {
      AbilityStartedEvent payload{};
      if (!reader.integral(payload.ability) ||
          !reader.enumeration(payload.owner, PlayerId::Two) ||
          !reader.integral(payload.source.value)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::AbilityInterrupted: {
      AbilityInterruptedEvent payload{};
      if (!reader.integral(payload.ability) ||
          !reader.integral(payload.source.value) ||
          !reader.integral(payload.interrupter.value)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
    case SimulationEventType::MissionObjectiveChanged: {
      MissionObjectiveChangedEvent payload{};
      if (!reader.integral(payload.objective) ||
          !reader.enumeration(payload.previous,
                              MissionObjectiveStatus::Failed) ||
          !reader.enumeration(payload.current,
                              MissionObjectiveStatus::Failed)) {
        return false;
      }
      event.payload = payload;
      return true;
    }
  }
  reader.fail(SnapshotError::InvalidData);
  return false;
}

void write_metadata(Writer& writer, const DefinitionMetadata& metadata) {
  writer.integral(metadata.stable_id);
  writer.integral(metadata.version);
  writer.text(metadata.development_name);
  writer.text(metadata.localization_key);
  writer.text(metadata.presentation_key);
}

void write_content_registry(Writer& writer, const ContentRegistry& registry) {
  writer.count(registry.factions.size());
  for (const auto& definition : registry.factions) {
    write_metadata(writer, definition.metadata);
    writer.enumeration(definition.faction);
  }
  writer.count(registry.units.size());
  for (const auto& definition : registry.units) {
    write_metadata(writer, definition.metadata);
    writer.enumeration(definition.faction);
    writer.enumeration(definition.archetype);
    writer.integral(definition.cost);
    writer.integral(definition.build_ticks);
    writer.integral(definition.capabilities);
  }
  writer.count(registry.structures.size());
  for (const auto& definition : registry.structures) {
    write_metadata(writer, definition.metadata);
    writer.enumeration(definition.faction);
    writer.enumeration(definition.archetype);
    writer.integral(definition.cost);
    writer.integral(definition.build_ticks);
    writer.integral(definition.capabilities);
  }
  writer.count(registry.supply_nodes.size());
  for (const auto& definition : registry.supply_nodes) {
    write_metadata(writer, definition.metadata);
    writer.integral(definition.structure);
    writer.boolean(definition.source);
    writer.boolean(definition.relay);
    writer.integral(definition.link_range);
    writer.integral(definition.capacity);
    writer.integral(definition.demand);
  }
  writer.count(registry.abilities.size());
  for (const auto& definition : registry.abilities) {
    write_metadata(writer, definition.metadata);
    write_optional_enum(writer, definition.faction);
    writer.integral(definition.windup_ticks);
    writer.integral(definition.recovery_ticks);
    writer.boolean(definition.projectile.has_value());
    if (definition.projectile.has_value()) {
      writer.integral(*definition.projectile);
    }
    writer.integral(definition.required_capability);
  }
  writer.count(registry.projectiles.size());
  for (const auto& definition : registry.projectiles) {
    write_metadata(writer, definition.metadata);
    writer.integral(definition.speed_per_tick);
    writer.integral(definition.damage);
    writer.integral(definition.radius);
  }
  writer.count(registry.formations.size());
  for (const auto& definition : registry.formations) {
    write_metadata(writer, definition.metadata);
    write_optional_enum(writer, definition.faction);
    writer.integral(definition.minimum_members);
    writer.integral(definition.maximum_members);
  }
  writer.count(registry.research.size());
  for (const auto& definition : registry.research) {
    write_metadata(writer, definition.metadata);
    writer.enumeration(definition.research);
    write_optional_enum(writer, definition.faction);
    writer.integral(definition.cost);
    writer.integral(definition.duration_ticks);
    write_optional_enum(writer, definition.prerequisite);
  }
  writer.count(registry.powers.size());
  for (const auto& definition : registry.powers) {
    write_metadata(writer, definition.metadata);
    writer.enumeration(definition.faction);
    writer.integral(definition.ability);
    writer.integral(definition.cost);
    writer.integral(definition.cooldown_ticks);
  }
  writer.count(registry.transformations.size());
  for (const auto& definition : registry.transformations) {
    write_metadata(writer, definition.metadata);
    writer.enumeration(definition.faction);
    writer.enumeration(definition.source);
    writer.enumeration(definition.result);
    writer.integral(definition.permanent_cost);
    writer.integral(definition.duration_ticks);
    writer.boolean(definition.reversible);
  }
  writer.count(registry.vows.size());
  for (const auto& definition : registry.vows) {
    write_metadata(writer, definition.metadata);
    writer.integral(definition.vow.value);
    writer.boolean(definition.amendment_requires_affected_party);
    writer.text(definition.consequence_key);
  }
  writer.count(registry.ai_doctrines.size());
  for (const auto& definition : registry.ai_doctrines) {
    write_metadata(writer, definition.metadata);
    writer.enumeration(definition.faction);
  }
  writer.count(registry.ai_strategies.size());
  for (const auto& definition : registry.ai_strategies) {
    write_metadata(writer, definition.metadata);
    writer.enumeration(definition.faction);
    writer.integral(definition.desired_workers);
    writer.integral(definition.desired_vanguards);
    writer.integral(definition.desired_skirmishers);
  }
  writer.count(registry.objectives.size());
  for (const auto& definition : registry.objectives) {
    write_metadata(writer, definition.metadata);
    writer.boolean(definition.related_vow.has_value());
    if (definition.related_vow.has_value()) {
      writer.integral(definition.related_vow->value);
    }
    writer.integral(definition.capture_radius);
  }
}

void write_gameplay_catalog(Writer& writer) {
  constexpr std::array factions{
      FactionId::Compact, FactionId::Ascendancy, FactionId::Concord};
  constexpr std::array entity_types{
      EntityType::Worker, EntityType::Vanguard, EntityType::Skirmisher,
      EntityType::Command, EntityType::Barracks, EntityType::Turret};
  constexpr std::array research_ids{
      ResearchId::TierTwo, ResearchId::TemperedOaths,
      ResearchId::Wardcraft, ResearchId::ChorusOfKnives,
      ResearchId::PitBroods, ResearchId::VaultPlate,
      ResearchId::SiegeLiturgy};
  constexpr std::array difficulties{
      AIDifficulty::Story, AIDifficulty::Standard,
      AIDifficulty::Veteran, AIDifficulty::Competitive};

  writer.count(factions.size());
  for (const auto faction : factions) {
    const auto definition = faction_definition(faction);
    writer.enumeration(definition.id);
    writer.text(definition.name);
    writer.integral(definition.income_basis_points);
    writer.integral(definition.resolve_drift);
  }

  writer.count(factions.size() * entity_types.size());
  for (const auto faction : factions) {
    for (const auto entity_type : entity_types) {
      const auto definition = entity_definition(faction, entity_type);
      writer.enumeration(faction);
      writer.enumeration(definition.type);
      writer.enumeration(definition.kind);
      writer.text(definition.label);
      writer.integral(definition.cost);
      writer.integral(definition.build_ticks);
      writer.integral(definition.hit_points);
      writer.integral(definition.radius);
      writer.integral(definition.speed_per_tick);
      writer.integral(definition.attack_range);
      writer.integral(definition.damage);
      writer.integral(definition.attack_cooldown_ticks);
      writer.integral(definition.sight);
      writer.enumeration(definition.armor);
      writer.enumeration(definition.bonus_against);
      writer.boolean(definition.has_damage_bonus);
      writer.integral(definition.bonus_damage);
      writer.integral(definition.terror);
      writer.integral(definition.ward);
      writer.integral(definition.supply_cost);
      writer.integral(definition.supply_provided);
    }
  }

  writer.count(research_ids.size());
  for (const auto research : research_ids) {
    const auto definition = research_definition(research);
    writer.enumeration(definition.id);
    write_optional_enum(writer, definition.faction);
    writer.text(definition.label);
    writer.integral(definition.cost);
    writer.integral(definition.research_ticks);
    writer.enumeration(definition.producer);
    write_optional_enum(writer, definition.prerequisite);
  }

  writer.count(factions.size());
  for (const auto faction : factions) {
    const auto definition = power_definition(faction);
    writer.enumeration(definition.faction);
    writer.text(definition.label);
    writer.integral(definition.cost);
    writer.integral(definition.cooldown_ticks);
  }

  writer.count(entity_types.size() * entity_types.size());
  for (const auto producer : entity_types) {
    for (const auto unit : entity_types) {
      writer.enumeration(producer);
      writer.enumeration(unit);
      writer.boolean(can_train(producer, unit));
    }
  }

  writer.count(difficulties.size());
  for (const auto difficulty : difficulties) {
    const auto& profile = ai_difficulty_profile(difficulty);
    writer.enumeration(profile.difficulty);
    writer.integral(profile.reaction_delay_ticks);
    writer.integral(profile.strategic_cadence_ticks);
    writer.integral(profile.tactical_cadence_ticks);
    writer.integral(profile.tactical_phase_ticks);
    writer.integral(profile.micro_cadence_ticks);
    writer.integral(profile.command_latency_ticks);
    writer.integral(profile.command_precision_radius);
    writer.integral(profile.planning_horizon_cells);
    writer.integral(profile.mistake_rate_basis_points);
    writer.integral(profile.minimum_mistake_quality_basis_points);
    writer.integral(profile.mobile_memory_ticks);
    writer.size(profile.utility_search_breadth);
  }

  constexpr std::uint64_t doctrine_seed_samples = 32;
  writer.count(factions.size() * 2U * doctrine_seed_samples);
  for (const auto faction : factions) {
    for (const auto player : {PlayerId::One, PlayerId::Two}) {
      for (std::uint64_t seed = 0; seed < doctrine_seed_samples; ++seed) {
        writer.enumeration(faction);
        writer.enumeration(player);
        writer.integral(seed);
        writer.integral(
            ai_doctrine_hash(ai_doctrine_profile(faction, seed, player)));
      }
    }
  }
}

}  // namespace

class SnapshotCodec final {
 public:
  [[nodiscard]] static bool can_encode(
      const Simulation& simulation) noexcept {
    return validate(simulation);
  }

  static void write_payload(Writer& writer, const Simulation& simulation) {
    write_config(writer, simulation.config_);
    writer.integral(simulation.tick_);
    writer.enumeration(simulation.status_);
    write_optional_enum(writer, simulation.winner_);
    for (const auto& player : simulation.players_) {
      write_player_state(writer, player);
    }
    for (const auto seen : simulation.command_seen_) {
      writer.boolean(seen);
    }
    for (const auto& grid : simulation.visibility_) {
      write_visibility(writer, grid);
    }
    for (const auto& memories : simulation.resource_memory_) {
      writer.count(memories.size());
      for (const auto& memory : memories) {
        writer.boolean(memory.discovered);
        writer.integral(memory.amount);
        writer.integral(memory.observed_tick);
      }
    }
    for (const auto& memories : simulation.control_point_memory_) {
      writer.count(memories.size());
      for (const auto& memory : memories) {
        writer.boolean(memory.observed);
        write_optional_enum(writer, memory.owner);
        writer.integral(memory.influence);
        writer.integral(memory.observed_tick);
      }
    }
    for (const auto& memories : simulation.enemy_memory_) {
      writer.count(memories.size());
      for (const auto& memory : memories) {
        write_observed_enemy(writer, memory);
      }
    }
    for (const auto& commander : simulation.commanders_) {
      write_commander(writer, commander);
    }
    writer.count(simulation.entities_.size());
    for (const auto& entity : simulation.entities_) {
      write_entity(writer, entity);
    }
    writer.count(simulation.resources_.size());
    for (const auto& resource : simulation.resources_) {
      write_resource(writer, resource);
    }
    writer.count(simulation.control_points_.size());
    for (const auto& point : simulation.control_points_) {
      write_control_point(writer, point);
    }
    writer.count(simulation.vows_.size());
    for (const auto& vow : simulation.vows_) {
      write_vow(writer, vow);
    }
    writer.count(simulation.command_queue_.size());
    for (const auto& queued : simulation.command_queue_) {
      write_command(writer, queued.command);
      writer.integral(queued.issued_tick);
      writer.enumeration(queued.source);
      writer.integral(queued.observation_hash);
      writer.integral(queued.ai_decision_id);
    }
    writer.count(simulation.command_trace_.size());
    for (const auto& trace : simulation.command_trace_) {
      write_command_trace(writer, trace);
    }
    writer.count(simulation.ai_decision_trace_.size());
    for (const auto& decision : simulation.ai_decision_trace_) {
      write_ai_decision(writer, decision);
    }
    writer.count(simulation.events_.size());
    for (const auto& event : simulation.events_) {
      write_event(writer, event);
    }
    writer.integral(simulation.ruin_tide_);
    writer.integral(simulation.next_entity_id_);
    writer.integral(simulation.next_resource_id_);
    writer.integral(simulation.next_control_point_id_);
    writer.integral(simulation.next_sequence_);
    writer.integral(simulation.next_ai_decision_id_);
    writer.integral(simulation.next_event_id_);
    writer.integral(simulation.event_digest_);
  }

  static std::unique_ptr<Simulation> read_payload(Reader& reader) {
    SimulationConfig config{};
    if (!read_config(reader, config) || !valid_config(config)) {
      reader.fail(SnapshotError::InvalidData);
      return nullptr;
    }
    auto simulation = std::make_unique<Simulation>(config);
    simulation->config_ = std::move(config);
    if (!reader.integral(simulation->tick_) ||
        !reader.enumeration(simulation->status_, MatchStatus::Lost) ||
        !read_optional_enum(reader, simulation->winner_, PlayerId::Two)) {
      return nullptr;
    }
    for (auto& player : simulation->players_) {
      if (!read_player_state(reader, player)) {
        return nullptr;
      }
    }
    for (auto& seen : simulation->command_seen_) {
      if (!reader.boolean(seen)) {
        return nullptr;
      }
    }
    for (auto& grid : simulation->visibility_) {
      if (!read_visibility(reader, grid)) {
        return nullptr;
      }
    }
    for (auto& memories : simulation->resource_memory_) {
      std::size_t count{};
      if (!reader.count(count)) {
        return nullptr;
      }
      memories.resize(count);
      for (auto& memory : memories) {
        if (!reader.boolean(memory.discovered) ||
            !reader.integral(memory.amount) ||
            !reader.integral(memory.observed_tick)) {
          return nullptr;
        }
      }
    }
    for (auto& memories : simulation->control_point_memory_) {
      std::size_t count{};
      if (!reader.count(count)) {
        return nullptr;
      }
      memories.resize(count);
      for (auto& memory : memories) {
        if (!reader.boolean(memory.observed) ||
            !read_optional_enum(reader, memory.owner, PlayerId::Two) ||
            !reader.integral(memory.influence) ||
            !reader.integral(memory.observed_tick)) {
          return nullptr;
        }
      }
    }
    for (auto& memories : simulation->enemy_memory_) {
      std::size_t count{};
      if (!reader.count(count)) {
        return nullptr;
      }
      memories.resize(count);
      for (auto& memory : memories) {
        if (!read_observed_enemy(reader, memory)) {
          return nullptr;
        }
      }
    }
    for (auto& commander : simulation->commanders_) {
      if (!read_commander(reader, commander)) {
        return nullptr;
      }
    }
    if (!read_vector(reader, simulation->entities_, read_entity) ||
        !read_vector(reader, simulation->resources_, read_resource) ||
        !read_vector(reader, simulation->control_points_,
                     read_control_point) ||
        !read_vector(reader, simulation->vows_, read_vow)) {
      return nullptr;
    }
    std::size_t queued_count{};
    if (!reader.count(queued_count)) {
      return nullptr;
    }
    simulation->command_queue_.resize(queued_count);
    for (auto& queued : simulation->command_queue_) {
      if (!read_command(reader, queued.command) ||
          !reader.integral(queued.issued_tick) ||
          !reader.enumeration(queued.source, CommandSource::CommanderAI) ||
          !reader.integral(queued.observation_hash) ||
          !reader.integral(queued.ai_decision_id)) {
        return nullptr;
      }
    }
    if (!read_vector(reader, simulation->command_trace_,
                     read_command_trace) ||
        !read_vector(reader, simulation->ai_decision_trace_,
                     read_ai_decision) ||
        !read_vector(reader, simulation->events_, read_event) ||
        !reader.integral(simulation->ruin_tide_) ||
        !reader.integral(simulation->next_entity_id_) ||
        !reader.integral(simulation->next_resource_id_) ||
        !reader.integral(simulation->next_control_point_id_) ||
        !reader.integral(simulation->next_sequence_) ||
        !reader.integral(simulation->next_ai_decision_id_) ||
        !reader.integral(simulation->next_event_id_) ||
        !reader.integral(simulation->event_digest_)) {
      return nullptr;
    }
    if (!simulation->objective_system_.rebuild(simulation->events_)) {
      reader.fail(SnapshotError::InvalidData);
      return nullptr;
    }
    simulation->rebuild_entity_index();
    simulation->spatial_grid_.reset(simulation->config_.map_size,
                                    simulation->config_.spatial_cell_size);
    simulation->spatial_grid_.rebuild(simulation->entities_);
    simulation->supply_system_.rebuild(
        simulation->entities_, simulation->spatial_grid_, builtin_content());
    if (!validate(*simulation)) {
      reader.fail(SnapshotError::InvalidData);
      return nullptr;
    }
    return simulation;
  }

 private:
  template <typename Value, typename Decoder>
  static bool read_vector(Reader& reader, std::vector<Value>& values,
                          Decoder decoder) {
    std::size_t count{};
    if (!reader.count(count)) {
      return false;
    }
    values.resize(count);
    for (auto& value : values) {
      if (!decoder(reader, value)) {
        return false;
      }
    }
    return true;
  }

  static bool grid_shape(const Vec2 map_size, const std::int32_t cell_size,
                         std::int32_t& columns, std::int32_t& rows,
                         std::size_t& cell_count) noexcept {
    if (map_size.x < 0 || map_size.y < 0 || cell_size <= 0) {
      return false;
    }
    const auto columns_64 = std::max<std::int64_t>(
        1, (static_cast<std::int64_t>(map_size.x) + cell_size - 1) /
               cell_size);
    const auto rows_64 = std::max<std::int64_t>(
        1, (static_cast<std::int64_t>(map_size.y) + cell_size - 1) /
               cell_size);
    const auto count_64 =
        static_cast<std::uint64_t>(columns_64) *
        static_cast<std::uint64_t>(rows_64);
    if (columns_64 > std::numeric_limits<std::int32_t>::max() ||
        rows_64 > std::numeric_limits<std::int32_t>::max() ||
        count_64 > kMaximumCollectionElements) {
      return false;
    }
    columns = static_cast<std::int32_t>(columns_64);
    rows = static_cast<std::int32_t>(rows_64);
    cell_count = static_cast<std::size_t>(count_64);
    return true;
  }

  static void write_visibility(Writer& writer,
                               const VisibilityGrid& grid) {
    write_vec2(writer, grid.map_size_);
    writer.integral(grid.cell_size_);
    writer.integral(grid.columns_);
    writer.integral(grid.rows_);
    writer.count(grid.cells_.size());
    for (const auto state : grid.cells_) {
      writer.enumeration(state);
    }
  }

  static bool read_visibility(Reader& reader, VisibilityGrid& grid) {
    Vec2 map_size{};
    std::int32_t cell_size{};
    std::int32_t columns{};
    std::int32_t rows{};
    if (!read_vec2(reader, map_size) ||
        !reader.integral(cell_size) ||
        !reader.integral(columns) ||
        !reader.integral(rows)) {
      reader.fail(SnapshotError::InvalidData);
      return false;
    }
    std::size_t cell_count{};
    if (!reader.count(cell_count)) {
      return false;
    }
    std::int32_t expected_columns{};
    std::int32_t expected_rows{};
    std::size_t expected_cell_count{};
    if (!grid_shape(map_size, cell_size, expected_columns, expected_rows,
                    expected_cell_count) ||
        columns != expected_columns || rows != expected_rows ||
        cell_count != expected_cell_count) {
      reader.fail(SnapshotError::InvalidData);
      return false;
    }
    grid.map_size_ = map_size;
    grid.cell_size_ = cell_size;
    grid.columns_ = columns;
    grid.rows_ = rows;
    grid.cells_.assign(cell_count, VisibilityState::Hidden);
    for (auto& state : grid.cells_) {
      if (!reader.enumeration(state, VisibilityState::Visible)) {
        return false;
      }
    }
    return true;
  }

  static void write_observation(Writer& writer,
                                const PlayerObservation& observation) {
    writer.integral(observation.tick_);
    writer.integral(observation.revision_);
    writer.integral(observation.knowledge_tick_);
    writer.integral(observation.match_seed_);
    writer.enumeration(observation.player_);
    writer.enumeration(observation.opponent_faction_);
    writer.enumeration(observation.status_);
    write_player_state(writer, observation.self_);
    writer.integral(observation.ruin_tide_);
    write_vec2(writer, observation.map_size_);
    writer.integral(observation.navigation_cell_size_);
    writer.count(observation.navigation_obstacles_.size());
    for (const auto& obstacle : observation.navigation_obstacles_) {
      write_navigation_obstacle(writer, obstacle);
    }
    write_visibility(writer, observation.explored_map_);
    writer.count(observation.owned_entities_.size());
    for (const auto& entity : observation.owned_entities_) {
      write_entity(writer, entity);
    }
    writer.count(observation.known_enemies_.size());
    for (const auto& enemy : observation.known_enemies_) {
      write_observed_enemy(writer, enemy);
    }
    writer.count(observation.known_resources_.size());
    for (const auto& resource : observation.known_resources_) {
      write_observed_resource(writer, resource);
    }
    writer.count(observation.public_objectives_.size());
    for (const auto& objective : observation.public_objectives_) {
      write_observed_control_point(writer, objective);
    }
    writer.count(observation.capabilities_.size());
    for (const auto& capability : observation.capabilities_) {
      write_capability(writer, capability);
    }
  }

  static bool read_observation(
      Reader& reader, std::unique_ptr<PlayerObservation>& output) {
    Tick tick{};
    std::uint64_t revision{};
    Tick knowledge_tick{};
    std::uint64_t match_seed{};
    PlayerId player{};
    FactionId opponent_faction{};
    MatchStatus status{};
    PlayerState self{};
    std::int32_t ruin_tide{};
    Vec2 map_size{};
    std::int32_t navigation_cell_size{};
    if (!reader.integral(tick) ||
        !reader.integral(revision) ||
        !reader.integral(knowledge_tick) ||
        !reader.integral(match_seed) ||
        !reader.enumeration(player, PlayerId::Two) ||
        !reader.enumeration(opponent_faction, FactionId::Concord) ||
        !reader.enumeration(status, MatchStatus::Lost) ||
        !read_player_state(reader, self) ||
        !reader.integral(ruin_tide) ||
        !read_vec2(reader, map_size) ||
        !reader.integral(navigation_cell_size)) {
      return false;
    }
    std::vector<NavigationObstacle> obstacles;
    std::vector<Entity> owned_entities;
    std::vector<ObservedEnemy> enemies;
    std::vector<ObservedResource> resources;
    std::vector<ObservedControlPoint> objectives;
    std::vector<CommandCapability> capabilities;
    if (!read_vector(reader, obstacles, read_navigation_obstacle)) {
      return false;
    }
    VisibilityGrid explored_map;
    if (!read_visibility(reader, explored_map) ||
        !read_vector(reader, owned_entities, read_entity) ||
        !read_vector(reader, enemies, read_observed_enemy) ||
        !read_vector(reader, resources, read_observed_resource) ||
        !read_vector(reader, objectives, read_observed_control_point) ||
        !read_vector(reader, capabilities, read_capability)) {
      return false;
    }
    if (navigation_cell_size <= 0 || map_size.x < 0 || map_size.y < 0 ||
        explored_map.map_size_ != map_size ||
        self.id != player || knowledge_tick > tick ||
        revision < knowledge_tick) {
      reader.fail(SnapshotError::InvalidData);
      return false;
    }
    output = std::unique_ptr<PlayerObservation>(new PlayerObservation{
        tick, match_seed, player, opponent_faction, status, std::move(self),
        ruin_tide, map_size, navigation_cell_size, std::move(obstacles),
        std::move(explored_map), std::move(owned_entities),
        std::move(enemies), std::move(resources), std::move(objectives),
        std::move(capabilities)});
    output->revision_ = revision;
    output->knowledge_tick_ = knowledge_tick;
    return true;
  }

  static void write_commander(Writer& writer,
                              const CommanderAI& commander) {
    writer.enumeration(commander.player_);
    writer.enumeration(commander.difficulty_);
    write_strategy_state(writer, commander.strategy_state_);
    writer.count(commander.observation_history_.size());
    for (const auto& observation : commander.observation_history_) {
      write_observation(writer, observation);
    }
  }

  static bool read_commander(Reader& reader, CommanderAI& commander) {
    if (!reader.enumeration(commander.player_, PlayerId::Two) ||
        !reader.enumeration(commander.difficulty_,
                            AIDifficulty::Competitive) ||
        !read_strategy_state(reader, commander.strategy_state_)) {
      return false;
    }
    std::size_t history_count{};
    if (!reader.count(history_count)) {
      return false;
    }
    commander.observation_history_.clear();
    for (std::size_t index = 0; index < history_count; ++index) {
      std::unique_ptr<PlayerObservation> observation;
      if (!read_observation(reader, observation)) {
        return false;
      }
      commander.observation_history_.push_back(std::move(*observation));
    }
    return true;
  }

  static bool valid_config(const SimulationConfig& config) noexcept {
    if (config.map_size.x <= 0 || config.map_size.y <= 0) {
      return false;
    }
    std::int32_t columns{};
    std::int32_t rows{};
    std::size_t cell_count{};
    return grid_shape(config.map_size, config.visibility_cell_size,
                      columns, rows, cell_count) &&
           grid_shape(config.map_size, config.navigation_cell_size,
                      columns, rows, cell_count) &&
           grid_shape(config.map_size, config.spatial_cell_size,
                      columns, rows, cell_count);
  }

  template <typename Value, typename IdProjection>
  static bool stable_ids(const std::vector<Value>& values,
                         const std::uint64_t next_id,
                         IdProjection projection) noexcept {
    std::uint64_t previous{};
    for (const auto& value : values) {
      const auto id = static_cast<std::uint64_t>(projection(value));
      if (id == 0 || id <= previous || id >= next_id) {
        return false;
      }
      previous = id;
    }
    return next_id > previous;
  }

  static bool validate(const Simulation& simulation) noexcept {
    if (!valid_config(simulation.config_) ||
        simulation.players_[0].id != PlayerId::One ||
        simulation.players_[1].id != PlayerId::Two ||
        simulation.players_[0].faction !=
            simulation.config_.player_one_faction ||
        simulation.players_[1].faction !=
            simulation.config_.player_two_faction ||
        simulation.commanders_[0].player_ != PlayerId::One ||
        simulation.commanders_[1].player_ != PlayerId::Two ||
        simulation.commanders_[0].difficulty_ !=
            simulation.config_.commander_difficulties[0] ||
        simulation.commanders_[1].difficulty_ !=
            simulation.config_.commander_difficulties[1] ||
        simulation.resource_memory_[0].size() !=
            simulation.resources_.size() ||
        simulation.resource_memory_[1].size() !=
            simulation.resources_.size() ||
        simulation.control_point_memory_[0].size() !=
            simulation.control_points_.size() ||
        simulation.control_point_memory_[1].size() !=
            simulation.control_points_.size() ||
        !simulation.objective_system_.event_projection_matches(
            simulation.config_, simulation.events_) ||
        !simulation.objective_system_.outcome_matches(
            simulation.config_.mode, simulation.status_, simulation.winner_) ||
        !simulation.supply_system_.derivation_matches(
            simulation.entities_, simulation.spatial_grid_,
            builtin_content())) {
      return false;
    }
    if (!stable_ids(
            simulation.entities_, simulation.next_entity_id_,
            [](const Entity& value) { return value.id.value; }) ||
        !stable_ids(
            simulation.resources_, simulation.next_resource_id_,
            [](const ResourceNode& value) { return value.id.value; }) ||
        !stable_ids(
            simulation.control_points_, simulation.next_control_point_id_,
            [](const ControlPoint& value) { return value.id.value; }) ||
        !stable_ids(
            simulation.vows_, std::numeric_limits<std::uint64_t>::max(),
            [](const VowState& value) { return value.id.value; })) {
      return false;
    }
    if (simulation.next_sequence_ == 0 ||
        simulation.next_ai_decision_id_ == 0 ||
        simulation.next_event_id_ == 0) {
      return false;
    }
    if (!std::ranges::is_sorted(
            simulation.command_queue_, {},
            [](const Simulation::QueuedCommand& queued) {
              return std::tuple{
                  queued.command.execute_tick, queued.command.sequence,
                  player_index(queued.command.player)};
            })) {
      return false;
    }
    auto expected_event_id = std::uint64_t{1};
    auto digest = kFnvOffset;
    for (const auto& event : simulation.events_) {
      if (event.id.value != expected_event_id ||
          event.tick > simulation.tick_) {
        return false;
      }
      fold_u64(digest, simulation_event_hash(event));
      ++expected_event_id;
    }
    if (simulation.next_event_id_ != expected_event_id ||
        simulation.event_digest_ != digest) {
      return false;
    }
    for (const auto& commander : simulation.commanders_) {
      if (!std::ranges::is_sorted(
              commander.observation_history_, {},
              [](const PlayerObservation& observation) {
                return observation.tick_;
              })) {
        return false;
      }
      for (const auto& observation : commander.observation_history_) {
        const auto observer_index = player_index(commander.player_);
        const auto opponent_index = 1U - observer_index;
        if (observation.player_ != commander.player_ ||
            observation.match_seed_ != simulation.config_.match_seed ||
            observation.tick_ > simulation.tick_ ||
            observation.self_.id != commander.player_ ||
            observation.self_.faction !=
                simulation.players_[observer_index].faction ||
            observation.opponent_faction_ !=
                simulation.players_[opponent_index].faction ||
            observation.map_size_ != simulation.config_.map_size ||
            observation.navigation_cell_size_ !=
                simulation.config_.navigation_cell_size ||
            observation.navigation_obstacles_ !=
                simulation.config_.navigation_obstacles ||
            observation.explored_map_.map_size_ !=
                simulation.config_.map_size ||
            observation.explored_map_.cell_size_ !=
                simulation.config_.visibility_cell_size) {
          return false;
        }
      }
    }
    return true;
  }
};

std::uint64_t current_content_digest() {
  Writer writer;
  writer.integral(kSnapshotSchemaVersion);
  write_content_registry(writer, builtin_content());
  write_gameplay_catalog(writer);
  writer.integral(scenario_catalog_digest());
  return hash_bytes(writer.bytes());
}

std::uint64_t current_pipeline_digest() {
  Writer writer;
  writer.integral(kSnapshotSchemaVersion);
  writer.integral(kTicksPerSecond);
  writer.count(kTargetSystemPipeline.size());
  for (const auto& phase : kTargetSystemPipeline) {
    writer.enumeration(phase.phase);
    writer.text(phase.development_name);
  }
  return hash_bytes(writer.bytes());
}

std::vector<std::uint8_t> save_snapshot_v1(
    const Simulation& simulation) {
  if (!SnapshotCodec::can_encode(simulation)) {
    throw std::invalid_argument(
        "Simulation state is outside the SnapshotV1 invariants.");
  }
  Writer payload_writer;
  SnapshotCodec::write_payload(payload_writer, simulation);
  const auto& payload = payload_writer.bytes();
  if (payload.size() > kMaximumPayloadBytes) {
    throw std::length_error("Snapshot payload exceeds the V1 limit.");
  }

  const SnapshotHeader header{
      .schema_version = kSnapshotSchemaVersion,
      .minimum_reader_version = kSnapshotMinimumReaderVersion,
      .content_digest = current_content_digest(),
      .pipeline_digest = current_pipeline_digest(),
      .checkpoint_tick = simulation.tick(),
      .checkpoint_state_hash = simulation.state_hash(),
      .payload_size = payload.size(),
      .payload_hash = hash_bytes(payload),
  };

  Writer snapshot_writer;
  snapshot_writer.append(kSnapshotMagic);
  snapshot_writer.integral(header.schema_version);
  snapshot_writer.integral(header.minimum_reader_version);
  snapshot_writer.integral(header.content_digest);
  snapshot_writer.integral(header.pipeline_digest);
  snapshot_writer.integral(header.checkpoint_tick);
  snapshot_writer.integral(header.checkpoint_state_hash);
  snapshot_writer.integral(header.payload_size);
  snapshot_writer.integral(header.payload_hash);
  snapshot_writer.append(payload);
  return std::move(snapshot_writer).release();
}

SnapshotLoadResult load_snapshot_v1(
    const std::span<const std::uint8_t> bytes) {
  SnapshotLoadResult result;
  if (bytes.empty()) {
    result.error = SnapshotError::EmptyInput;
    return result;
  }

  Reader reader{bytes};
  const auto magic = reader.take(kSnapshotMagic.size());
  if (!reader.ok()) {
    result.error = reader.error();
    return result;
  }
  if (!std::ranges::equal(magic, kSnapshotMagic)) {
    result.error = SnapshotError::BadMagic;
    return result;
  }
  if (!reader.integral(result.header.schema_version) ||
      !reader.integral(result.header.minimum_reader_version) ||
      !reader.integral(result.header.content_digest) ||
      !reader.integral(result.header.pipeline_digest) ||
      !reader.integral(result.header.checkpoint_tick) ||
      !reader.integral(result.header.checkpoint_state_hash) ||
      !reader.integral(result.header.payload_size) ||
      !reader.integral(result.header.payload_hash)) {
    result.error = reader.error();
    return result;
  }
  if (result.header.schema_version != kSnapshotSchemaVersion ||
      result.header.minimum_reader_version > kSnapshotSchemaVersion) {
    result.error = SnapshotError::UnsupportedSchema;
    return result;
  }
  if (result.header.content_digest != current_content_digest()) {
    result.error = SnapshotError::IncompatibleContent;
    return result;
  }
  if (result.header.pipeline_digest != current_pipeline_digest()) {
    result.error = SnapshotError::IncompatiblePipeline;
    return result;
  }
  if (result.header.payload_size > kMaximumPayloadBytes ||
      result.header.payload_size >
          static_cast<std::uint64_t>(
              std::numeric_limits<std::size_t>::max())) {
    result.error = SnapshotError::PayloadTooLarge;
    return result;
  }

  const auto payload_size =
      static_cast<std::size_t>(result.header.payload_size);
  if (payload_size > reader.remaining()) {
    result.error = SnapshotError::Truncated;
    return result;
  }
  if (payload_size < reader.remaining()) {
    result.error = SnapshotError::TrailingData;
    return result;
  }
  const auto payload = reader.take(payload_size);
  if (hash_bytes(payload) != result.header.payload_hash) {
    result.error = SnapshotError::ChecksumMismatch;
    return result;
  }

  Reader payload_reader{payload};
  try {
    result.simulation = SnapshotCodec::read_payload(payload_reader);
  } catch (const std::bad_alloc&) {
    result.error = SnapshotError::InvalidData;
    return result;
  } catch (const std::length_error&) {
    result.error = SnapshotError::InvalidData;
    return result;
  }
  if (!payload_reader.ok()) {
    result.error = payload_reader.error();
    result.simulation.reset();
    return result;
  }
  if (payload_reader.remaining() != 0) {
    result.error = SnapshotError::TrailingData;
    result.simulation.reset();
    return result;
  }
  if (!result.simulation) {
    result.error = SnapshotError::InvalidData;
    return result;
  }
  if (result.simulation->tick() != result.header.checkpoint_tick ||
      result.simulation->state_hash() !=
          result.header.checkpoint_state_hash) {
    result.error = SnapshotError::StateHashMismatch;
    result.simulation.reset();
    return result;
  }
  return result;
}

std::string_view to_string(const SnapshotError error) noexcept {
  switch (error) {
    case SnapshotError::None:
      return "none";
    case SnapshotError::EmptyInput:
      return "empty input";
    case SnapshotError::BadMagic:
      return "bad magic";
    case SnapshotError::UnsupportedSchema:
      return "unsupported schema";
    case SnapshotError::IncompatibleContent:
      return "incompatible content";
    case SnapshotError::IncompatiblePipeline:
      return "incompatible pipeline";
    case SnapshotError::Truncated:
      return "truncated";
    case SnapshotError::TrailingData:
      return "trailing data";
    case SnapshotError::PayloadTooLarge:
      return "payload too large";
    case SnapshotError::ChecksumMismatch:
      return "checksum mismatch";
    case SnapshotError::InvalidData:
      return "invalid data";
    case SnapshotError::StateHashMismatch:
      return "state hash mismatch";
  }
  return "unknown";
}

}  // namespace ashen::core
