#include "ui/PianoRollCtrl.h"

#include <wx/wx.h>

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

namespace {

    int g_failures = 0;

    void expect(bool condition, const std::string& message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    void click(UI::PianoRollCtrl& roll, wxEventType type, const wxPoint& pos)
    {
        wxMouseEvent event(type);
        event.SetPosition(pos);
        event.SetEventObject(&roll);
        roll.ProcessWindowEvent(event);
    }

    void click_note(UI::PianoRollCtrl& roll, const wxPoint& pos)
    {
        click(roll, wxEVT_LEFT_DOWN, pos);
        click(roll, wxEVT_LEFT_UP, pos);
    }

    void press_key(UI::PianoRollCtrl& roll, int key,
                   bool control = false, bool shift = false)
    {
        wxKeyEvent event(wxEVT_KEY_DOWN);
        event.m_keyCode = key;
        event.SetControlDown(control);
        event.SetShiftDown(shift);
        event.SetEventObject(&roll);
        roll.ProcessWindowEvent(event);
    }

    void run_interaction_tests()
    {
        wxFrame frame(nullptr, wxID_ANY, "PianoRollCtrlTests");
        auto* roll = new UI::PianoRollCtrl(&frame, 60, 72);
        roll->SetSize(400, 120);
        roll->SetMap({});
        frame.Layout();

        std::map<int, Util::KeyMapping> observed;
        int changes = 0;
        roll->onMapChanged = [&](const std::map<int, Util::KeyMapping>& map) {
            observed = map;
            ++changes;
        };

        click_note(*roll, {15, 70});
        press_key(*roll, 'A');
        expect(changes == 1 && observed.at(60).vk_code == 'A',
               "clicking C4 then pressing A should bind A");

        click_note(*roll, {45, 70});
        press_key(*roll, 'A');
        expect(changes == 2 && observed.at(62).vk_code == 'A',
               "clicking D4 then pressing A should bind A");
        const auto conflict = roll->GetBindingAnalysis();
        expect(conflict.logical_duplicates.count(60) == 1 &&
                   conflict.physical_conflicts.count(62) == 1,
               "event-driven binding should update conflict analysis");

        press_key(*roll, 'Z', true);
        expect(changes == 3 && observed.find(62) == observed.end(),
               "Ctrl+Z should undo the latest note binding");

        press_key(*roll, 'Y', true);
        expect(changes == 4 && observed.at(62).vk_code == 'A',
               "Ctrl+Y should redo the latest note binding");

        click(*roll, wxEVT_RIGHT_DOWN, {15, 70});
        expect(changes == 5 && observed.find(60) == observed.end(),
               "right-click should clear a binding when no note is selected");

        press_key(*roll, WXK_RIGHT);
        press_key(*roll, 'B');
        expect(changes == 6 && observed.at(63).vk_code == 'B',
               "right-arrow navigation should continue from the last edited note");

        frame.Destroy();
    }

} // namespace

class PianoRollCtrlTestApp : public wxApp {
public:
    bool OnInit() override
    {
        CallAfter([this] {
            run_interaction_tests();
            ExitMainLoop();
        });
        return true;
    }

    int OnRun() override
    {
        wxApp::OnRun();
        if (g_failures != 0) {
            std::cerr << g_failures << " piano-roll control test(s) failed\n";
            return EXIT_FAILURE;
        }
        std::cout << "Piano roll control tests passed\n";
        return EXIT_SUCCESS;
    }
};

wxIMPLEMENT_APP_NO_MAIN(PianoRollCtrlTestApp);

int main(int argc, char** argv)
{
    return wxEntry(argc, argv);
}
