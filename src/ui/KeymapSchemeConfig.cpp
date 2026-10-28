#include "KeymapSchemeConfig.h"

namespace UI::KeymapSchemeStore {

long find_scheme_index(const std::vector<wxString>& names, const wxString& name)
{
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (names[i] == name)
            return static_cast<long>(i);
    }
    return -1;
}

static wxString scheme_group(const std::vector<wxString>& names, const wxString& name)
{
    const long index = find_scheme_index(names, name);
    return index >= 0 ? wxString::Format("/KeymapSchemes/List_%ld", index) : wxString();
}

bool read_scheme_map(wxConfigBase* config, const std::vector<wxString>& names,
                     const wxString& name, std::map<int, Util::KeyMapping>& out)
{
    if (!config)
        return false;
    const wxString groupPath = scheme_group(names, name);
    if (groupPath.IsEmpty() || !config->HasGroup(groupPath))
        return false;

    config->SetPath(groupPath);
    long noteCount = 0;
    config->Read("NoteCount", &noteCount, 0L);
    for (long index = 0; index < noteCount; ++index)
    {
        wxString value;
        if (!config->Read(wxString::Format("Note%ld", index), &value))
            continue;

        long pitch = 0;
        long vk = 0;
        long modifier = 0;
        wxString rest = value;
        if (!rest.BeforeFirst(',').ToLong(&pitch))
            continue;
        rest = rest.AfterFirst(',');
        if (!rest.BeforeFirst(',').ToLong(&vk))
            continue;
        rest = rest.AfterFirst(',');
        if (!rest.ToLong(&modifier))
            continue;
        out[static_cast<int>(pitch)] = {
            static_cast<int>(vk),
            static_cast<int>(modifier)
        };
    }
    config->SetPath("/");
    return true;
}

void write_scheme_map(wxConfigBase* config, const std::vector<wxString>& names,
                      const wxString& name, const std::map<int, Util::KeyMapping>& map)
{
    if (!config)
        return;
    const wxString groupPath = scheme_group(names, name);
    if (groupPath.IsEmpty())
        return;

    config->SetPath(groupPath);
    config->Write("Name", name);
    config->Write("NoteCount", static_cast<long>(map.size()));
    long noteIndex = 0;
    for (const auto& [pitch, mapping] : map)
    {
        config->Write(
            wxString::Format("Note%ld", noteIndex++),
            wxString::Format("%d,%d,%d", pitch, mapping.vk_code, mapping.modifier));
    }
    config->SetPath("/");
}

static wxString pitch_key(const std::vector<wxString>& names, const wxString& name)
{
    if (name.IsEmpty() || name == "@builtin_0")
        return "/Global/FF14Pitch";
    if (name.StartsWith("@builtin_"))
        return "/Global/YYPitch";
    const wxString groupPath = scheme_group(names, name);
    return groupPath.IsEmpty() ? wxString() : groupPath + "/Pitch";
}

bool read_scheme_pitch(wxConfigBase* config, const std::vector<wxString>& names,
                       const wxString& name, int& minPitch, int& maxPitch)
{
    minPitch = 48;
    maxPitch = 84;
    if (!config)
        return false;

    const wxString key = pitch_key(names, name);
    if (key.IsEmpty())
        return false;

    if ((name.IsEmpty() || name == "@builtin_0") && !config->HasEntry(key))
    {
        config->Read("/Global/MinPitch", &minPitch, 48);
        config->Read("/Global/MaxPitch", &maxPitch, 84);
        return true;
    }
    if (!name.IsEmpty() && name.StartsWith("@builtin_"))
        maxPitch = 83;
    if (!config->HasEntry(key))
        return false;

    config->Read(key, &minPitch, 48);
    config->Read(key + "Max", &maxPitch, maxPitch);
    return true;
}

void write_scheme_pitch(wxConfigBase* config, const std::vector<wxString>& names,
                        const wxString& name, int minPitch, int maxPitch)
{
    if (!config)
        return;
    const wxString key = pitch_key(names, name);
    if (key.IsEmpty())
        return;
    config->Write(key, minPitch);
    config->Write(key + "Max", maxPitch);
}

void populate_choice(wxChoice* choice, const std::vector<wxString>& names,
                     const wxString& currentName,
                     const wxString& defaultLabel, const wxString& builtinLabel)
{
    if (!choice)
        return;

    choice->Clear();
    choice->Append(defaultLabel);
    choice->Append(builtinLabel);
    for (const auto& name : names)
        choice->Append(name);

    if (currentName.IsEmpty() || currentName == "@builtin_0")
        choice->SetSelection(0);
    else if (currentName == "@builtin_1")
        choice->SetSelection(1);
    else
    {
        const long index = find_scheme_index(names, currentName);
        choice->SetSelection(index >= 0 ? static_cast<int>(index + 2) : 0);
    }
}

}
