#pragma once

#include "ashen/core/Simulation.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace ashen::core {

inline constexpr std::uint32_t kSnapshotSchemaVersion = 1;
inline constexpr std::uint32_t kSnapshotMinimumReaderVersion = 1;

enum class SnapshotError : std::uint8_t {
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
  StateHashMismatch,
};

struct SnapshotHeader {
  std::uint32_t schema_version{};
  std::uint32_t minimum_reader_version{};
  std::uint64_t content_digest{};
  std::uint64_t pipeline_digest{};
  Tick checkpoint_tick{};
  std::uint64_t checkpoint_state_hash{};
  std::uint64_t payload_size{};
  std::uint64_t payload_hash{};
};

struct SnapshotLoadResult {
  SnapshotError error{SnapshotError::None};
  SnapshotHeader header{};
  std::unique_ptr<Simulation> simulation{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SnapshotError::None && simulation != nullptr;
  }
};

// SnapshotV1 is a deterministic little-endian binary format. It persists all
// authoritative state and audit history, but never derived entity/spatial indexes.
// Saving throws std::invalid_argument for state outside V1 invariants and
// std::length_error when a bounded V1 collection or payload is too large.
[[nodiscard]] ASHENCORE_API std::vector<std::uint8_t> save_snapshot_v1(
    const Simulation& simulation);
[[nodiscard]] ASHENCORE_API SnapshotLoadResult load_snapshot_v1(
    std::span<const std::uint8_t> bytes);

[[nodiscard]] ASHENCORE_API std::uint64_t current_content_digest();
[[nodiscard]] ASHENCORE_API std::uint64_t current_pipeline_digest();
[[nodiscard]] ASHENCORE_API std::string_view to_string(
    SnapshotError error) noexcept;

}  // namespace ashen::core
