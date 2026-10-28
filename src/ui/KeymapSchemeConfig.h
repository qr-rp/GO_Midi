#pragma once

#include <wx/choice.h>
#include <wx/confbase.h>

#include <map>
#include <vector>

#include "../util/KeyManager.h"

namespace UI::KeymapSchemeStore {

long find_scheme_index(const std::vector<wxString>& names, const wxString& name);

bool read_scheme_map(wxConfigBase* config, const std::vector<wxString>& names,
                     const wxString& name, std::map<int, Util::KeyMapping>& out);

void write_scheme_map(wxConfigBase* config, const std::vector<wxString>& names,
                      const wxString& name, const std::map<int, Util::KeyMapping>& map);

bool read_scheme_pitch(wxConfigBase* config, const std::vector<wxString>& names,
                       const wxString& name, int& minPitch, int& maxPitch);

void write_scheme_pitch(wxConfigBase* config, const std::vector<wxString>& names,
                        const wxString& name, int minPitch, int maxPitch);

void populate_choice(wxChoice* choice, const std::vector<wxString>& names,
                     const wxString& currentName,
                     const wxString& defaultLabel, const wxString& builtinLabel);

}
