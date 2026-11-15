// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

namespace weather_routing::detail {
// Floating-point to_chars is unavailable with the macOS 11 deployment target.
// Select readable locale-independent text that parses back to the exact value.
// This runs only when populating an editable coordinate, never in routing.
inline std::string PortableCoordinateText(double value) {
  if (std::isnan(value)) return std::signbit(value) ? "-nan" : "nan";
  if (std::isinf(value)) return std::signbit(value) ? "-inf" : "inf";
  std::string best;
  const auto consider = [&](const std::string& text) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double parsed{};
    input >> parsed;
    if (input && parsed == value && std::signbit(parsed) == std::signbit(value) &&
        (best.empty() || text.size() < best.size()))
      best = text;
  };
  for (int digits = 0; digits <= std::numeric_limits<double>::max_digits10; ++digits) {
    std::ostringstream fixed;
    fixed.imbue(std::locale::classic());
    fixed << std::fixed << std::setprecision(digits) << value;
    consider(fixed.str());
    if (digits) {
      std::ostringstream general;
      general.imbue(std::locale::classic());
      general << std::setprecision(digits) << value;
      consider(general.str());
    }
  }
  return best;
}
}  // namespace weather_routing::detail
