#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/gbsizer.h>
#include <wx/statline.h>
#include <wx/html/htmlwin.h>
#include "../tmp/lang.gen.hpp"
#include <cstdio>
#include <iostream>

#include "guiPreConnectPane.hpp"



class MyApp: public wxApp
{
	wxFrame *frame;
public:
	
	bool OnInit()
	{
		if ((wxApp::argc > 1) && (wxString(wxApp::argv[1]) == "--version")) {
			std::cout << "version number XXX" << std::endl; // XXX
			exit(0);
		}
		langInit(wxLocale::GetLanguageCanonicalName(wxLocale::GetSystemLanguage()));
		wxSizerFlags::DisableConsistencyChecks();
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		frame = new wxFrame((wxFrame *)NULL, -1,  wxT("GUI"), wxDefaultPosition, wxSize(800,600));
		
		guiPreConnectPane* preConnectPane = new guiPreConnectPane(frame);
		sizer->Add(preConnectPane, 1, wxEXPAND);
		frame->SetSizer(sizer);
		frame->SetMinSize({750, 100});
		
		frame->Show();
		return true;
	} 
};

IMPLEMENT_APP(MyApp)
