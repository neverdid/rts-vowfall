#include "ashen/core/Replay.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ashen::core {
namespace {

inline constexpr std::array<std::uint8_t, 8> kReplayMagic{
    'V', 'O', 'W', 'R', 'E', 'P', 'L', '1'};
inline constexpr std::uint64_t kFnvOffset =
    14'695'981'039'346'656'037ULL;
inline constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;
inline constexpr std::uint64_t kMaximumReplayPayloadBytes =
    512ULL * 1'024 * 1'024;
inline constexpr std::uint64_t kMaximumEmbeddedSnapshotBytes =
    256ULL * 1'024 * 1'024 + 64;
inline constexpr std::uint32_t kMaximumReplayRecords = 2'000'000;
inline constexpr std::uint32_t kMaximumCommandEntities = 1'000'000;

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
    if (value > kMaximumReplayRecords) {
      throw std::length_error("Replay collection exceeds the V1 limit.");
    }
    integral(static_cast<std::uint32_t>(value));
  }

  void size(const std::size_t value) {
    integral(static_cast<std::uint64_t>(value));
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
      bits = static_cast<Unsigned>(
          static_cast<std::uint64_t>(bits) |
          (static_cast<std::uint64_t>(bytes_[offset_++]) << (byte * 8U)));
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
        fail(ReplayError::InvalidData);
        return false;
      }
    }
    if (raw > static_cast<Underlying>(maximum)) {
      fail(ReplayError::InvalidData);
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
      fail(ReplayError::InvalidData);
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
    if (raw > kMaximumReplayRecords) {
      fail(ReplayError::InvalidData);
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
      fail(ReplayError::InvalidData);
      return false;
    }
    value = static_cast<std::size_t>(raw);
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

  void fail(const ReplayError error) noexcept {
    if (error_ == ReplayError::None) {
      error_ = error;
    }
  }

  [[nodiscard]] bool ok() const noexcept {
    return error_ == ReplayError::None;
  }
  [[nodiscard]] ReplayError error() const noexcept { return error_; }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - offset_;
  }

 private:
  bool require(const std::size_t count) {
    if (count > remaining()) {
      fail(ReplayError::Truncated);
      return false;
    }
    return true;
  }

  std::span<const std::uint8_t> bytes_{};
  std::size_t offset_{};
  ReplayError error_{ReplayError::None};
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

void write_vec2(Writer& writer, const Vec2 value) {
  writer.integral(value.x);
  writer.integral(value.y);
}

bool read_vec2(Reader& reader, Vec2& value) {
  return reader.integral(value.x) && reader.integral(value.y);
}

void write_command(Writer& writer, const Command& command) {
  writer.integral(command.execute_tick);
  writer.integral(command.sequence);
  writer.enumeration(command.player);
  writer.enumeration(command.type);
  if (command.entities.size() > kMaximumCommandEntities) {
    throw std::length_error("Replay command entity list exceeds the V1 limit.");
  }
  writer.integral(static_cast<std::uint32_t>(command.entities.size()));
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
  std::uint32_t entity_count{};
  if (!reader.integral(entity_count)) {
    return false;
  }
  if (entity_count > kMaximumCommandEntities) {
    reader.fail(ReplayError::InvalidData);
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
         reader.integral(command.vow.value) && reader.boolean(command.queue);
}

void write_trace(Writer& writer, const CommandTraceEntry& trace) {
  writer.integral(trace.issued_tick);
  writer.integral(trace.applied_tick);
  writer.enumeration(trace.source);
  writer.integral(trace.observation_hash);
  writer.integral(trace.ai_decision_id);
  write_command(writer, trace.command);
  writer.boolean(trace.accepted);
  writer.enumeration(trace.error);
}

bool read_trace(Reader& reader, CommandTraceEntry& trace) {
  return reader.integral(trace.issued_tick) &&
         reader.integral(trace.applied_tick) &&
         reader.enumeration(trace.source, CommandSource::CommanderAI) &&
         reader.integral(trace.observation_hash) &&
         reader.integral(trace.ai_decision_id) &&
         read_command(reader, trace.command) &&
         reader.boolean(trace.accepted) &&
         reader.enumeration(trace.error, CommandError::VowAuthorityRequired);
}

void write_input(Writer& writer, const ReplayInput& input) {
  writer.enumeration(input.submission);
  writer.integral(input.issued_tick);
  write_command(writer, input.command);
  writer.boolean(input.applied);
  writer.integral(input.applied_tick);
  writer.boolean(input.accepted);
  writer.enumeration(input.error);
}

bool read_input(Reader& reader, ReplayInput& input) {
  return reader.enumeration(input.submission, ReplaySubmission::Enqueue) &&
         reader.integral(input.issued_tick) &&
         read_command(reader, input.command) && reader.boolean(input.applied) &&
         reader.integral(input.applied_tick) &&
         reader.boolean(input.accepted) &&
         reader.enumeration(input.error, CommandError::VowAuthorityRequired);
}

void write_event_audit(Writer& writer, const ReplayEventAudit& event) {
  writer.integral(event.id.value);
  writer.integral(event.tick);
  writer.enumeration(event.type);
  writer.integral(event.hash);
}

bool read_event_audit(Reader& reader, ReplayEventAudit& event) {
  return reader.integral(event.id.value) && reader.integral(event.tick) &&
         reader.enumeration(event.type,
                            SimulationEventType::CasualtyStateChanged) &&
         reader.integral(event.hash);
}

void write_checkpoint(Writer& writer, const ReplayCheckpoint& checkpoint) {
  writer.integral(checkpoint.tick);
  writer.integral(checkpoint.input_count);
  writer.integral(checkpoint.state_hash);
  writer.integral(checkpoint.command_count);
  writer.integral(checkpoint.event_count);
  writer.integral(checkpoint.event_digest);
}

bool read_checkpoint(Reader& reader, ReplayCheckpoint& checkpoint) {
  return reader.integral(checkpoint.tick) &&
         reader.integral(checkpoint.input_count) &&
         reader.integral(checkpoint.state_hash) &&
         reader.integral(checkpoint.command_count) &&
         reader.integral(checkpoint.event_count) &&
         reader.integral(checkpoint.event_digest);
}

template <typename Value, typename Write>
void write_vector(Writer& writer, const std::vector<Value>& values,
                  Write write) {
  writer.count(values.size());
  for (const auto& value : values) {
    write(writer, value);
  }
}

template <typename Value, typename Read>
bool read_vector(Reader& reader, std::vector<Value>& values, Read read) {
  std::size_t count{};
  if (!reader.count(count)) {
    return false;
  }
  values.resize(count);
  for (auto& value : values) {
    if (!read(reader, value)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] ReplayEventAudit audit_event(const SimulationEvent& event) {
  return ReplayEventAudit{event.id, event.tick, event_type(event),
                          simulation_event_hash(event)};
}

[[nodiscard]] ReplayCheckpoint make_checkpoint(
    const Simulation& simulation, const std::size_t input_count) {
  return ReplayCheckpoint{
      simulation.tick(), static_cast<std::uint64_t>(input_count),
      simulation.state_hash(),
      static_cast<std::uint64_t>(simulation.command_trace().size()),
      static_cast<std::uint64_t>(simulation.events().size()),
      simulation.event_digest()};
}

[[nodiscard]] bool is_prefix(
    const std::vector<CommandTraceEntry>& prefix,
    const std::vector<CommandTraceEntry>& values) {
  return prefix.size() <= values.size() &&
         std::ranges::equal(prefix, values | std::views::take(prefix.size()));
}

[[nodiscard]] bool is_prefix(const std::vector<SimulationEvent>& prefix,
                             const std::vector<SimulationEvent>& values) {
  return prefix.size() <= values.size() &&
         std::ranges::equal(prefix, values | std::views::take(prefix.size()));
}

template <typename Enum>
[[nodiscard]] bool valid_enum(const Enum value, const Enum maximum) noexcept {
  using Underlying = std::underlying_type_t<Enum>;
  const auto raw = static_cast<Underlying>(value);
  if constexpr (std::is_signed_v<Underlying>) {
    if (raw < 0) {
      return false;
    }
  }
  return raw <= static_cast<Underlying>(maximum);
}

[[nodiscard]] bool valid_command(const Command& command) noexcept {
  return command.sequence != 0 &&
         command.sequence != std::numeric_limits<std::uint64_t>::max() &&
         command.entities.size() <= kMaximumCommandEntities &&
         valid_enum(command.player, PlayerId::Two) &&
         valid_enum(command.type, CommandType::AmendVow) &&
         valid_enum(command.train_type, EntityType::Turret) &&
         valid_enum(command.building_type, EntityType::Turret) &&
         valid_enum(command.research, ResearchId::SiegeLiturgy) &&
         valid_enum(command.stance, UnitStance::Hold);
}

[[nodiscard]] ReplayError validate_data(const ReplayData& replay) {
  if (replay.header.schema_version != kReplaySchemaVersion ||
      replay.header.minimum_reader_version != kReplayMinimumReaderVersion) {
    return ReplayError::UnsupportedSchema;
  }
  if (replay.header.content_digest != current_content_digest()) {
    return ReplayError::IncompatibleContent;
  }
  if (replay.header.pipeline_digest != current_pipeline_digest()) {
    return ReplayError::IncompatiblePipeline;
  }

  const auto initial = load_snapshot_v1(replay.initial_snapshot);
  if (!initial) {
    return ReplayError::InitialSnapshotRejected;
  }
  if (initial.header.checkpoint_tick != replay.header.initial_tick ||
      initial.header.checkpoint_state_hash !=
          replay.header.initial_state_hash ||
      replay.header.final_tick < replay.header.initial_tick) {
    return ReplayError::InvalidData;
  }

  Tick previous_input_tick = replay.header.initial_tick;
  std::vector<std::uint64_t> input_sequences{};
  input_sequences.reserve(replay.inputs.size());
  for (const auto& input : replay.inputs) {
    if (input.issued_tick < previous_input_tick ||
        input.issued_tick > replay.header.final_tick ||
        !valid_enum(input.submission, ReplaySubmission::Enqueue) ||
        !valid_enum(input.error, CommandError::VowAuthorityRequired) ||
        !valid_command(input.command)) {
      return ReplayError::InvalidData;
    }
    previous_input_tick = input.issued_tick;
    input_sequences.push_back(input.command.sequence);
    if (input.submission == ReplaySubmission::ExecuteNow &&
        (input.command.execute_tick != input.issued_tick || !input.applied ||
         input.applied_tick != input.issued_tick)) {
      return ReplayError::InvalidData;
    }
    if (input.applied) {
      if (input.applied_tick < input.issued_tick ||
          input.applied_tick > replay.header.final_tick ||
          (input.submission == ReplaySubmission::Enqueue &&
           input.applied_tick !=
               std::max(input.issued_tick, input.command.execute_tick)) ||
          input.accepted != (input.error == CommandError::None)) {
        return ReplayError::InvalidData;
      }
    } else if (input.submission != ReplaySubmission::Enqueue ||
               input.applied_tick != 0 || input.accepted ||
               input.error != CommandError::None) {
      return ReplayError::InvalidData;
    }
  }
  std::ranges::sort(input_sequences);
  if (std::ranges::adjacent_find(input_sequences) != input_sequences.end()) {
    return ReplayError::InvalidData;
  }

  std::size_t applied_external_count{};
  for (const auto& trace : replay.expected_commands) {
    if (trace.issued_tick < replay.header.initial_tick ||
        trace.issued_tick > trace.applied_tick ||
        trace.applied_tick > replay.header.final_tick ||
        !valid_enum(trace.source, CommandSource::CommanderAI) ||
        !valid_enum(trace.error, CommandError::VowAuthorityRequired) ||
        !valid_command(trace.command) ||
        trace.accepted != (trace.error == CommandError::None)) {
      return ReplayError::InvalidData;
    }
    if (trace.source == CommandSource::External) {
      const auto input = std::ranges::find(
          replay.inputs, trace.command.sequence,
          [](const ReplayInput& value) { return value.command.sequence; });
      if (input == replay.inputs.end() || !input->applied ||
          input->issued_tick != trace.issued_tick ||
          input->applied_tick != trace.applied_tick ||
          input->accepted != trace.accepted || input->error != trace.error ||
          input->command != trace.command) {
        return ReplayError::InvalidData;
      }
      ++applied_external_count;
    }
  }
  if (applied_external_count != static_cast<std::size_t>(
                                    std::ranges::count(
                                        replay.inputs, true,
                                        &ReplayInput::applied))) {
    return ReplayError::InvalidData;
  }

  auto next_event_id = initial.simulation->events().empty()
                           ? std::uint64_t{1}
                           : initial.simulation->events().back().id.value + 1;
  for (const auto& event : replay.expected_events) {
    if (event.id.value != next_event_id ||
        event.tick < replay.header.initial_tick ||
        event.tick > replay.header.final_tick ||
        !valid_enum(event.type, SimulationEventType::CasualtyStateChanged)) {
      return ReplayError::InvalidData;
    }
    ++next_event_id;
  }

  if (replay.checkpoints.empty()) {
    return ReplayError::InvalidData;
  }
  auto previous_key = std::tuple{replay.header.initial_tick,
                                 std::uint64_t{0}};
  for (const auto& checkpoint : replay.checkpoints) {
    const auto key = std::tuple{checkpoint.tick, checkpoint.input_count};
    if (key < previous_key || checkpoint.tick < replay.header.initial_tick ||
        checkpoint.tick > replay.header.final_tick ||
        checkpoint.input_count > replay.inputs.size() ||
        checkpoint.command_count < initial.simulation->command_trace().size() ||
        checkpoint.command_count > initial.simulation->command_trace().size() +
                                       replay.expected_commands.size() ||
        checkpoint.event_count < initial.simulation->events().size() ||
        checkpoint.event_count > initial.simulation->events().size() +
                                     replay.expected_events.size()) {
      return ReplayError::InvalidData;
    }
    previous_key = key;
  }
  const auto& final = replay.checkpoints.back();
  if (final.tick != replay.header.final_tick ||
      final.input_count != replay.inputs.size() ||
      final.state_hash != replay.header.final_state_hash ||
      final.command_count != initial.simulation->command_trace().size() +
                                 replay.expected_commands.size() ||
      final.event_count != initial.simulation->events().size() +
                               replay.expected_events.size()) {
    return ReplayError::InvalidData;
  }
  return ReplayError::None;
}

[[nodiscard]] bool checkpoint_matches(const ReplayCheckpoint& expected,
                                      const Simulation& simulation) {
  return expected.tick == simulation.tick() &&
         expected.state_hash == simulation.state_hash() &&
         expected.command_count == simulation.command_trace().size() &&
         expected.event_count == simulation.events().size() &&
         expected.event_digest == simulation.event_digest();
}

[[nodiscard]] ReplayVerificationResult verification_failure(
    const ReplayData& replay, const ReplayError error, const Tick tick,
    const std::size_t index, const std::uint64_t expected = 0,
    const std::uint64_t actual = 0) {
  ReplayVerificationResult result{};
  result.error = error;
  result.header = replay.header;
  result.mismatch_tick = tick;
  result.mismatch_index = static_cast<std::uint64_t>(index);
  result.expected = expected;
  result.actual = actual;
  return result;
}

}  // namespace

ReplayRecorder::ReplayRecorder(const Simulation& initial)
    : initial_snapshot_(save_snapshot_v1(initial)),
      initial_tick_(initial.tick()),
      initial_state_hash_(initial.state_hash()),
      initial_commands_(initial.command_trace()),
      initial_events_(initial.events()) {}

CommandResult ReplayRecorder::execute_now(Simulation& simulation,
                                          Command command) {
  if (simulation.tick() < initial_tick_) {
    throw std::invalid_argument("Replay simulation predates its checkpoint.");
  }
  const auto result = simulation.execute_now(std::move(command));
  if (simulation.command_trace().empty()) {
    throw std::logic_error("Immediate command did not produce an audit record.");
  }
  const auto& trace = simulation.command_trace().back();
  if (trace.source != CommandSource::External ||
      trace.issued_tick != simulation.tick()) {
    throw std::logic_error("Immediate replay command has invalid provenance.");
  }
  inputs_.push_back(ReplayInput{
      ReplaySubmission::ExecuteNow, trace.issued_tick, trace.command, true,
      trace.applied_tick, trace.accepted, trace.error});
  return result;
}

std::uint64_t ReplayRecorder::enqueue(Simulation& simulation,
                                      Command command) {
  if (simulation.tick() < initial_tick_) {
    throw std::invalid_argument("Replay simulation predates its checkpoint.");
  }
  const auto issued_tick = simulation.tick();
  const auto sequence = simulation.enqueue(command);
  command.sequence = sequence;
  inputs_.push_back(ReplayInput{ReplaySubmission::Enqueue, issued_tick,
                                std::move(command), false, 0, false,
                                CommandError::None});
  return sequence;
}

void ReplayRecorder::capture_checkpoint(const Simulation& simulation) {
  if (simulation.tick() < initial_tick_) {
    throw std::invalid_argument("Replay simulation predates its checkpoint.");
  }
  const auto checkpoint = make_checkpoint(simulation, inputs_.size());
  if (!checkpoints_.empty() &&
      std::tuple{checkpoint.tick, checkpoint.input_count} <
          std::tuple{checkpoints_.back().tick,
                     checkpoints_.back().input_count}) {
    throw std::invalid_argument("Replay checkpoints must be chronological.");
  }
  checkpoints_.push_back(checkpoint);
}

ReplayData ReplayRecorder::finish(const Simulation& simulation) const {
  if (simulation.tick() < initial_tick_ ||
      !is_prefix(initial_commands_, simulation.command_trace()) ||
      !is_prefix(initial_events_, simulation.events())) {
    throw std::invalid_argument(
        "Final simulation does not continue the recorded checkpoint.");
  }

  ReplayData replay{};
  replay.header = ReplayHeader{
      kReplaySchemaVersion,
      kReplayMinimumReaderVersion,
      current_content_digest(),
      current_pipeline_digest(),
      initial_tick_,
      simulation.tick(),
      initial_state_hash_,
      simulation.state_hash(),
      0,
      0,
  };
  replay.initial_snapshot = initial_snapshot_;
  replay.inputs = inputs_;
  replay.expected_commands.assign(
      simulation.command_trace().begin() +
          static_cast<std::ptrdiff_t>(initial_commands_.size()),
      simulation.command_trace().end());
  replay.expected_events.reserve(simulation.events().size() -
                                 initial_events_.size());
  for (auto event = simulation.events().begin() +
                    static_cast<std::ptrdiff_t>(initial_events_.size());
       event != simulation.events().end(); ++event) {
    replay.expected_events.push_back(audit_event(*event));
  }

  for (auto& input : replay.inputs) {
    const auto trace = std::ranges::find(
        replay.expected_commands, input.command.sequence,
        [](const CommandTraceEntry& value) {
          return value.source == CommandSource::External
                     ? value.command.sequence
                     : std::uint64_t{0};
        });
    if (trace == replay.expected_commands.end()) {
      if (input.submission == ReplaySubmission::ExecuteNow) {
        throw std::invalid_argument(
            "Recorded immediate command is missing from the final audit.");
      }
      continue;
    }
    input.applied = true;
    input.applied_tick = trace->applied_tick;
    input.accepted = trace->accepted;
    input.error = trace->error;
  }

  const auto applied_inputs =
      std::ranges::count(replay.inputs, true, &ReplayInput::applied);
  const auto external_commands = std::ranges::count(
      replay.expected_commands, CommandSource::External,
      &CommandTraceEntry::source);
  if (applied_inputs != external_commands) {
    throw std::invalid_argument(
        "External commands bypassed ReplayRecorder after its checkpoint.");
  }

  replay.checkpoints = checkpoints_;
  const auto final_checkpoint = make_checkpoint(simulation, inputs_.size());
  if (!replay.checkpoints.empty() &&
      std::tuple{replay.checkpoints.back().tick,
                 replay.checkpoints.back().input_count} ==
          std::tuple{final_checkpoint.tick, final_checkpoint.input_count}) {
    if (replay.checkpoints.back() != final_checkpoint) {
      throw std::invalid_argument(
          "Final state changed after the last replay checkpoint boundary.");
    }
  } else {
    replay.checkpoints.push_back(final_checkpoint);
  }

  const auto validation = validate_data(replay);
  if (validation != ReplayError::None) {
    throw std::invalid_argument("Recorded replay violates ReplayV1 invariants.");
  }
  return replay;
}

std::vector<std::uint8_t> save_replay_v1(const ReplayData& replay) {
  const auto validation = validate_data(replay);
  if (validation != ReplayError::None) {
    throw std::invalid_argument("Replay data violates ReplayV1 invariants.");
  }

  Writer payload_writer;
  payload_writer.size(replay.initial_snapshot.size());
  payload_writer.append(replay.initial_snapshot);
  write_vector(payload_writer, replay.inputs, write_input);
  write_vector(payload_writer, replay.expected_commands, write_trace);
  write_vector(payload_writer, replay.expected_events, write_event_audit);
  write_vector(payload_writer, replay.checkpoints, write_checkpoint);
  const auto& payload = payload_writer.bytes();
  if (payload.size() > kMaximumReplayPayloadBytes) {
    throw std::length_error("Replay payload exceeds the V1 limit.");
  }

  const ReplayHeader header{
      kReplaySchemaVersion,
      kReplayMinimumReaderVersion,
      current_content_digest(),
      current_pipeline_digest(),
      replay.header.initial_tick,
      replay.header.final_tick,
      replay.header.initial_state_hash,
      replay.header.final_state_hash,
      payload.size(),
      hash_bytes(payload),
  };

  Writer writer;
  writer.append(kReplayMagic);
  writer.integral(header.schema_version);
  writer.integral(header.minimum_reader_version);
  writer.integral(header.content_digest);
  writer.integral(header.pipeline_digest);
  writer.integral(header.initial_tick);
  writer.integral(header.final_tick);
  writer.integral(header.initial_state_hash);
  writer.integral(header.final_state_hash);
  writer.integral(header.payload_size);
  writer.integral(header.payload_hash);
  writer.append(payload);
  return std::move(writer).release();
}

ReplayLoadResult load_replay_v1(const std::span<const std::uint8_t> bytes) {
  ReplayLoadResult result{};
  if (bytes.empty()) {
    result.error = ReplayError::EmptyInput;
    return result;
  }

  Reader reader{bytes};
  const auto magic = reader.take(kReplayMagic.size());
  if (!reader.ok()) {
    result.error = reader.error();
    return result;
  }
  if (!std::ranges::equal(magic, kReplayMagic)) {
    result.error = ReplayError::BadMagic;
    return result;
  }
  if (!reader.integral(result.header.schema_version) ||
      !reader.integral(result.header.minimum_reader_version) ||
      !reader.integral(result.header.content_digest) ||
      !reader.integral(result.header.pipeline_digest) ||
      !reader.integral(result.header.initial_tick) ||
      !reader.integral(result.header.final_tick) ||
      !reader.integral(result.header.initial_state_hash) ||
      !reader.integral(result.header.final_state_hash) ||
      !reader.integral(result.header.payload_size) ||
      !reader.integral(result.header.payload_hash)) {
    result.error = reader.error();
    return result;
  }
  if (result.header.schema_version != kReplaySchemaVersion ||
      result.header.minimum_reader_version > kReplaySchemaVersion) {
    result.error = ReplayError::UnsupportedSchema;
    return result;
  }
  if (result.header.content_digest != current_content_digest()) {
    result.error = ReplayError::IncompatibleContent;
    return result;
  }
  if (result.header.pipeline_digest != current_pipeline_digest()) {
    result.error = ReplayError::IncompatiblePipeline;
    return result;
  }
  if (result.header.payload_size > kMaximumReplayPayloadBytes ||
      result.header.payload_size >
          static_cast<std::uint64_t>(
              std::numeric_limits<std::size_t>::max())) {
    result.error = ReplayError::PayloadTooLarge;
    return result;
  }

  const auto payload_size =
      static_cast<std::size_t>(result.header.payload_size);
  if (payload_size > reader.remaining()) {
    result.error = ReplayError::Truncated;
    return result;
  }
  if (payload_size < reader.remaining()) {
    result.error = ReplayError::TrailingData;
    return result;
  }
  const auto payload = reader.take(payload_size);
  if (hash_bytes(payload) != result.header.payload_hash) {
    result.error = ReplayError::ChecksumMismatch;
    return result;
  }

  Reader payload_reader{payload};
  std::unique_ptr<ReplayData> replay{};
  try {
    replay = std::make_unique<ReplayData>();
    replay->header = result.header;
    std::size_t snapshot_size{};
    if (!payload_reader.size(snapshot_size)) {
      result.error = payload_reader.error();
      return result;
    }
    if (snapshot_size > kMaximumEmbeddedSnapshotBytes) {
      result.error = ReplayError::InvalidData;
      return result;
    }
    const auto snapshot = payload_reader.take(snapshot_size);
    if (!payload_reader.ok()) {
      result.error = payload_reader.error();
      return result;
    }
    replay->initial_snapshot.assign(snapshot.begin(), snapshot.end());
    if (!read_vector(payload_reader, replay->inputs, read_input) ||
        !read_vector(payload_reader, replay->expected_commands, read_trace) ||
        !read_vector(payload_reader, replay->expected_events,
                     read_event_audit) ||
        !read_vector(payload_reader, replay->checkpoints, read_checkpoint)) {
      result.error = payload_reader.error();
      return result;
    }
  } catch (const std::bad_alloc&) {
    result.error = ReplayError::InvalidData;
    return result;
  } catch (const std::length_error&) {
    result.error = ReplayError::InvalidData;
    return result;
  }
  if (payload_reader.remaining() != 0) {
    result.error = ReplayError::TrailingData;
    return result;
  }
  const auto validation = validate_data(*replay);
  if (validation != ReplayError::None) {
    result.error = validation;
    return result;
  }
  result.replay = std::move(replay);
  return result;
}

ReplayVerificationResult verify_replay_v1(const ReplayData& replay) {
  const auto validation = validate_data(replay);
  if (validation != ReplayError::None) {
    return verification_failure(replay, validation, replay.header.initial_tick,
                                0);
  }
  auto initial = load_snapshot_v1(replay.initial_snapshot);
  if (!initial) {
    return verification_failure(replay, ReplayError::InitialSnapshotRejected,
                                replay.header.initial_tick, 0);
  }

  auto simulation = std::move(initial.simulation);
  const auto initial_command_count = simulation->command_trace().size();
  const auto initial_event_count = simulation->events().size();
  std::size_t input_index{};
  std::size_t checkpoint_index{};

  const auto check_due_checkpoints = [&]() -> ReplayVerificationResult {
    while (checkpoint_index < replay.checkpoints.size()) {
      const auto& checkpoint = replay.checkpoints[checkpoint_index];
      if (checkpoint.tick != simulation->tick() ||
          checkpoint.input_count != input_index) {
        break;
      }
      if (!checkpoint_matches(checkpoint, *simulation)) {
        return verification_failure(
            replay, ReplayError::CheckpointMismatch, simulation->tick(),
            checkpoint_index, checkpoint.state_hash,
            simulation->state_hash());
      }
      ++checkpoint_index;
    }
    ReplayVerificationResult ok{};
    ok.header = replay.header;
    ok.error = ReplayError::None;
    return ok;
  };

  while (true) {
    auto checkpoint_result = check_due_checkpoints();
    if (checkpoint_result.error != ReplayError::None) {
      return checkpoint_result;
    }
    while (input_index < replay.inputs.size() &&
           replay.inputs[input_index].issued_tick == simulation->tick()) {
      const auto& input = replay.inputs[input_index];
      if (input.submission == ReplaySubmission::ExecuteNow) {
        const auto result = simulation->execute_now(input.command);
        if (result.ok != input.accepted || result.error != input.error) {
          return verification_failure(
              replay, ReplayError::CommandMismatch, simulation->tick(),
              input_index, static_cast<std::uint64_t>(input.error),
              static_cast<std::uint64_t>(result.error));
        }
      } else {
        const auto sequence = simulation->enqueue(input.command);
        if (sequence != input.command.sequence) {
          return verification_failure(replay, ReplayError::TimelineMismatch,
                                      simulation->tick(), input_index,
                                      input.command.sequence, sequence);
        }
      }
      ++input_index;
      checkpoint_result = check_due_checkpoints();
      if (checkpoint_result.error != ReplayError::None) {
        return checkpoint_result;
      }
    }

    if (simulation->tick() == replay.header.final_tick) {
      break;
    }
    if (simulation->tick() > replay.header.final_tick ||
        simulation->status() != MatchStatus::Playing) {
      return verification_failure(replay, ReplayError::TimelineMismatch,
                                  simulation->tick(), input_index,
                                  replay.header.final_tick,
                                  simulation->tick());
    }
    const auto prior_tick = simulation->tick();
    simulation->step();
    if (simulation->tick() == prior_tick) {
      return verification_failure(replay, ReplayError::TimelineMismatch,
                                  prior_tick, input_index,
                                  replay.header.final_tick, prior_tick);
    }
  }

  if (input_index != replay.inputs.size() ||
      checkpoint_index != replay.checkpoints.size()) {
    return verification_failure(replay, ReplayError::TimelineMismatch,
                                simulation->tick(), input_index,
                                replay.inputs.size(), input_index);
  }

  const auto actual_command_count =
      simulation->command_trace().size() - initial_command_count;
  if (actual_command_count != replay.expected_commands.size()) {
    return verification_failure(
        replay, ReplayError::CommandMismatch, simulation->tick(),
        std::min(actual_command_count, replay.expected_commands.size()),
        replay.expected_commands.size(), actual_command_count);
  }
  for (std::size_t index = 0; index < actual_command_count; ++index) {
    if (simulation->command_trace()[initial_command_count + index] !=
        replay.expected_commands[index]) {
      return verification_failure(replay, ReplayError::CommandMismatch,
                                  simulation->tick(), index);
    }
  }

  const auto actual_event_count = simulation->events().size() - initial_event_count;
  if (actual_event_count != replay.expected_events.size()) {
    return verification_failure(
        replay, ReplayError::EventMismatch, simulation->tick(),
        std::min(actual_event_count, replay.expected_events.size()),
        replay.expected_events.size(), actual_event_count);
  }
  for (std::size_t index = 0; index < actual_event_count; ++index) {
    const auto actual = audit_event(
        simulation->events()[initial_event_count + index]);
    if (actual != replay.expected_events[index]) {
      return verification_failure(replay, ReplayError::EventMismatch,
                                  actual.tick, index,
                                  replay.expected_events[index].hash,
                                  actual.hash);
    }
  }

  if (simulation->tick() != replay.header.final_tick ||
      simulation->state_hash() != replay.header.final_state_hash) {
    return verification_failure(replay, ReplayError::FinalStateMismatch,
                                simulation->tick(), 0,
                                replay.header.final_state_hash,
                                simulation->state_hash());
  }

  ReplayVerificationResult result{};
  result.header = replay.header;
  result.simulation = std::move(simulation);
  return result;
}

ReplayVerificationResult verify_replay_v1(
    const std::span<const std::uint8_t> bytes) {
  auto loaded = load_replay_v1(bytes);
  if (!loaded) {
    ReplayVerificationResult result{};
    result.error = loaded.error;
    result.header = loaded.header;
    return result;
  }
  return verify_replay_v1(*loaded.replay);
}

std::string_view to_string(const ReplayError error) noexcept {
  switch (error) {
    case ReplayError::None:
      return "none";
    case ReplayError::EmptyInput:
      return "empty input";
    case ReplayError::BadMagic:
      return "bad magic";
    case ReplayError::UnsupportedSchema:
      return "unsupported schema";
    case ReplayError::IncompatibleContent:
      return "incompatible content";
    case ReplayError::IncompatiblePipeline:
      return "incompatible pipeline";
    case ReplayError::Truncated:
      return "truncated";
    case ReplayError::TrailingData:
      return "trailing data";
    case ReplayError::PayloadTooLarge:
      return "payload too large";
    case ReplayError::ChecksumMismatch:
      return "checksum mismatch";
    case ReplayError::InvalidData:
      return "invalid data";
    case ReplayError::InitialSnapshotRejected:
      return "initial snapshot rejected";
    case ReplayError::TimelineMismatch:
      return "timeline mismatch";
    case ReplayError::CommandMismatch:
      return "command mismatch";
    case ReplayError::EventMismatch:
      return "event mismatch";
    case ReplayError::CheckpointMismatch:
      return "checkpoint mismatch";
    case ReplayError::FinalStateMismatch:
      return "final state mismatch";
  }
  return "unknown";
}

}  // namespace ashen::core
