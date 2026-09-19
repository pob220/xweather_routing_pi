# The historical opencpn-libs Windows import libraries are x86. A native x64
# build must explicitly supply the import library from its matching host SDK.
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  return()
endif()
set(WEATHER_ROUTING_WINDOWS_IMPORT_LIBRARY "" CACHE FILEPATH
    "Import library produced by the native Windows x64 OpenCPN host")
if(NOT EXISTS "${WEATHER_ROUTING_WINDOWS_IMPORT_LIBRARY}")
  message(FATAL_ERROR "Windows x64 requires WEATHER_ROUTING_WINDOWS_IMPORT_LIBRARY")
endif()
add_library(WEATHER_ROUTING_API_X64 INTERFACE)
add_library(ocpn::api ALIAS WEATHER_ROUTING_API_X64)
target_include_directories(WEATHER_ROUTING_API_X64 INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/opencpn-libs/api-${OCPN_API_VERSION_MINOR}")
target_compile_definitions(WEATHER_ROUTING_API_X64 INTERFACE UNICODE)
target_link_libraries(WEATHER_ROUTING_API_X64 INTERFACE
    "${WEATHER_ROUTING_WINDOWS_IMPORT_LIBRARY}")
find_package(ZLIB REQUIRED)
add_library(WEATHER_ROUTING_ZLIB_X64 INTERFACE)
add_library(ocpn::zlib ALIAS WEATHER_ROUTING_ZLIB_X64)
target_link_libraries(WEATHER_ROUTING_ZLIB_X64 INTERFACE ZLIB::ZLIB)
add_compile_options(/we4302 /we4311 /we4312)
add_compile_definitions(_USE_MATH_DEFINES)
