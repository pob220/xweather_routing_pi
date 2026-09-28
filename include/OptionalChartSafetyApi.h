/***************************************************************************
 * Compatibility names for the draft OpenCPN HostApi123 chart-safety API.
 *
 * Keeping these aliases local avoids a broad mechanical rename in the
 * routing engine while making HostApi123 the single source of ABI types.
 ***************************************************************************/

#ifndef XWEATHER_ROUTING_OPTIONAL_CHART_SAFETY_API_H
#define XWEATHER_ROUTING_OPTIONAL_CHART_SAFETY_API_H

#include "ocpn_plugin.h"

using PlugInSegmentSafetyStatus = HostApi123::SegmentSafetyStatus;
using PlugInSegmentSafetySource = HostApi123::SegmentSafetySource;
using PlugInSegmentSafetyDiagnosticReason =
    HostApi123::SegmentSafetyDiagnosticReason;
using PlugInSegmentSafetyHitCause = HostApi123::SegmentSafetyHitCause;
using PlugInSegmentSafetyOptions = HostApi123::SegmentSafetyOptions;
using PlugInSegmentSafetyResult = HostApi123::SegmentSafetyResult;
using PlugInSegmentSafetyRequestServiceResult =
    HostApi123::SegmentSafetyRequestServiceResult;
using PlugInSegmentSafetyTile = HostApi123::SegmentSafetyTile;
using PlugInSegmentSafetyTileCacheCallbacks =
    HostApi123::SegmentSafetyTileCacheCallbacks;
using PlugInSegmentSafetyChartInfoV1 = HostApi123::SegmentSafetyChartInfo;

inline constexpr auto PI_SEGMENT_SAFETY_SAFE = HostApi123::kSegmentSafetySafe;
inline constexpr auto PI_SEGMENT_SAFETY_CROSSES_LAND =
    HostApi123::kSegmentSafetyCrossesLand;
inline constexpr auto PI_SEGMENT_SAFETY_WITHIN_LAND_MARGIN =
    HostApi123::kSegmentSafetyWithinLandMargin;
inline constexpr auto PI_SEGMENT_SAFETY_UNSAFE_AREA =
    HostApi123::kSegmentSafetyUnsafeArea;
inline constexpr auto PI_SEGMENT_SAFETY_NO_DATA =
    HostApi123::kSegmentSafetyNoData;
inline constexpr auto PI_SEGMENT_SAFETY_ERROR = HostApi123::kSegmentSafetyError;
inline constexpr auto PI_SEGMENT_SAFETY_DRYING_AREA =
    HostApi123::kSegmentSafetyDryingArea;
inline constexpr auto PI_SEGMENT_SAFETY_TOO_SHALLOW =
    HostApi123::kSegmentSafetyTooShallow;
inline constexpr auto PI_SEGMENT_SAFETY_UNKNOWN_DEPTH =
    HostApi123::kSegmentSafetyUnknownDepth;
inline constexpr auto PI_SEGMENT_SAFETY_PENDING_DATA =
    HostApi123::kSegmentSafetyPendingData;

inline constexpr auto PI_SEGMENT_SAFETY_SOURCE_NONE =
    HostApi123::kSegmentSafetySourceNone;
inline constexpr auto PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART =
    HostApi123::kSegmentSafetySourceVectorChart;
inline constexpr auto PI_SEGMENT_SAFETY_SOURCE_CM93 =
    HostApi123::kSegmentSafetySourceCm93;
inline constexpr auto PI_SEGMENT_SAFETY_SOURCE_GSHHS_FALLBACK =
    HostApi123::kSegmentSafetySourceGshhsFallback;
inline constexpr auto PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR =
    HostApi123::kSegmentSafetySourcePluginVector;

inline constexpr auto PI_SEGMENT_SAFETY_DIAG_NONE =
    HostApi123::kSegmentSafetyDiagnosticNone;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_NO_CHART_DATABASE =
    HostApi123::kSegmentSafetyDiagnosticNoChartDatabase;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_NO_CANDIDATE_CHART =
    HostApi123::kSegmentSafetyDiagnosticNoCandidateChart;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_RASTER_ONLY =
    HostApi123::kSegmentSafetyDiagnosticRasterOnly;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_UNSUPPORTED_CHART_TYPE =
    HostApi123::kSegmentSafetyDiagnosticUnsupportedChartType;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_CHART_LOAD_FAILED =
    HostApi123::kSegmentSafetyDiagnosticChartLoadFailed;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_NO_LANDARE_GEOMETRY =
    HostApi123::kSegmentSafetyDiagnosticNoLandAreaGeometry;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_CHART_GEOMETRY_CLEAR =
    HostApi123::kSegmentSafetyDiagnosticChartGeometryClear;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_CHART_GEOMETRY_HIT =
    HostApi123::kSegmentSafetyDiagnosticChartGeometryHit;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_GSHHS_FALLBACK =
    HostApi123::kSegmentSafetyDiagnosticGshhsFallback;
inline constexpr auto PI_SEGMENT_SAFETY_DIAG_PENDING_DATA =
    HostApi123::kSegmentSafetyDiagnosticPendingData;

inline constexpr auto PI_SEGMENT_SAFETY_HIT_NONE =
    HostApi123::kSegmentSafetyHitNone;
inline constexpr auto PI_SEGMENT_SAFETY_HIT_ENDPOINT_IN_LANDARE =
    HostApi123::kSegmentSafetyHitEndpointInLandArea;
inline constexpr auto PI_SEGMENT_SAFETY_HIT_SEGMENT_INTERSECTS_LANDARE_EDGE =
    HostApi123::kSegmentSafetyHitSegmentIntersectsLandAreaEdge;
inline constexpr auto PI_SEGMENT_SAFETY_HIT_MARGIN_TO_LANDARE_EDGE =
    HostApi123::kSegmentSafetyHitMarginToLandAreaEdge;

inline constexpr int PI_SEGMENT_SAFETY_CHART_INFO_ABI_V1 = 1;

#endif  // XWEATHER_ROUTING_OPTIONAL_CHART_SAFETY_API_H
