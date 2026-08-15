set(ASHEN_CORE_MODULE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/../unreal/AshenDominion/Source/AshenCore
)

add_library(ashen_core STATIC
  ${ASHEN_CORE_MODULE_DIR}/Private/AIDecision.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/AIDifficulty.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/AIDoctrine.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/AIInfluenceMap.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/AIStrategyState.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/AIMicroLayer.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/AIPlanning.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/AIStrategicLayer.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/AITacticalLayer.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/Campaign.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/CasualtySystem.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/Catalog.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/Content.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/CommanderAI.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/ObjectiveSystem.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/PlayerObservation.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/Replay.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/ResolveSystem.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/Simulation.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/SimulationEvent.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/Scenario.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/Snapshot.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/SpatialGrid.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/SupplySystem.cpp
  ${ASHEN_CORE_MODULE_DIR}/Private/VisibilityGrid.cpp
)

target_include_directories(ashen_core
  PUBLIC
    ${ASHEN_CORE_MODULE_DIR}/Public
)

target_compile_features(ashen_core PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(ashen_core PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(ashen_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(ashen_headless tools/headless_main.cpp)
target_link_libraries(ashen_headless PRIVATE ashen_core)

add_executable(ashen_replay tools/replay_main.cpp)
target_link_libraries(ashen_replay PRIVATE ashen_core)

add_library(ashen_benchmarks STATIC benchmark/SelfPlay.cpp)
target_include_directories(ashen_benchmarks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ashen_benchmarks PUBLIC ashen_core)
target_compile_features(ashen_benchmarks PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(ashen_benchmarks PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(ashen_benchmarks PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(ashen_self_play tools/self_play_main.cpp)
target_link_libraries(ashen_self_play PRIVATE ashen_benchmarks)

add_executable(ashen_simulation_performance benchmark/SimulationPerformance.cpp)
target_link_libraries(ashen_simulation_performance PRIVATE ashen_core)
target_compile_features(ashen_simulation_performance PRIVATE cxx_std_20)

if(ASHEN_BUILD_TESTS AND BUILD_TESTING)
  add_executable(ashen_core_tests tests/SimulationTests.cpp)
  target_link_libraries(ashen_core_tests PRIVATE ashen_core)
  add_test(NAME ashen_core_tests COMMAND ashen_core_tests)

  add_executable(ashen_campaign_tests tests/CampaignTests.cpp)
  target_link_libraries(ashen_campaign_tests PRIVATE ashen_core)
  add_test(NAME ashen_campaign_tests COMMAND ashen_campaign_tests)

  add_executable(ashen_commander_ai_tests tests/CommanderAITests.cpp)
  target_link_libraries(ashen_commander_ai_tests PRIVATE ashen_core)
  add_test(NAME ashen_commander_ai_tests COMMAND ashen_commander_ai_tests)

  add_executable(ashen_faction_ai_tests tests/FactionAITests.cpp)
  target_link_libraries(ashen_faction_ai_tests PRIVATE ashen_core)
  add_test(NAME ashen_faction_ai_tests COMMAND ashen_faction_ai_tests)

  add_executable(ashen_influence_map_tests tests/InfluenceMapTests.cpp)
  target_link_libraries(ashen_influence_map_tests PRIVATE ashen_core)
  add_test(NAME ashen_influence_map_tests COMMAND ashen_influence_map_tests)

  add_executable(ashen_difficulty_ai_tests tests/DifficultyAITests.cpp)
  target_link_libraries(ashen_difficulty_ai_tests PRIVATE ashen_core)
  add_test(NAME ashen_difficulty_ai_tests COMMAND ashen_difficulty_ai_tests)

  add_executable(ashen_self_play_tests tests/SelfPlayTests.cpp)
  target_link_libraries(ashen_self_play_tests PRIVATE ashen_benchmarks)
  add_test(NAME ashen_self_play_tests COMMAND ashen_self_play_tests)

  add_executable(ashen_foundation_tests tests/FoundationTests.cpp)
  target_link_libraries(ashen_foundation_tests PRIVATE ashen_core)
  add_test(NAME ashen_foundation_tests COMMAND ashen_foundation_tests)

  add_executable(ashen_snapshot_tests tests/SnapshotTests.cpp)
  target_link_libraries(ashen_snapshot_tests PRIVATE ashen_core)
  add_test(NAME ashen_snapshot_tests COMMAND ashen_snapshot_tests)

  add_executable(ashen_replay_tests tests/ReplayTests.cpp)
  target_link_libraries(ashen_replay_tests PRIVATE ashen_core)
  add_test(NAME ashen_replay_tests COMMAND ashen_replay_tests)

  add_executable(ashen_scenario_tests tests/ScenarioTests.cpp)
  target_link_libraries(ashen_scenario_tests PRIVATE ashen_core)
  add_test(NAME ashen_scenario_tests COMMAND ashen_scenario_tests)

  add_executable(ashen_supply_tests tests/SupplyTests.cpp)
  target_link_libraries(ashen_supply_tests PRIVATE ashen_core)
  add_test(NAME ashen_supply_tests COMMAND ashen_supply_tests)

  add_executable(ashen_casualty_tests tests/CasualtyTests.cpp)
  target_link_libraries(ashen_casualty_tests PRIVATE ashen_core)
  add_test(NAME ashen_casualty_tests COMMAND ashen_casualty_tests)

  add_executable(ashen_parity_runner tools/parity_runner.cpp)
  target_link_libraries(ashen_parity_runner PRIVATE ashen_core)
  add_test(NAME ashen_parity_catalog COMMAND ashen_parity_runner --catalog)

  add_test(
    NAME ashen_self_play_fixed_suite
    COMMAND ashen_self_play
      --seeds 2
      --output ${CMAKE_BINARY_DIR}/self-play-report.json
  )

  if(MINGW)
    get_filename_component(ASHEN_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set_tests_properties(
      ashen_core_tests
      ashen_campaign_tests
      ashen_commander_ai_tests
      ashen_faction_ai_tests
      ashen_influence_map_tests
      ashen_difficulty_ai_tests
      ashen_self_play_tests
      ashen_foundation_tests
      ashen_snapshot_tests
      ashen_replay_tests
      ashen_scenario_tests
      ashen_supply_tests
      ashen_casualty_tests
      ashen_parity_catalog
      ashen_self_play_fixed_suite
      PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${ASHEN_MINGW_BIN_DIR}"
    )
  endif()
endif()
