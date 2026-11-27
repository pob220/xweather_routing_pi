// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/adaptive/Quick2.h"

namespace quick2_adaptive {
Result Engine::route(const wr::RoutingRequest& request,
                     const wr::RoutingEnvironment& environment,
                     const Options& options) const {
  auto result = quick2_routing::Engine{}.route(request, environment, options);
  result.route.engineIdentity = kEngineIdentity;
  return result;
}
}  // namespace quick2_adaptive
