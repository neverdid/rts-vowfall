#include "ashen/core/Content.hpp"
#include "ashen/core/Replay.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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
inline constexpr std::size_t kPayloadSizeOffset = 64;
inline constexpr std::size_t kPayloadHashOffset = 72;
inline constexpr std::size_t kPayloadOffset = 80;
inline constexpr std::size_t kEmbeddedSnapshotOffset = 88;
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

void repair_payload_hash(std::vector<std::uint8_t>& bytes) {
  write_u64(bytes, kPayloadHashOffset,
            hash_bytes(std::span{bytes}.subspan(kPayloadOffset)));
}

struct RecordedMatch {
  ReplayData replay{};
  std::vector<std::uint8_t> bytes{};
  std::vector<std::uint8_t> final_snapshot{};
};

[[nodiscard]] RecordedMatch record_match() {
  SimulationConfig config{};
  config.match_seed = 0x51a7e5eedULL;
  config.commander_players = {true, true};
  config.commander_difficulties = {
      AIDifficulty::Standard, AIDifficulty::Veteran};
  Simulation simulation{config};
  ReplayRecorder recorder{simulation};

  CHECK(recorder.execute_now(
            simulation,
            Command{.player = PlayerId::One,
                    .type = CommandType::MakeVow,
                    .vow = kBridgeOpenVow})
            .ok);
  CHECK(!recorder.execute_now(
             simulation,
             Command{.player = PlayerId::One,
                     .type = CommandType::Move,
                     .entities = {EntityId{999'999}},
                     .target = world(400, 400)})
             .ok);

  const auto worker = std::ranges::find_if(
      simulation.entities(), [](const Entity& entity) {
        return entity.owner == PlayerId::One &&
               entity.type == EntityType::Worker;
      });
  CHECK(worker != simulation.entities().end());
  const auto worker_id = worker != simulation.entities().end()
                             ? worker->id
                             : EntityId{};
  if (worker != simulation.entities().end()) {
    CHECK(recorder.enqueue(
              simulation,
              Command{.execute_tick = 10,
                      .player = PlayerId::One,
                      .type = CommandType::Move,
                      .entities = {worker_id},
                      .target = world(760, 610)}) != 0);
  }
  recorder.capture_checkpoint(simulation);

  while (simulation.tick() < 240 &&
         simulation.status() == MatchStatus::Playing) {
    simulation.step();
    if (simulation.tick() == 120) {
      recorder.capture_checkpoint(simulation);
      CHECK(recorder.execute_now(
                simulation,
                Command{.player = PlayerId::One,
                        .type = CommandType::KeepVow,
                        .vow = kBridgeOpenVow})
                .ok);
    }
    if (simulation.tick() == 200 && worker_id) {
      CHECK(recorder.enqueue(
                simulation,
                Command{.execute_tick = 300,
                        .player = PlayerId::One,
                        .type = CommandType::Move,
                        .entities = {worker_id},
                        .target = world(820, 640)}) != 0);
    }
    if (simulation.tick() % 60 == 0) {
      recorder.capture_checkpoint(simulation);
    }
  }

  auto replay = recorder.finish(simulation);
  auto bytes = save_replay_v1(replay);
  return RecordedMatch{std::move(replay), std::move(bytes),
                       save_snapshot_v1(simulation)};
}

void replay_round_trip_and_verification_are_bit_exact() {
  const auto first = record_match();
  const auto second = record_match();
  CHECK(first.bytes == second.bytes);
  CHECK(first.replay.inputs.size() == 5);
  CHECK(std::ranges::any_of(first.replay.inputs,
                            [](const ReplayInput& input) {
                              return input.applied && !input.accepted;
                            }));
  CHECK(std::ranges::any_of(first.replay.inputs,
                            [](const ReplayInput& input) {
                              return !input.applied;
                            }));
  CHECK(std::ranges::any_of(first.replay.expected_commands,
                            [](const CommandTraceEntry& command) {
                              return command.source ==
                                     CommandSource::CommanderAI;
                            }));
  CHECK(!first.replay.expected_events.empty());
  CHECK(first.replay.checkpoints.size() >= 5);

  const auto loaded = load_replay_v1(first.bytes);
  CHECK(loaded);
  CHECK(loaded.error == ReplayError::None);
  CHECK(loaded.header.schema_version == kReplaySchemaVersion);
  CHECK(loaded.header.content_digest == current_content_digest());
  CHECK(loaded.header.pipeline_digest == current_pipeline_digest());
  CHECK(loaded && save_replay_v1(*loaded.replay) == first.bytes);

  const auto verified = verify_replay_v1(first.bytes);
  if (!verified) {
    std::cerr << "  verification error: " << to_string(verified.error)
              << " tick=" << verified.mismatch_tick
              << " index=" << verified.mismatch_index
              << " expected=" << verified.expected
              << " actual=" << verified.actual << '\n';
  }
  CHECK(verified);
  CHECK(verified.error == ReplayError::None);
  CHECK(verified.simulation != nullptr);
  CHECK(verified.simulation &&
        save_snapshot_v1(*verified.simulation) == first.final_snapshot);
}

void malformed_and_incompatible_containers_are_rejected() {
  const auto valid = record_match().bytes;
  CHECK(load_replay_v1({}).error == ReplayError::EmptyInput);

  auto bad_magic = valid;
  bad_magic[0] ^= 1U;
  CHECK(load_replay_v1(bad_magic).error == ReplayError::BadMagic);

  auto future_schema = valid;
  write_u32(future_schema, kSchemaOffset, kReplaySchemaVersion + 1);
  CHECK(load_replay_v1(future_schema).error ==
        ReplayError::UnsupportedSchema);

  auto future_reader = valid;
  write_u32(future_reader, kMinimumReaderOffset,
            kReplaySchemaVersion + 1);
  CHECK(load_replay_v1(future_reader).error ==
        ReplayError::UnsupportedSchema);

  auto bad_content = valid;
  bad_content[kContentDigestOffset] ^= 1U;
  CHECK(load_replay_v1(bad_content).error ==
        ReplayError::IncompatibleContent);

  auto bad_pipeline = valid;
  bad_pipeline[kPipelineDigestOffset] ^= 1U;
  CHECK(load_replay_v1(bad_pipeline).error ==
        ReplayError::IncompatiblePipeline);

  auto oversized = valid;
  write_u64(oversized, kPayloadSizeOffset,
            513ULL * 1'024 * 1'024);
  CHECK(load_replay_v1(oversized).error == ReplayError::PayloadTooLarge);

  auto truncated = valid;
  truncated.pop_back();
  CHECK(load_replay_v1(truncated).error == ReplayError::Truncated);

  auto trailing = valid;
  trailing.push_back(0);
  CHECK(load_replay_v1(trailing).error == ReplayError::TrailingData);

  auto bad_checksum = valid;
  bad_checksum.back() ^= 1U;
  CHECK(load_replay_v1(bad_checksum).error ==
        ReplayError::ChecksumMismatch);

  auto bad_snapshot = valid;
  bad_snapshot[kEmbeddedSnapshotOffset] ^= 1U;
  repair_payload_hash(bad_snapshot);
  CHECK(load_replay_v1(bad_snapshot).error ==
        ReplayError::InitialSnapshotRejected);
}

void verifier_reports_commands_events_and_checkpoints() {
  const auto recorded = record_match();
  auto command_mismatch = recorded.replay;
  const auto ai_command = std::ranges::find(
      command_mismatch.expected_commands, CommandSource::CommanderAI,
      &CommandTraceEntry::source);
  CHECK(ai_command != command_mismatch.expected_commands.end());
  if (ai_command != command_mismatch.expected_commands.end()) {
    ai_command->observation_hash ^= 1U;
    const auto result = verify_replay_v1(save_replay_v1(command_mismatch));
    if (result.error != ReplayError::CommandMismatch) {
      std::cerr << "  command tamper reported: " << to_string(result.error)
                << '\n';
    }
    CHECK(result.error == ReplayError::CommandMismatch);
  }

  auto event_mismatch = recorded.replay;
  CHECK(!event_mismatch.expected_events.empty());
  if (!event_mismatch.expected_events.empty()) {
    event_mismatch.expected_events.front().hash ^= 1U;
    const auto result = verify_replay_v1(save_replay_v1(event_mismatch));
    if (result.error != ReplayError::EventMismatch) {
      std::cerr << "  event tamper reported: " << to_string(result.error)
                << '\n';
    }
    CHECK(result.error == ReplayError::EventMismatch);
  }

  auto checkpoint_mismatch = recorded.replay;
  CHECK(checkpoint_mismatch.checkpoints.size() > 1);
  if (checkpoint_mismatch.checkpoints.size() > 1) {
    checkpoint_mismatch.checkpoints.front().state_hash ^= 1U;
    const auto result =
        verify_replay_v1(save_replay_v1(checkpoint_mismatch));
    CHECK(result.error == ReplayError::CheckpointMismatch);
    CHECK(result.mismatch_tick ==
          checkpoint_mismatch.checkpoints.front().tick);
  }
}

void recorder_rejects_untracked_external_commands() {
  Simulation simulation{};
  ReplayRecorder recorder{simulation};
  CHECK(simulation.execute_now(
            Command{.player = PlayerId::One,
                    .type = CommandType::MakeVow,
                    .vow = kBridgeOpenVow})
            .ok);
  auto rejected = false;
  try {
    static_cast<void>(recorder.finish(simulation));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  CHECK(rejected);
}

}  // namespace

int main() {
  run_test("replay round trip and verification are bit exact",
           replay_round_trip_and_verification_are_bit_exact);
  run_test("malformed and incompatible containers are rejected",
           malformed_and_incompatible_containers_are_rejected);
  run_test("verifier reports commands, events, and checkpoints",
           verifier_reports_commands_events_and_checkpoints);
  run_test("recorder rejects untracked external commands",
           recorder_rejects_untracked_external_commands);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
