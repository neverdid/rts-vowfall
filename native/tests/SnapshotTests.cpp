#include "ashen/core/Content.hpp"
#include "ashen/core/Snapshot.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace ashen::core;

inline constexpr std::size_t kSchemaOffset = 8;
inline constexpr std::size_t kMinimumReaderOffset = 12;
inline constexpr std::size_t kContentDigestOffset = 16;
inline constexpr std::size_t kPipelineDigestOffset = 24;
inline constexpr std::size_t kCheckpointHashOffset = 40;
inline constexpr std::size_t kPayloadSizeOffset = 48;
inline constexpr std::size_t kPayloadHashOffset = 56;
inline constexpr std::size_t kPayloadOffset = 64;
inline constexpr std::uint64_t kFnvOffset =
    14'695'981'039'346'656'037ULL;
inline constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "  check failed at line " << __LINE__ << ": " #condition   \
                << '\n';                                                        \
      ++failures;                                                               \
    }                                                                           \
  } while (false)

template <typename Test>
void run_test(const std::string_view name, Test&& test) {
  const auto before = failures;
  test();
  std::cout << (failures == before ? "[pass] " : "[fail] ") << name
            << '\n';
}

void write_u32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               std::uint32_t value) {
  for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
    bytes[offset + byte] = static_cast<std::uint8_t>(value & 0xffU);
    value >>= 8U;
  }
}

void write_u64(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               std::uint64_t value) {
  for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
    bytes[offset + byte] = static_cast<std::uint8_t>(value & 0xffU);
    value >>= 8U;
  }
}

[[nodiscard]] std::uint64_t hash_bytes(
    const std::span<const std::uint8_t> bytes) {
  auto hash = kFnvOffset;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= kFnvPrime;
  }
  return hash;
}

[[nodiscard]] Simulation live_ai_checkpoint() {
  SimulationConfig config{};
  config.match_seed = 0x00c0ffeeULL;
  config.commander_players = {true, true};
  config.commander_difficulties = {
      AIDifficulty::Standard, AIDifficulty::Veteran};
  Simulation simulation{config};

  CHECK(simulation.execute_now(Command{
            .player = PlayerId::One,
            .type = CommandType::MakeVow,
            .vow = kBridgeOpenVow})
            .ok);
  CHECK(simulation.execute_now(Command{
            .player = PlayerId::Two,
            .type = CommandType::AmendVow,
            .vow = kBridgeOpenVow})
            .ok);
  simulation.run(240);

  const auto worker = std::ranges::find_if(
      simulation.entities(), [](const Entity& entity) {
        return entity.owner == PlayerId::One &&
               entity.type == EntityType::Worker;
      });
  CHECK(worker != simulation.entities().end());
  if (worker != simulation.entities().end()) {
    CHECK(simulation.execute_now(Command{
              .player = PlayerId::One,
              .type = CommandType::Move,
              .entities = {worker->id},
              .target = world(850, 600),
          })
              .ok);
    CHECK(simulation.execute_now(Command{
              .player = PlayerId::One,
              .type = CommandType::Move,
              .entities = {worker->id},
              .target = world(875, 625),
              .queue = true,
          })
              .ok);
    simulation.enqueue(Command{
        .execute_tick = simulation.tick() + 37,
        .player = PlayerId::One,
        .type = CommandType::Move,
        .entities = {worker->id},
        .target = world(900, 650),
    });
  }
  return simulation;
}

void deterministic_round_trip_preserves_every_public_audit_stream() {
  auto original = live_ai_checkpoint();
  CHECK(!original.command_trace().empty());
  CHECK(!original.ai_decision_trace().empty());
  CHECK(std::ranges::any_of(
      original.ai_decision_trace(), [](const AIDecisionRecord& decision) {
        return decision.command_status == AICommandStatus::Queued;
      }));
  CHECK(!original.events().empty());
  CHECK(!original.vows().empty());
  CHECK(!original.vows().empty() &&
        original.vows().front().resolution ==
            VowResolution::Amended);
  CHECK(!original.vows().empty() &&
        original.vows().front().participating_affected_player ==
            PlayerId::Two);
  const auto bytes = save_snapshot_v1(original);
  const auto duplicate_bytes = save_snapshot_v1(original);
  CHECK(bytes == duplicate_bytes);
  CHECK(bytes == save_snapshot_v1(live_ai_checkpoint()));
  CHECK(bytes.size() > kPayloadOffset);

  auto loaded = load_snapshot_v1(bytes);
  CHECK(loaded);
  CHECK(loaded.error == SnapshotError::None);
  CHECK(loaded.header.schema_version == kSnapshotSchemaVersion);
  CHECK(loaded.header.minimum_reader_version ==
        kSnapshotMinimumReaderVersion);
  CHECK(loaded.header.content_digest == current_content_digest());
  CHECK(loaded.header.pipeline_digest == current_pipeline_digest());
  CHECK(loaded.header.checkpoint_tick == original.tick());
  CHECK(loaded.header.checkpoint_state_hash == original.state_hash());
  if (!loaded) {
    return;
  }

  auto& restored = *loaded.simulation;
  CHECK(restored.state_hash() == original.state_hash());
  CHECK(restored.vows() == original.vows());
  CHECK(restored.command_trace() == original.command_trace());
  CHECK(restored.ai_decision_trace() == original.ai_decision_trace());
  CHECK(restored.events() == original.events());
  CHECK(restored.event_digest() == original.event_digest());
  CHECK(save_snapshot_v1(restored) == bytes);

  CHECK(restored.spatial_grid().entry_count() ==
        restored.entities().size());
  const auto first_entity = restored.entities().front().id;
  CHECK(restored.find_entity(first_entity) != nullptr);
  const auto restored_hits = restored.spatial_grid().query_radius(
      restored.entities().front().position, world(300, 0).x);
  CHECK(std::ranges::any_of(
      restored_hits, [first_entity](const SpatialQueryHit& hit) {
        return hit.id == first_entity;
      }));

  SimulationConfig zero_seed_config{};
  zero_seed_config.match_seed = 0;
  Simulation zero_seed{zero_seed_config};
  auto zero_seed_result =
      load_snapshot_v1(save_snapshot_v1(zero_seed));
  CHECK(zero_seed_result);
  CHECK(zero_seed_result &&
        zero_seed_result.simulation->state_hash() ==
            zero_seed.state_hash());
}

void restored_ai_match_continues_bit_exactly() {
  auto original = live_ai_checkpoint();
  auto loaded = load_snapshot_v1(save_snapshot_v1(original));
  CHECK(loaded);
  if (!loaded) {
    return;
  }

  auto& restored = *loaded.simulation;
  original.run(480);
  restored.run(480);
  CHECK(restored.tick() == original.tick());
  CHECK(restored.status() == original.status());
  CHECK(restored.winner() == original.winner());
  CHECK(restored.state_hash() == original.state_hash());
  CHECK(restored.command_trace() == original.command_trace());
  CHECK(restored.ai_decision_trace() == original.ai_decision_trace());
  CHECK(restored.events() == original.events());
  CHECK(restored.event_digest() == original.event_digest());
  CHECK(save_snapshot_v1(restored) == save_snapshot_v1(original));
}

void malformed_and_incompatible_snapshots_are_rejected() {
  const auto valid = save_snapshot_v1(live_ai_checkpoint());

  SimulationConfig invalid_config{};
  invalid_config.seed_starting_forces = false;
  invalid_config.map_size = {};
  invalid_config.navigation_obstacles.clear();
  const Simulation invalid_simulation{invalid_config};
  auto invalid_save_rejected = false;
  try {
    static_cast<void>(save_snapshot_v1(invalid_simulation));
  } catch (const std::invalid_argument&) {
    invalid_save_rejected = true;
  }
  CHECK(invalid_save_rejected);

  CHECK(load_snapshot_v1({}).error == SnapshotError::EmptyInput);

  auto bad_magic = valid;
  bad_magic[0] ^= 0xffU;
  CHECK(load_snapshot_v1(bad_magic).error == SnapshotError::BadMagic);

  auto unsupported_schema = valid;
  write_u32(unsupported_schema, kSchemaOffset,
            kSnapshotSchemaVersion + 1U);
  CHECK(load_snapshot_v1(unsupported_schema).error ==
        SnapshotError::UnsupportedSchema);

  auto unsupported_reader = valid;
  write_u32(unsupported_reader, kMinimumReaderOffset,
            kSnapshotSchemaVersion + 1U);
  CHECK(load_snapshot_v1(unsupported_reader).error ==
        SnapshotError::UnsupportedSchema);

  auto incompatible_content = valid;
  incompatible_content[kContentDigestOffset] ^= 1U;
  CHECK(load_snapshot_v1(incompatible_content).error ==
        SnapshotError::IncompatibleContent);

  auto incompatible_pipeline = valid;
  incompatible_pipeline[kPipelineDigestOffset] ^= 1U;
  CHECK(load_snapshot_v1(incompatible_pipeline).error ==
        SnapshotError::IncompatiblePipeline);

  auto too_large = valid;
  write_u64(too_large, kPayloadSizeOffset,
            256ULL * 1'024 * 1'024 + 1ULL);
  CHECK(load_snapshot_v1(too_large).error ==
        SnapshotError::PayloadTooLarge);

  auto truncated = valid;
  truncated.pop_back();
  CHECK(load_snapshot_v1(truncated).error == SnapshotError::Truncated);

  auto trailing = valid;
  trailing.push_back(0);
  CHECK(load_snapshot_v1(trailing).error == SnapshotError::TrailingData);

  auto bad_checksum = valid;
  bad_checksum.back() ^= 1U;
  CHECK(load_snapshot_v1(bad_checksum).error ==
        SnapshotError::ChecksumMismatch);

  auto bad_state_hash = valid;
  bad_state_hash[kCheckpointHashOffset] ^= 1U;
  CHECK(load_snapshot_v1(bad_state_hash).error ==
        SnapshotError::StateHashMismatch);

  auto invalid_enum = valid;
  invalid_enum[kPayloadOffset] = 0xffU;
  write_u64(
      invalid_enum, kPayloadHashOffset,
      hash_bytes(std::span<const std::uint8_t>{invalid_enum}.subspan(
          kPayloadOffset)));
  CHECK(load_snapshot_v1(invalid_enum).error ==
        SnapshotError::InvalidData);

  auto unsafe_grid = valid;
  write_u32(unsafe_grid, kPayloadOffset + 24,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()));
  write_u32(unsafe_grid, kPayloadOffset + 32, 1U);
  write_u64(
      unsafe_grid, kPayloadHashOffset,
      hash_bytes(std::span<const std::uint8_t>{unsafe_grid}.subspan(
          kPayloadOffset)));
  CHECK(load_snapshot_v1(unsafe_grid).error ==
        SnapshotError::InvalidData);

  CHECK(to_string(SnapshotError::ChecksumMismatch) ==
        "checksum mismatch");
}

}  // namespace

int main() {
  run_test("deterministic snapshot round-trip preserves audit streams",
           deterministic_round_trip_preserves_every_public_audit_stream);
  run_test("restored AI match continues bit-exactly",
           restored_ai_match_continues_bit_exactly);
  run_test("malformed and incompatible snapshots are rejected",
           malformed_and_incompatible_snapshots_are_rejected);

  if (failures != 0) {
    std::cerr << failures << " snapshot check(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All Vowfall SnapshotV3 checks passed.\n";
  return EXIT_SUCCESS;
}
