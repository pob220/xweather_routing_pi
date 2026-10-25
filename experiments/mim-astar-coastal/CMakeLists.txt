cmake_minimum_required(VERSION 3.20)
project(mim_coastal_experiment LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(COASTAL_BUILD_TESTS "Build controlled comparisons and contracts" ON)
option(COASTAL_SANITIZE "Address and undefined-behaviour sanitizers" OFF)
if(COASTAL_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_runner.py"
  "${CMAKE_CURRENT_SOURCE_DIR}/../mim-astar/tools/generate_runner.py")
# Build the unchanged experiment and shared fixtures as comparison dependencies.
# Neither its source nor its own saved build directory is modified.
set(MIM_BUILD_TESTS ${COASTAL_BUILD_TESTS} CACHE BOOL "" FORCE)
set(MIM_SANITIZE OFF CACHE BOOL "" FORCE)
add_subdirectory(../mim-astar first-experiment)
get_filename_component(BASELINE "../.." ABSOLUTE)
add_library(mim_coastal STATIC src/Router.cpp src/GeographyHints.h src/SearchStorage.h)
target_compile_features(mim_coastal PUBLIC cxx_std_20)
target_include_directories(mim_coastal PUBLIC include
  PRIVATE "${BASELINE}/vendor/weather-routing-engine/src")
target_link_libraries(mim_coastal PUBLIC weather_routing_engine)
if(MSVC)
  target_compile_options(mim_coastal PRIVATE /W4 /permissive-)
else()
  target_compile_options(mim_coastal PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
if(COASTAL_BUILD_TESTS)
  enable_testing()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(COASTAL_JSON REQUIRED IMPORTED_TARGET jsoncpp)
  pkg_check_modules(COASTAL_ECCODES QUIET IMPORTED_TARGET eccodes)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_runner.py"
    "${CMAKE_CURRENT_BINARY_DIR}/first-experiment/Benchmark.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/Benchmark.cpp" COMMAND_ERROR_IS_FATAL ANY)
  add_executable(coastal_benchmark "${CMAKE_CURRENT_BINARY_DIR}/Benchmark.cpp")
  target_include_directories(coastal_benchmark PRIVATE ../mim-astar/tests
    "${BASELINE}/vendor/atlas-routing-engine/tests")
  target_link_libraries(coastal_benchmark PRIVATE mim_coastal mim_astar atlas_routing_engine
    atlas_benchmark_quick2 hardened_original_engine quick_weather_routing_engine
    alternative_weather_routing_engine PkgConfig::COASTAL_JSON)
  if(COASTAL_ECCODES_FOUND)
    target_link_libraries(coastal_benchmark PRIVATE PkgConfig::COASTAL_ECCODES)
    target_compile_definitions(coastal_benchmark PRIVATE ATLAS_HAS_ECCODES=1)
  endif()
  add_executable(coastal_contracts tests/Contracts.cpp)
  target_include_directories(coastal_contracts PRIVATE src
    "${BASELINE}/vendor/weather-routing-engine/src" "${BASELINE}/vendor/atlas-routing-engine/tests")
  target_link_libraries(coastal_contracts PRIVATE mim_coastal atlas_routing_engine PkgConfig::COASTAL_JSON)
  foreach(group analytic geography guidance waiting acceptance resources observers concurrency)
    add_test(NAME coastal-${group} COMMAND coastal_contracts ${group})
    set_tests_properties(coastal-${group} PROPERTIES TIMEOUT 45)
  endforeach()
endif()
