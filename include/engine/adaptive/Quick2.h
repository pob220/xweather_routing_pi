// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "quick2_routing/Engine.h"

namespace quick2_adaptive {
namespace wr = supercpn::weather_routing;
inline constexpr char kEngineName[] = "Quick2Adaptive";
inline constexpr char kEngineIdentity[] = "quick2-adaptive-v1";
using quick2_routing::Diagnostics;
using quick2_routing::Objective;
using quick2_routing::Options;
using quick2_routing::Result;

class Engine {
public:
  Result route(const wr::RoutingRequest&, const wr::RoutingEnvironment&,
               const Options& = {}) const;
};
using Quick2Engine = Engine;
}  // namespace quick2_adaptive
