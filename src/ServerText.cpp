/******************************************************************************
 * mayara_pi - translating the text the server sends. See ServerText.h.
 *****************************************************************************/
#include "ServerText.h"

#include <wx/intl.h>
#include <wx/translation.h>

namespace {

wxString Translate(const std::string& english, const std::string& context) {
  const wxString s = wxString::FromUTF8(english.c_str());
  if (s.empty()) return s;
#if wxCHECK_VERSION(3, 1, 1)
  if (!context.empty()) {
    // wxGetTranslation hands back its argument when there is no translation.
    const wxString t =
        wxGetTranslation(s, wxEmptyString, wxString::FromUTF8(context.c_str()));
    if (t != s) return t;
  }
#else
  (void)context;
#endif
  return wxGetTranslation(s);
}

}  // namespace

wxString ServerControlName(const ControlDef& def) {
  return Translate(def.name, std::string());
}

wxString ServerControlDescription(const ControlDef& def) {
  return Translate(def.description, std::string());
}

wxString ServerEnumLabel(const ControlDef& def, int value) {
  auto it = def.descriptions.find(value);
  if (it == def.descriptions.end()) return wxString::Format("%d", value);
  return Translate(it->second, def.id);
}

wxString ServerCategory(const std::string& category) {
  if (category == "base") return _("Base");
  if (category == "targets") return _("Targets");
  if (category == "trails") return _("Trails");
  if (category == "advanced") return _("Advanced");
  if (category == "installation") return _("Installation");
  if (category == "info") return _("Info");
  return wxString::FromUTF8(category.c_str()).Capitalize();
}
