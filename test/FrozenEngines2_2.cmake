cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED SOURCE_ROOT)
  get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
file(STRINGS "${SOURCE_ROOT}/testdata/engine-freeze-2.2.sha256" entries)
set(expected_names)
foreach(entry IN LISTS entries)
  if(entry MATCHES "^([0-9a-f]+)  (.+)$")
    set(expected "${CMAKE_MATCH_1}")
    set(name "${CMAKE_MATCH_2}")
    if(NOT EXISTS "${SOURCE_ROOT}/${name}")
      message(FATAL_ERROR "Frozen production engine input was removed: ${name}")
    endif()
    file(SHA256 "${SOURCE_ROOT}/${name}" actual)
    if(name STREQUAL "src/engine/native/OpenCpnAdapter.cpp")
      # 2.3.2's sole adapter exception preserves reporting metadata when a stop
      # clears the delivered route. Invert that exact change before hashing;
      # The 1.37/2.4 qualification records the authorised source-reporting,
      # forecast-boundary and optional completion-orchestration adapter.
      # Vendor solvers remain unchanged. The controller additionally qualifies
      # an opt-in portfolio handoff after repeated chronological repair failures.
      # The qualified PartialRoutes helper adds an optional pre-copy distance
      # filter; all solver .cpp files retain their original hashes.
      file(READ "${SOURCE_ROOT}/${name}" adapter)
      string(REGEX MATCHALL "weather_routing::CancelRoutingResultKeepingDiagnostics\\(result\\)" reporting_calls "${adapter}")
      list(LENGTH reporting_calls reporting_call_count)
      if(NOT reporting_call_count EQUAL 1)
        message(FATAL_ERROR "Expected exactly one qualified cancellation-reporting exception")
      endif()
      string(REPLACE
        "    weather_routing::CancelRoutingResultKeepingDiagnostics(result);"
        "    result = {};\n    result.status = wr::RoutingStatus::Cancelled;\n    result.message = \"Routing cancelled\";"
        adapter "${adapter}")
      string(SHA256 actual "${adapter}")
    endif()
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "2.2 must preserve the existing engines: ${name}")
    endif()
    list(APPEND expected_names "${name}")
  endif()
endforeach()
foreach(scope vendor third_party src/engine src/headless)
  file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${SOURCE_ROOT}" "${SOURCE_ROOT}/${scope}/*")
  foreach(name IN LISTS files)
    if(NOT name IN_LIST expected_names)
      message(FATAL_ERROR "New production engine input is outside the 2.2 freeze: ${name}")
    endif()
  endforeach()
endforeach()
list(LENGTH expected_names count)
message(STATUS "All ${count} qualified calculation inputs match; exact adapter reporting exception verified")
