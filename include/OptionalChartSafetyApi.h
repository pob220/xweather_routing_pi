/***************************************************************************
 * Compatibility names for the draft OpenCPN HostApi122 chart-safety API.
 *
 * Keeping these aliases local avoids a broad mechanical rename in the
 * routing engine while making HostApi122 the single source of ABI types.
 ***************************************************************************/

#ifndef XWEATHER_ROUTING_OPTIONAL_CHART_SAFETY_API_H
#define XWEATHER_ROUTING_OPTIONAL_CHART_SAFETY_API_H

#include "ocpn_plugin.h"

using PlugInSegmentSafetyStatus = HostApi122::SegmentSafetyStatus;
using PlugInSegmentSafetySource = HostApi122::SegmentSafetySource;
using PlugInSegmentSafetyDiagnosticReason =
    HostApi122::SegmentSafetyDiagnosticReason;
using PlugInSegmentSafetyHitCause = HostApi122::SegmentSafetyHitCause;
using PlugInSegmentSafetyOptions = HostApi122::SegmentSafetyOptions;
using PlugInSegmentSafetyResult = HostApi122::SegmentSafetyResult;
using PlugInSegmentSafetyRequestServiceResult =
    HostApi122::SegmentSafetyRequestServiceResult;
using PlugInSegmentSafetyTile = HostApi122::SegmentSafetyTile;
using PlugInSegmentSafetyTileCacheCallbacks =
    HostApi122::SegmentSafetyTileCacheCallbacks;
using PlugInSegmentSafetyChartInfoV1 = HostApi122::SegmentSafetyChartInfo;

inline constexpr auto PI_SEGMENT_SAFETY_SAFE = HostApi122::kSegmentSafetySafe;
inline constexpr auto PI_SEGMENT_SAFETY_CROSSES_LAND =
    HostApi122::kSegmentSafetyCrossesLand;
inline constexpr auto PI_SEGMENT_SAFETY_WITHIN_LAND_MARGIN =
    HostApi122::kSegmentSafetyWithinLandMargin;
inline constexpr auto PI_SEGMENT_SAFETY_UNSAFE_AREA =
    HostApi122::kSegmentSafetyUnsafeArea;
inline constexpr auto PI_SEGMENT_SAFETY_NO_DATA =
    HostApi122::kSegmentSafetyNoData;
inline constexpr auto PI_SEGMENT_SAFETY_ERROR = HostApi122::kSegmentSafetyError;
inline constexpr auto PI_SEGMENT_SAFETY_DRYING_AREA =
    HostApi122::kSegmentSafetyDryingArea;
inline constexpr auto PI_SEGMENT_SAFETY_TOO_SHALLOW =
    HostApi122::kSegmentSafetyTooShallow;
inline constexpr auto PI_SEGMENT_SAFETY_UNKNOWN_DEPTH =
    HostApi122::kSegmentSafetyUnknownDepth;
inline constexpr auto PI_SEGMENT_SAFETY_PENDING_DATA =
    HostApi122::kSegmentSafetyPendingData;

inline constexpr auto PI_SEGMENT_SAFETY_SOURCE_NONE =
    HostApi122::kSegmentSafetySourceNone;
inline constexpr auto PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART =
    HostApi122::kSegmentSafetySourceVectorChart;
inline constexpr auto PI_SEGMENT_SAFETY_SOURCE_CM93 =
    HostApi122::kSegmentSafetySourceCm93;
inline constexpr auto PI_SEGMENT_SAFETY_SOURCE_GSHHS_FALLBACK =
    HostApi122::kSegmentSafetySourceGshhsFallback;
inline constexpr auto PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR =
    HostApi122::kSegmentSafetySourcePluginVector;

inline constexpr auto PI_SEGMENT_SAFETY_DIAG_NONE =
    HostApi122::kSegmentSafetyDiagnosticNone;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_NO_CHART_DATABASE =
    HostApi122::kSegmentSafetyDiagnosticNoChartDatabase;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_NO_CANDIDATE_CHART =
    HostApi122::kSegmentSafetyDiagnosticNoCandidateChart;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_RASTER_ONLY =
    HostApi122::kSegmentSafetyDiagnosticRasterOnly;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_UNSUPPORTED_CHART_TYPE =
    HostApi122::kSegmentSafetyDiagnosticUnsupportedChartType;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_CHART_LOAD_FAILED =
    HostApi122::kSegmentSafetyDiagnosticChartLoadFailed;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_NO_LANDARE_GEOMETRY =
    HostApi122::kSegmentSafetyDiagnosticNoLandAreaGeometry;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_CHART_GEOMETRY_CLEAR =
    HostApi122::kSegmentSafetyDiagnosticChartGeometryClear;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_CHART_GEOMETRY_HIT =
    HostApi122::kSegmentSafetyDiagnosticChartGeometryHit;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_GSHHS_FALLBACK =
    HostApi122::kSegmentSafetyDiagnosticGshhsFallback;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_PENDING_DATA =
    HostApi122::kSegmentSafetyDiagnosticPendingData;

inline constexpr auto PI_SEGMENT_SAFETY_HIT_NONE =
    HostApi122::kSegmentSafetyHitNone;
inline constexpr auto PI_SEGMENT_SAFETY_HIT_ENDPOINT_IN_LANDARE =
    HostApi122::kSegmentSafetyHitEndpointInLandArea;
inline constexpr auto PI_SEGMENT_SAFETY_HIT_SEGMENT_INTERSECTS_LANDARE_EDGE =
    HostApi122::kSegmentSafetyHitSegmentIntersectsLandAreaEdge;
inline constexpr auto PI_SEGMENT_SAFETY_HIT_MARGIN_TO_LANDARE_EDGE =
    HostApi122::kSegmentSafetyHitMarginToLandAreaEdge;

inline constexpr int PI_SEGMENT_SAFETY_CHART_INFO_ABI_V1 = 1;

#endif  // XWEATHER_ROUTING_OPTIONAL_CHART_SAFETY_API_H
