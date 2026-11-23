# SPDX-License-Identifier: GPL-3.0-or-later
include("${CMAKE_CURRENT_LIST_DIR}/VerifyClimatology.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BundledGribAec.cmake")
target_link_libraries(${PACKAGE_NAME} PRIVATE grib_aec_decoder)
add_subdirectory(opencpn-libs/wxJSON)
target_link_libraries(${PACKAGE_NAME} PRIVATE ocpn::wxjson)
target_include_directories(${PACKAGE_NAME} PRIVATE
  opencpn-libs/wxJSON/include/wx third_party/environmental-grib-generator/include)
if(NOT OCPN_FLATPAK_CONFIG)
  include(BundledJasper)
  target_link_libraries(${PACKAGE_NAME} PRIVATE ${XGRIB_JASPER_TARGET})
  if(WIN32)
    set(_weather_runtime "plugins/${PACKAGE_NAME}/runtime")
  elseif(APPLE)
    set(_weather_runtime "OpenCPN.app/Contents/SharedSupport/plugins/${PACKAGE_NAME}/runtime")
  else()
    set(_weather_runtime "share/opencpn/plugins/${PACKAGE_NAME}/runtime")
  endif()
  install(FILES "${XGRIB_JASPER_LICENSE}" DESTINATION "${_weather_runtime}/licenses/jasper")
  target_compile_definitions(${PACKAGE_NAME} PRIVATE JASPER)
  if(WIN32)
    # Keep the helper's scientific dependencies outside the in-process wx SDK.
    # ci/build-windows-generator.py builds and checks this app-local runtime.
    set(WEATHER_ROUTING_WINDOWS_GENERATOR_STAGE "$ENV{WEATHER_ROUTING_WINDOWS_GENERATOR_STAGE}" CACHE PATH
      "Staged x64 environmental-grib runtime from ci/build-windows-generator.py")
    set(_weather_x86_host_on_x64_default OFF)
    if("$ENV{WEATHER_ROUTING_WINDOWS_X86_HOST_ON_X64}" STREQUAL "1")
      set(_weather_x86_host_on_x64_default ON)
    endif()
    option(WEATHER_ROUTING_WINDOWS_X86_HOST_ON_X64
      "Build an x86 OpenCPN plugin for 64-bit Windows, using the separate x64 helper"
      ${_weather_x86_host_on_x64_default})
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
      if(NOT WEATHER_ROUTING_WINDOWS_X86_HOST_ON_X64)
        message(FATAL_ERROR "The Windows x86 plugin uses an x64 GRIB helper and requires 64-bit Windows. Enable WEATHER_ROUTING_WINDOWS_X86_HOST_ON_X64 explicitly.")
      endif()
    elseif(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
      message(FATAL_ERROR "Unsupported Windows plugin architecture")
    endif()
    foreach(_required bin/environmental-grib.exe bin/vcomp140.dll
        runtime/share/eccodes/definitions/grib1/boot.def
        runtime/share/eccodes/samples/regular_ll_sfc_grib1.tmpl
        runtime/share/proj/proj.db runtime/stage.json)
      if(NOT EXISTS "${WEATHER_ROUTING_WINDOWS_GENERATOR_STAGE}/${_required}")
        message(FATAL_ERROR "Missing Windows helper runtime: ${_required}. Run python ci/circleci-build-windows64.py; see docs/v2/WINDOWS.md")
      endif()
    endforeach()
    install(DIRECTORY "${WEATHER_ROUTING_WINDOWS_GENERATOR_STAGE}/bin/"
      DESTINATION "plugins/${PACKAGE_NAME}/bin")
    install(DIRECTORY "${WEATHER_ROUTING_WINDOWS_GENERATOR_STAGE}/runtime/"
      DESTINATION "${_weather_runtime}")
    install(FILES third_party/environmental-grib-generator/LICENSE
      third_party/INTEGRATED_WEATHER_SOURCES.md DESTINATION "${_weather_runtime}/licenses")
  else()
    add_subdirectory(third_party/environmental-grib-generator generator)
    if(UNIX AND NOT APPLE AND NOT ANDROID)
      # Older distro ecCodes exposes the GRIB-specific sample factory, but
      # not the generic factory added later. Both callers create GRIB samples.
      # Keep this build adaptation outside the shared scientific source tree.
      include(CheckCXXSourceCompiles)
      find_path(_weather_eccodes_header NAMES eccodes.h REQUIRED)
      set(_weather_saved_required_includes "${CMAKE_REQUIRED_INCLUDES}")
      set(CMAKE_REQUIRED_INCLUDES "${_weather_eccodes_header}")
      check_cxx_source_compiles("#include <eccodes.h>
using SampleFactory = decltype(&codes_handle_new_from_samples);
int main() { return 0; }"
        WEATHER_ROUTING_ECCODES_HAS_GENERIC_SAMPLE_FACTORY)
      set(CMAKE_REQUIRED_INCLUDES "${_weather_saved_required_includes}")
      unset(_weather_saved_required_includes)
      if(NOT WEATHER_ROUTING_ECCODES_HAS_GENERIC_SAMPLE_FACTORY)
        target_compile_definitions(environmental_grib_engine PRIVATE
          codes_handle_new_from_samples=codes_grib_handle_new_from_samples)
      endif()
    endif()
  endif()
  if(WIN32)
    # Installed above from the independently built and tested helper stage.
  elseif(ANDROID)
    target_link_libraries(${PACKAGE_NAME} PRIVATE environmental_grib_engine)
    # The host carries an older TLS stack. Keep the complete statically linked
    # generator runtime private, as in the tested Android xGRIB adapter.
    target_link_options(${PACKAGE_NAME} PRIVATE "-Wl,--exclude-libs,ALL")
    install(FILES third_party/environmental-grib-generator/LICENSE
      third_party/INTEGRATED_WEATHER_SOURCES.md DESTINATION
      "${PREFIX_PARENTDATA}/plugins/${PACKAGE_NAME}/data/android-licenses")
    file(GLOB _android_dependency_licenses
      "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/*/copyright")
    foreach(_license IN LISTS _android_dependency_licenses)
      get_filename_component(_dependency_dir "${_license}" DIRECTORY)
      get_filename_component(_dependency "${_dependency_dir}" NAME)
      install(FILES "${_license}" DESTINATION
        "${PREFIX_PARENTDATA}/plugins/${PACKAGE_NAME}/data/android-licenses/${_dependency}")
    endforeach()
  elseif(UNIX AND NOT APPLE)
    set_target_properties(environmental-grib PROPERTIES OUTPUT_NAME environmental-grib.bin)
    install(TARGETS environmental-grib RUNTIME DESTINATION
      "share/opencpn/plugins/${PACKAGE_NAME}/libexec")
    install(PROGRAMS tools/environmental-grib DESTINATION
      "share/opencpn/plugins/${PACKAGE_NAME}/bin")
    find_path(ECCODES_DEFINITIONS_DIR NAMES grib1/boot.def
      PATHS /usr/share/eccodes/definitions /usr/local/share/eccodes/definitions
        "${CMAKE_INSTALL_PREFIX}/share/eccodes/definitions" REQUIRED)
    find_path(ECCODES_SAMPLES_DIR NAMES regular_ll_sfc_grib1.tmpl
      PATHS /usr/share/eccodes/samples /usr/local/share/eccodes/samples
        "${CMAKE_INSTALL_PREFIX}/share/eccodes/samples" REQUIRED)
    find_path(PROJ_DATA_DIR NAMES proj.db PATHS /usr/share/proj /usr/local/share/proj
      "${CMAKE_INSTALL_PREFIX}/share/proj" REQUIRED)
    # The helper handles GRIB. BUFR, aviation and GRIB3 definitions contribute
    # about 80 MiB on current distributions and are not part of this reader.
    install(DIRECTORY "${ECCODES_DEFINITIONS_DIR}"
      DESTINATION "share/opencpn/plugins/${PACKAGE_NAME}/runtime/share/eccodes"
      PATTERN "bufr" EXCLUDE PATTERN "metar" EXCLUDE PATTERN "taf" EXCLUDE PATTERN "grib3" EXCLUDE)
    install(DIRECTORY "${ECCODES_SAMPLES_DIR}"
      DESTINATION "share/opencpn/plugins/${PACKAGE_NAME}/runtime/share/eccodes")
    install(DIRECTORY "${PROJ_DATA_DIR}"
      DESTINATION "share/opencpn/plugins/${PACKAGE_NAME}/runtime/share")
    configure_file(cmake/BundleGeneratorRuntime.cmake.in BundleGeneratorRuntime.cmake @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/BundleGeneratorRuntime.cmake")
    install(FILES third_party/environmental-grib-generator/LICENSE
      third_party/INTEGRATED_WEATHER_SOURCES.md
      DESTINATION "share/opencpn/plugins/${PACKAGE_NAME}/runtime/licenses")
  elseif(APPLE)
    install(TARGETS environmental-grib RUNTIME DESTINATION
      "OpenCPN.app/Contents/SharedSupport/plugins/${PACKAGE_NAME}/bin")
    set(_mac_runtime "OpenCPN.app/Contents/SharedSupport/plugins/${PACKAGE_NAME}/runtime")
    find_path(ECCODES_DEFINITIONS_DIR NAMES grib1/boot.def
      HINTS ${CMAKE_PREFIX_PATH} PATHS /opt/homebrew /usr/local
      PATH_SUFFIXES share/eccodes/definitions REQUIRED)
    find_path(ECCODES_SAMPLES_DIR NAMES regular_ll_sfc_grib1.tmpl
      HINTS ${CMAKE_PREFIX_PATH} PATHS /opt/homebrew /usr/local
      PATH_SUFFIXES share/eccodes/samples REQUIRED)
    find_path(PROJ_DATA_DIR NAMES proj.db HINTS ${CMAKE_PREFIX_PATH}
      PATHS /opt/homebrew /usr/local PATH_SUFFIXES share/proj REQUIRED)
    install(DIRECTORY "${ECCODES_DEFINITIONS_DIR}" "${ECCODES_SAMPLES_DIR}"
      DESTINATION "${_mac_runtime}/share/eccodes"
      PATTERN "bufr" EXCLUDE PATTERN "metar" EXCLUDE PATTERN "taf" EXCLUDE PATTERN "grib3" EXCLUDE)
    # Homebrew's top-level share/proj is a Cellar symlink. Materialise its
    # contents so relocation never retains a build-machine-relative link.
    file(REAL_PATH "${PROJ_DATA_DIR}" _mac_proj_data)
    install(DIRECTORY "${_mac_proj_data}/" DESTINATION "${_mac_runtime}/share/proj")
    install(FILES third_party/environmental-grib-generator/LICENSE
      third_party/INTEGRATED_WEATHER_SOURCES.md DESTINATION "${_mac_runtime}/licenses")
    configure_file(cmake/BundleMacGeneratorRuntime.cmake.in
      "${CMAKE_CURRENT_BINARY_DIR}/BundleMacGeneratorRuntime.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/BundleMacGeneratorRuntime.cmake")
  endif()
endif()
