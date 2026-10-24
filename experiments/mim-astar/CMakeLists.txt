cmake_minimum_required(VERSION 3.20)
project(mim_astar_experiment LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(MIM_PARENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(EXISTS "${MIM_PARENT_ROOT}/vendor/weather-routing-engine/CMakeLists.txt")
  set(MIM_DEFAULT_BASELINE "${MIM_PARENT_ROOT}")
else()
  set(MIM_DEFAULT_BASELINE "${MIM_PARENT_ROOT}/.worktrees/xweather-1.34")
endif()
set(MIM_BASELINE "${MIM_DEFAULT_BASELINE}"
    CACHE PATH "Existing six-engine source tree, never modified by this experiment")
option(MIM_SANITIZE "Enable address and undefined-behaviour sanitizers" OFF)
option(MIM_BUILD_TESTS "Build contract tests and the six-engine comparison client" ON)
if(MIM_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Unchanged shared physics and all six comparison engines. Atlas's test target
# also supplies the common instrumented fixtures, polar and optional GRIB reader.
if(MIM_BUILD_TESTS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(MIM_JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
  pkg_check_modules(MIM_ECCODES QUIET IMPORTED_TARGET eccodes)
  enable_testing()
  add_subdirectory("${MIM_BASELINE}/vendor/atlas-routing-engine" baseline)
else()
  set(WEATHER_ROUTING_ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(WEATHER_ROUTING_ENGINE_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory("${MIM_BASELINE}/vendor/weather-routing-engine" physics)
endif()
add_library(mim_astar STATIC src/Router.cpp src/SearchStorage.h)
target_compile_features(mim_astar PUBLIC cxx_std_20)
target_include_directories(mim_astar PUBLIC include
  PRIVATE "${MIM_BASELINE}/vendor/weather-routing-engine/src")
target_link_libraries(mim_astar PUBLIC weather_routing_engine)
if(MSVC)
  target_compile_options(mim_astar PRIVATE /W4 /permissive-)
else()
  target_compile_options(mim_astar PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(MIM_BUILD_TESTS)
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_runner.py"
  "${MIM_BASELINE}/vendor/atlas-routing-engine/tests/Benchmark.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/Benchmark.cpp"
  COMMAND_ERROR_IS_FATAL ANY)
add_executable(mim_benchmark "${CMAKE_CURRENT_BINARY_DIR}/Benchmark.cpp")
target_include_directories(mim_benchmark PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/tests"
  "${MIM_BASELINE}/vendor/atlas-routing-engine/tests")
target_link_libraries(mim_benchmark PRIVATE mim_astar atlas_routing_engine
  atlas_benchmark_quick2 hardened_original_engine quick_weather_routing_engine
  alternative_weather_routing_engine PkgConfig::MIM_JSONCPP)
if(MIM_ECCODES_FOUND)
  target_link_libraries(mim_benchmark PRIVATE PkgConfig::MIM_ECCODES)
  target_compile_definitions(mim_benchmark PRIVATE ATLAS_HAS_ECCODES=1)
endif()
add_executable(mim_contracts tests/Contracts.cpp)
add_executable(mim_headless_contracts tests/HeadlessContracts.cpp)
target_include_directories(mim_headless_contracts PRIVATE
  "${MIM_BASELINE}/vendor/atlas-routing-engine/tests")
target_link_libraries(mim_headless_contracts PRIVATE mim_astar atlas_routing_engine PkgConfig::MIM_JSONCPP)
add_test(NAME mim-headless-import COMMAND mim_headless_contracts)
target_include_directories(mim_contracts PRIVATE
  "${MIM_BASELINE}/vendor/atlas-routing-engine/tests")
target_link_libraries(mim_contracts PRIVATE mim_astar atlas_routing_engine PkgConfig::MIM_JSONCPP)
foreach(group analytic geography waiting acceptance resources bounds improvement stage_budgets observers concurrency)
  add_test(NAME mim-${group} COMMAND mim_contracts ${group})
  set_tests_properties(mim-${group} PROPERTIES TIMEOUT 60)
endforeach()
endif()
