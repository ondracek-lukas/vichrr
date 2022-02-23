#include "guiStaticInfo.hpp"
#include "guiAudioMeter.hpp"

class guiPreConnectPane : public wxScrolledWindow {
	wxChoice *inputList, *outputList;
	wxButton *btnDevApply, *btnMeasureLat, *btnCalibrate, *btnConnect;
	wxTextCtrl *address, *name, *latency;
	guiAudioMeter *meterSystem, *meterAdjusted;
	
	public:
		guiPreConnectPane(wxWindow* parent) : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL)
		{
			wxPanel* panel = new wxPanel(this);
			panel->SetMaxSize({1000, -1});
			wxGridBagSizer* sizer = new wxGridBagSizer(8, 15);
			{
				wxBoxSizer* outerSizer = new wxBoxSizer(wxVERTICAL);
				this->SetSizer(outerSizer);
				outerSizer->Add(panel, 1, wxEXPAND | wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

				wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
				panel->SetSizer(panelSizer);
				panelSizer->Add(sizer, 1, wxEXPAND | wxALL, 10);
			}
			this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));



			int l;

			// labels:
			l = 1;
			sizer->Add(new wxStaticText(panel, wxID_ANY, STR(lab_mic)),      {l++,0}, wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 0);
			sizer->Add(new wxStaticText(panel, wxID_ANY, STR(lab_head)),     {l++,0}, wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 0); l += 4;
			sizer->Add(new wxStaticText(panel, wxID_ANY, STR(lab_dev_lat)),  {l++,0}, wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 0); l += 2;
			sizer->Add(new wxStaticText(panel, wxID_ANY, STR(lab_mic_vol)),  {l++,0}, wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 0); l += 2;
			sizer->Add(new wxStaticText(panel, wxID_ANY, STR(lab_server)),   {l++,0}, wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 0);
			sizer->Add(new wxStaticText(panel, wxID_ANY, STR(lab_nickname)), {l++,0}, wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 0);

			// info boxes:
			l = 0;
			sizer->Add(new guiStaticInfo(panel, STR(info_audio_devices)),     {l,2}, {5,1}, wxEXPAND | wxALL, 0); l += 6;
			sizer->Add(new guiStaticInfo(panel, STR(info_lat_measurement)),   {l,2}, {3,1}, wxEXPAND | wxALL, 0); l += 4;
			sizer->Add(new guiStaticInfo(panel, STR(info_mic_calibration)),   {l,2}, {2,1}, wxEXPAND | wxALL, 0); l += 3;
			sizer->Add(new guiStaticInfo(panel, STR(info_server)),            {l,2}, {4,1}, wxEXPAND | wxALL, 0); l += 4;

			// separators
			l = 5;
			sizer->Add(new wxStaticLine(panel, wxID_ANY),      {l,0}, {1,3}, wxEXPAND); l += 4;
			sizer->Add(new wxStaticLine(panel, wxID_ANY),      {l,0}, {1,3}, wxEXPAND); l += 3;
			sizer->Add(new wxStaticLine(panel, wxID_ANY),      {l,0}, {1,3}, wxEXPAND); l += 2;

			l = 1;
			// device selection:
			inputList  = new wxChoice(panel, wxID_ANY);
			outputList = new wxChoice(panel, wxID_ANY);
			btnDevApply = new wxButton(panel, wxID_ANY, STR(btn_apply));
			sizer->Add(inputList,  {l,1}, wxDefaultSpan, wxEXPAND | wxALL, 0); l++;
			sizer->Add(outputList, {l,1}, wxDefaultSpan, wxEXPAND | wxALL, 0); l++;
			{
				wxGridSizer* subsizer = new wxGridSizer(1, 2, 0, 8);
				subsizer->Add(new wxControl(panel, wxID_ANY));
				subsizer->Add(btnDevApply, 0, wxEXPAND | wxLEFT, 0);
				sizer->Add(subsizer, {l,1}, wxDefaultSpan, wxEXPAND | wxALL, 0); l++;
			}
			l+=3;
				
			// device latency measurement
			latency = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
			btnMeasureLat = new wxButton(panel, wxID_ANY, STR(btn_measure_lat));
			{
				wxGridSizer* subsizer = new wxGridSizer(1, 2, 0, 8);
				subsizer->Add(latency, 0, wxEXPAND | wxRIGHT, 0);
				subsizer->Add(btnMeasureLat, 0, wxEXPAND | wxLEFT, 0);
				sizer->Add(subsizer, {l,1}, wxDefaultSpan, wxEXPAND | wxALL, 0); l++;
			}
			l+=2;

			// microphone volume calibration
			meterSystem = new guiAudioMeter(panel);
			meterAdjusted = new guiAudioMeter(panel);
			btnCalibrate = new wxButton(panel, wxID_ANY, STR(btn_calibrate));
			{
				wxGridSizer* subsizer = new wxGridSizer(1, 2, 0, 8);
				subsizer->Add(meterSystem,   0, wxEXPAND | wxLEFT | wxRIGHT, 40);
				subsizer->Add(meterAdjusted, 0, wxEXPAND | wxLEFT | wxRIGHT, 40);
				sizer->Add(subsizer, {l,1}, wxDefaultSpan, wxEXPAND | wxALL, 0); l++;
			}
			{
				wxGridSizer* subsizer = new wxGridSizer(1, 2, 0, 8);
				subsizer->Add(new wxStaticText(panel, wxID_ANY, STR(lab_system)),  0, wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
				subsizer->Add(btnCalibrate, 0, wxEXPAND);
				sizer->Add(subsizer, {l,1}, wxDefaultSpan, wxEXPAND | wxALL, 0); l++;
			}
			l++;

			// server and name
			address = new wxTextCtrl(panel, wxID_ANY);
			name = new wxTextCtrl(panel, wxID_ANY);
			btnConnect = new wxButton(panel, wxID_ANY, STR(btn_connect));
			sizer->Add(address,  {l,1}, wxDefaultSpan, wxEXPAND | wxALL, 0); l++;
			sizer->Add(name,     {l,1}, wxDefaultSpan, wxEXPAND | wxALL, 0); l++;
			{
				wxGridSizer* subsizer = new wxGridSizer(1, 2, 0, 8);
				subsizer->Add(new wxControl(panel, wxID_ANY));
				subsizer->Add(btnConnect, 0, wxEXPAND | wxLEFT, 0);
				sizer->Add(subsizer, {l,1}, wxDefaultSpan, wxEXPAND | wxALL, 0); l++;
			}


			sizer->AddGrowableCol(0);
			sizer->AddGrowableCol(1,1);
			sizer->AddGrowableCol(2,0); // expansion not working
			
			/*
			sizer->AddGrowableRow(6,0);
			sizer->AddGrowableRow(8,0);
			*/

			sizer->AddGrowableRow(10,1);

			this->FitInside(); // ask the sizer about the needed size
			this->SetScrollRate(10, 10);
		}


};

