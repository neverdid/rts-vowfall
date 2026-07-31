#include "ashen/core/Replay.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace ashen::core;

[[nodiscard]] bool read_file(const std::filesystem::path& path,
                             std::vector<std::uint8_t>& bytes) {
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    return false;
  }
  bytes.assign(std::istreambuf_iterator<char>{input},
               std::istreambuf_iterator<char>{});
  return input.good() || input.eof();
}

[[nodiscard]] bool write_file(const std::filesystem::path& path,
                              const std::span<const std::uint8_t> bytes) {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  if (!output) {
    return false;
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return output.good();
}

template <typename Value>
[[nodiscard]] bool parse_number(const std::string_view text, Value& value) {
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}

void print_header(const ReplayData& replay) {
  std::cout << "ReplayV1\n"
            << "  schema: " << replay.header.schema_version << '\n'
            << "  ticks: " << replay.header.initial_tick << " -> "
            << replay.header.final_tick << '\n'
            << "  initial state: 0x" << std::hex << std::setw(16)
            << std::setfill('0') << replay.header.initial_state_hash << '\n'
            << "  final state:   0x" << std::setw(16)
            << replay.header.final_state_hash << std::dec << '\n'
            << "  external inputs: " << replay.inputs.size() << '\n'
            << "  command audits: " << replay.expected_commands.size() << '\n'
            << "  event audits: " << replay.expected_events.size() << '\n'
            << "  checkpoints: " << replay.checkpoints.size() << '\n';
}

int inspect(const std::filesystem::path& path, const bool verify) {
  std::vector<std::uint8_t> bytes{};
  if (!read_file(path, bytes)) {
    std::cerr << "Could not read replay: " << path.string() << '\n';
    return 1;
  }
  auto loaded = load_replay_v1(bytes);
  if (!loaded) {
    std::cerr << "Replay rejected: " << to_string(loaded.error) << '\n';
    return 1;
  }
  print_header(*loaded.replay);
  if (!verify) {
    return 0;
  }
  auto result = verify_replay_v1(*loaded.replay);
  if (!result) {
    std::cerr << "Verification failed: " << to_string(result.error)
              << " at tick " << result.mismatch_tick << ", index "
              << result.mismatch_index << '\n';
    return 1;
  }
  std::cout << "  verification: passed\n";
  return 0;
}

int record(const std::filesystem::path& path, const Tick requested_ticks,
           const std::uint64_t seed) {
  SimulationConfig config{};
  config.match_seed = seed;
  config.commander_players = {true, true};
  Simulation simulation{config};
  ReplayRecorder recorder{simulation};

  while (simulation.tick() < requested_ticks &&
         simulation.status() == MatchStatus::Playing) {
    simulation.step();
    if (simulation.tick() % 600 == 0) {
      recorder.capture_checkpoint(simulation);
    }
  }
  const auto replay = recorder.finish(simulation);
  const auto bytes = save_replay_v1(replay);
  if (!write_file(path, bytes)) {
    std::cerr << "Could not write replay: " << path.string() << '\n';
    return 1;
  }
  print_header(replay);
  std::cout << "  bytes: " << bytes.size() << '\n'
            << "  output: " << path.string() << '\n';
  return 0;
}

void print_usage() {
  std::cerr
      << "Usage:\n"
      << "  ashen_replay record <file> [ticks=2400] [seed=1]\n"
      << "  ashen_replay inspect <file>\n"
      << "  ashen_replay verify <file>\n";
}

}  // namespace

int main(const int argc, const char* const argv[]) {
  if (argc < 3) {
    print_usage();
    return 2;
  }
  const std::string_view operation{argv[1]};
  const std::filesystem::path path{argv[2]};
  if (operation == "inspect") {
    return inspect(path, false);
  }
  if (operation == "verify") {
    return inspect(path, true);
  }
  if (operation != "record" || argc > 5) {
    print_usage();
    return 2;
  }

  Tick ticks = 2'400;
  std::uint64_t seed = 1;
  if ((argc >= 4 && !parse_number(std::string_view{argv[3]}, ticks)) ||
      (argc >= 5 && !parse_number(std::string_view{argv[4]}, seed))) {
    std::cerr << "Ticks and seed must be unsigned integers.\n";
    return 2;
  }
  try {
    return record(path, ticks, seed);
  } catch (const std::exception& error) {
    std::cerr << "Replay recording failed: " << error.what() << '\n';
    return 1;
  }
}
