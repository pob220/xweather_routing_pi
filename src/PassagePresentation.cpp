// SPDX-License-Identifier: GPL-3.0-or-later
#include "PassagePresentation.h"
#include <wx/intl.h>

namespace weather_routing {
wxString PassageDisplayName(const wxString& name, const wxString& guid,
                           const wxString& start, const wxString& end) {
  wxString label = name;
  label.Trim(true).Trim(false);
  if (!label.empty() && (guid.empty() || !label.IsSameAs(guid, false)))
    return label;
  if (!start.empty() && !end.empty())
    return wxString::Format(_("%s to %s"), start, end);
  return _("Unnamed passage");
}
wxString PassageLegLabel(const wxString& name, int leg, int count) {
  // Wide literals bypass the Windows ANSI code page, including for gettext.
  return name + wxString::Format(_(L" • leg %d/%d"), leg, count);
}
wxString PassageDirection(const wxString& start, const wxString& end) {
  return start + L"  →  " + end;
}
}  // namespace weather_routing
