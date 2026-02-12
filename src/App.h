#pragma once
#include <wx/wx.h>

class App : public wxApp {
public:
    virtual bool OnInit();
    virtual int OnExit();
};
