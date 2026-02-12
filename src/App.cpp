#include "App.h"
#include "ui/MainFrame.h"
#include <ctime>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

wxIMPLEMENT_APP(App);

bool App::OnInit() {
    // Set up high precision timer (Windows specific)
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    
    // Initialize random seed for better randomness
    srand(static_cast<unsigned int>(time(nullptr)));
    
    MainFrame* frame = new MainFrame();
    frame->Show(true);
    return true;
}

int App::OnExit() {
#ifdef _WIN32
    timeEndPeriod(1);
#endif
    return 0;
}
