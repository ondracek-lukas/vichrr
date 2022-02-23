
class guiStaticInfo : public wxHtmlWindow {
	private:
		const int border = 10;
		const int minWidth = 300;
	public:
		guiStaticInfo(wxWindow *parent, wxString str)
			: wxHtmlWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_NEVER | wxHW_NO_SELECTION | wxBORDER_SIMPLE) {
			str.Replace("\n", "<br>", true);
			SetBorders(border);
			SetPage("<html><body>" + str + "</body></html>");
			this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));
			this->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT));
			//Bind(wxEVT_MOUSEWHEEL, &guiStaticInfo::OnWheel, this);
		}

		bool SetPage(const wxString& source)
		{
				m_OpenedPage.clear();
				m_OpenedAnchor.clear();
				m_OpenedPageTitle.clear();
				bool ret = DoSetPage(source);
				if (m_Cell != NULL) {
					SetMinSize({minWidth, m_Cell->GetHeight() + 2*border + 8}); // XXX why just 8?
				} else {
					SetMinSize({minWidth, -1});
				}
				Refresh();
				return ret;
		}
		void OnSize(wxSizeEvent& event)
		{
			wxHtmlWindow::OnSize(event);
			int minHeight = m_Cell->GetHeight() + 2*border + 8;
			SetMinSize({minWidth, minHeight});
			/*
			{
				int x,y;
				GetSize(&x,&y);
				if (y < minHeight) {
					GetParent()->Layout();
				}
			}
			*/
		}
		/*
		void OnWheel(wxMouseEvent& event) {
			printf("wheel\n"); fflush(stdout);
			GetParent()->GetParent()->GetEventHandler()->ProcessEvent(event);
		}
		*/

		virtual bool AcceptsFocus() const { return false; }
		virtual bool AcceptsFocusFromKeyboard() const { return false; }
		virtual bool AcceptsFocusRecursively() const { return false; }
	
	private:
		wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(guiStaticInfo, wxHtmlWindow)
	EVT_SIZE(guiStaticInfo::OnSize)
	//EVT_MOUSEWHEEL(guiStaticInfo::OnWheel)
wxEND_EVENT_TABLE()



// old version based on wxStaticText
// class guiStaticInfo : public wxPanel {
// 
// 	public:
// 		/*
// 		guiStaticInfo(wxWindow *parent, wxString str) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE) {
// 			this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));
// 			this->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT));
// 			wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
// 			this->SetSizer(sizer);
// 			wxStaticText* t = new wxStaticText(this, wxID_ANY, str, wxDefaultPosition);
// 			t->Wrap(300); // XXX not all lines are shown
// 			//this->SetMaxSize({300, -1});
// 			sizer->Add(t, 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
// 			//sizer->Layout();
// 		}
// 		*/
// 		guiStaticInfo(wxWindow *parent, wxString str) : wxPanel(parent, wxID_ANY, wxDefaultPosition, {300, -1}, wxBORDER_SIMPLE) {
// 			this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));
// 			this->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT));
// 			wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
// 			this->SetSizer(sizer);
// 			myHtml* html = new myHtml(this, str, wxID_ANY, wxDefaultPosition, {300, 100});
// 			sizer->Add(html, 1, wxALL | wxEXPAND, 10);
// 			/*
// 			{
// 				int x,y;
// 				html->GetVirtualSize(&x, &y);
// 				printf("%d %d\n", x, y);
// 				this->SetMinSize({x,y});
// 			}
// 			*/
// 			//this->SetMaxSize({300, -1});
// 			//sizer->Layout();
// 		}
// 
// };
