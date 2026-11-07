# SPDX-License-Identifier: GPL-3.0-or-later
include("${CMAKE_CURRENT_LIST_DIR}/VerifyClimatology.cmake")
add_subdirectory(opencpn-libs/wxJSON)
target_link_libraries(${PACKAGE_NAME} PRIVATE ocpn::wxjson)
target_include_directories(${PACKAGE_NAME} PRIVATE
  opencpn-libs/wxJSON/include/wx third_party/environmental-grib-generator/include)
if(NOT OCPN_FLATPAK_CONFIG)
  include(BundledJasper)
  target_link_libraries(${PACKAGE_NAME} PRIVATE ${XGRIB_JASPER_TARGET})
  install(FILES "${XGRIB_JASPER_LICENSE}" DESTINATION "share/opencpn/plugins/${PACKAGE_NAME}/runtime/licenses/jasper")
  target_compile_definitions(${PACKAGE_NAME} PRIVATE JASPER)
  add_subdirectory(third_party/environmental-grib-generator generator)
  if(ANDROID)
    target_link_libraries(${PACKAGE_NAME} PRIVATE environmental_grib_engine)
  elseif(UNIX AND NOT APPLE)
    set_target_properties(environmental-grib PROPERTIES OUTPUT_NAME environmental-grib.bin)
    install(TARGETS environmental-grib RUNTIME DESTINATION
      "share/opencpn/plugins/${PACKAGE_NAME}/libexec")
    install(PROGRAMS tools/environmental-grib DESTINATION
      "share/opencpn/plugins/${PACKAGE_NAME}/bin")
    find_path(ECCODES_DEFINITIONS_DIR NAMES grib1/boot.def
      PATHS /usr/share/eccodes/definitions /usr/local/share/eccodes/definitions REQUIRED)
    find_path(ECCODES_SAMPLES_DIR NAMES regular_ll_sfc_grib1.tmpl
      PATHS /usr/share/eccodes/samples /usr/local/share/eccodes/samples REQUIRED)
    find_path(PROJ_DATA_DIR NAMES proj.db PATHS /usr/share/proj /usr/local/share/proj REQUIRED)
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
  elseif(WIN32)
    install(TARGETS environmental-grib RUNTIME DESTINATION "plugins/${PACKAGE_NAME}/bin")
  elseif(APPLE)
    install(TARGETS environmental-grib RUNTIME DESTINATION
      "OpenCPN.app/Contents/SharedSupport/plugins/${PACKAGE_NAME}/bin")
  endif()
endif()
