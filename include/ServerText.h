/******************************************************************************
 * mayara_pi - translating the text the server sends.
 *
 * The control schema arrives in English: control names, descriptions,
 * categories and enum labels are string literals in mayara-server. We translate them here, on the
 * client, with the same gettext catalog as our own strings. The msgids come
 * from mayara-server's docs/ui-strings.json, turned into po/server_strings.h by
 * tools/gen-server-strings.py so xgettext picks them up.
 *
 * Anything without a translation -- a control newer than the catalog, another
 * Signal K radar provider, a language nobody has done yet -- shows the English
 * exactly as it arrived. Never feed values through this (model names, serial
 * numbers, the radar's user name), and never compare the result with anything:
 * code that reads meaning from a label must use the raw schema text.
 *****************************************************************************/
#ifndef MAYARA_SERVER_TEXT_H_
#define MAYARA_SERVER_TEXT_H_

#include <string>

#include <wx/string.h>

#include "RadarControls.h"

// A control's display name.
wxString ServerControlName(const ControlDef& def);

// A control's one-line description, for its tooltip. Empty when the server
// sends none.
wxString ServerControlDescription(const ControlDef& def);

// The label for one value of an enum control, or the number itself when the
// schema has no label for it. Looked up in the control's own context first, so
// a language can word "Auto" differently for different controls, then without.
wxString ServerEnumLabel(const ControlDef& def, int value);

// A section heading for a schema category ("base", "targets", ...).
wxString ServerCategory(const std::string& category);

#endif  // MAYARA_SERVER_TEXT_H_
