#include <memory>

#include "ocpn_plugin.h"

// Standalone unit tests do not run inside OpenCPN. Model a stock host without
// the optional API 1.22 capability; integration tests use the real core.
std::unique_ptr<HostApi> GetHostApi() { return nullptr; }
