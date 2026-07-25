#ifndef WEATHER_ROUTING_MODERN_NATIVE_ROUTE_H
#define WEATHER_ROUTING_MODERN_NATIVE_ROUTE_H

#include <wx/string.h>

class RouteMapOverlay;
class RouteMapConfiguration;

/** Return whether this configuration will use the modern native solver. */
bool ModernNativeRouteEnabled(const RouteMapConfiguration& configuration);

/** Run the modern value-node/Pareto/recovery solver for one configured leg. */
bool RunModernNativeRoute(RouteMapOverlay& overlay, wxString& error);

#endif
