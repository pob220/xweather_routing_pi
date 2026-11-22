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
      # The 1.41/2.8 coordinator also drains completions arriving during
      # publication; the individual search solvers remain unchanged.
      # 2.8 qualifies ordered parallel evaluation in the vendor solver inputs. The controller additionally qualifies
      # an opt-in portfolio handoff after repeated chronological repair failures.
      # The qualified PartialRoutes helper adds an optional pre-copy distance
      # filter; unrelated solver inputs retain their original hashes.
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
      # 2.9 removes unused polar/heading payloads only from the private land
      # and weather query templates. Normalize those exact constructor bodies;
      # every other adapter and solver byte remains covered by the manifest.
      set(_query_weather_new [=[        sharedCache_(shared ? std::move(shared) : SharedWeatherCacheFor(configuration_,overlay.AdditionalCalculationHelpersEnabled())) {
    // Weather queries need service settings, not boat performance tables or
    // search headings. Preserve independent mutable per-query snapshots.
    configuration_.boat.Polars.clear();
    configuration_.DegreeSteps.clear();
  }]=])
      set(_query_weather_old [=[        sharedCache_(shared ? std::move(shared) : SharedWeatherCacheFor(configuration_,overlay.AdditionalCalculationHelpersEnabled())) {}]=])
      string(FIND "${adapter}" "${_query_weather_new}" _query_weather_position)
      if(_query_weather_position EQUAL -1)
        message(FATAL_ERROR "Expected exact qualified weather query-template constructor")
      endif()
      string(REPLACE "${_query_weather_new}" "${_query_weather_old}" adapter "${adapter}")
      set(_query_land_new [=[      : overlay_(overlay), configuration_(std::move(configuration)),
        methodStop_(std::move(methodStop)) {
    // The separate performance provider retains the full boat and polars.
    // Land/depth queries only need settings and shared safety evidence.
    configuration_.boat.Polars.clear();
    configuration_.DegreeSteps.clear();
  }]=])
      set(_query_land_old [=[      : overlay_(overlay), configuration_(std::move(configuration)),
        methodStop_(std::move(methodStop)) {}]=])
      string(FIND "${adapter}" "${_query_land_new}" _query_land_position)
      if(_query_land_position EQUAL -1)
        message(FATAL_ERROR "Expected exact qualified land query-template constructor")
      endif()
      string(REPLACE "${_query_land_new}" "${_query_land_old}" adapter "${adapter}")
      string(SHA256 actual "${adapter}")
    endif()
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "Calculation input differs from the qualified manifest: ${name}")
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
message(STATUS "All ${count} qualified calculation inputs match; exact adapter reporting and query-template exceptions verified")
