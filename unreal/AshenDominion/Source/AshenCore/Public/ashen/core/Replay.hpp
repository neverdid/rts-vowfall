#pragma once

#include "ashen/core/Simulation.hpp"
#include "ashen/core/Snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace ashen::core {

inline constexpr std::uint32_t kReplaySchemaVersion = 3;
inline constexpr std::uint32_t kReplayMinimumReaderVersion = 3;

enum class ReplaySubmission : std::uint8_t { ExecuteNow, Enqueue };

enum class ReplayError : std::uint8_t {
  None,
  EmptyInput,
  BadMagic,
  UnsupportedSchema,
  IncompatibleContent,
  IncompatiblePipeline,
  Truncated,
  TrailingData,
  PayloadTooLarge,
  ChecksumMismatch,
  InvalidData,
  InitialSnapshotRejected,
  TimelineMismatch,
  CommandMismatch,
  EventMismatch,
  CheckpointMismatch,
  FinalStateMismatch,
};

struct ReplayHeader {
  std::uint32_t schema_version{};
  std::uint32_t minimum_reader_version{};
  std::uint64_t content_digest{};
  std::uint64_t pipeline_digest{};
  Tick initial_tick{};
  Tick final_tick{};
  std::uint64_t initial_state_hash{};
  std::uint64_t final_state_hash{};
  std::uint64_t payload_size{};
  std::uint64_t payload_hash{};
};

struct ReplayInput {
  ReplaySubmission submission{ReplaySubmission::ExecuteNow};
  Tick issued_tick{};
  Command command{};
  bool applied{};
  Tick applied_tick{};
  bool accepted{};
  CommandError error{CommandError::None};

  auto operator<=>(const ReplayInput&) const = default;
};

// Events are verification evidence, never authoritative replay input. The verifier
// regenerates every event and compares its stable identity, type, and full hash.
struct ReplayEventAudit {
  EventId id{};
  Tick tick{};
  SimulationEventType type{SimulationEventType::EntitySpawned};
  std::uint64_t hash{};

  auto operator<=>(const ReplayEventAudit&) const = default;
};

// input_count places a checkpoint precisely between external submissions that share
// a tick. This avoids an ambiguous "before or after input" checkpoint boundary.
struct ReplayCheckpoint {
  Tick tick{};
  std::uint64_t input_count{};
  std::uint64_t state_hash{};
  std::uint64_t command_count{};
  std::uint64_t event_count{};
  std::uint64_t event_digest{};

  auto operator<=>(const ReplayCheckpoint&) const = default;
};

struct ReplayData {
  ReplayHeader header{};
  std::vector<std::uint8_t> initial_snapshot{};
  std::vector<ReplayInput> inputs{};
  std::vector<CommandTraceEntry> expected_commands{};
  std::vector<ReplayEventAudit> expected_events{};
  std::vector<ReplayCheckpoint> checkpoints{};
};

struct ReplayLoadResult {
  ReplayError error{ReplayError::None};
  ReplayHeader header{};
  std::unique_ptr<ReplayData> replay{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == ReplayError::None && replay != nullptr;
  }
};

struct ReplayVerificationResult {
  ReplayError error{ReplayError::None};
  ReplayHeader header{};
  Tick mismatch_tick{};
  std::uint64_t mismatch_index{};
  std::uint64_t expected{};
  std::uint64_t actual{};
  std::unique_ptr<Simulation> simulation{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == ReplayError::None && simulation != nullptr;
  }
};

// ReplayRecorder must wrap every external submission after construction. AI commands
// remain inside Simulation and are regenerated from the restored, fog-limited state.
class ASHENCORE_API ReplayRecorder final {
 public:
  explicit ReplayRecorder(const Simulation& initial);

  [[nodiscard]] CommandResult execute_now(Simulation& simulation,
                                          Command command);
  [[nodiscard]] std::uint64_t enqueue(Simulation& simulation,
                                      Command command);
  void capture_checkpoint(const Simulation& simulation);
  [[nodiscard]] ReplayData finish(const Simulation& simulation) const;

 private:
  std::vector<std::uint8_t> initial_snapshot_{};
  Tick initial_tick_{};
  std::uint64_t initial_state_hash_{};
  std::vector<CommandTraceEntry> initial_commands_{};
  std::vector<SimulationEvent> initial_events_{};
  std::vector<ReplayInput> inputs_{};
  std::vector<ReplayCheckpoint> checkpoints_{};
};

[[nodiscard]] ASHENCORE_API std::vector<std::uint8_t> save_replay_v1(
    const ReplayData& replay);
[[nodiscard]] ASHENCORE_API ReplayLoadResult load_replay_v1(
    std::span<const std::uint8_t> bytes);
[[nodiscard]] ASHENCORE_API ReplayVerificationResult verify_replay_v1(
    const ReplayData& replay);
[[nodiscard]] ASHENCORE_API ReplayVerificationResult verify_replay_v1(
    std::span<const std::uint8_t> bytes);
[[nodiscard]] ASHENCORE_API std::string_view to_string(
    ReplayError error) noexcept;

}  // namespace ashen::core
