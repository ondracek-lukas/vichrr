#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <cfloat>


struct audioStats {
	float rmsDB = -FLT_MAX;
	float peakDB = -FLT_MAX;
};

class guiAudioMeter : public wxWindow
{
		bool pressedDown;
			
		wxColour color = *wxLIGHT_GREY;
		wxTimer timer;


		struct {
			float minDB, maxDB;
			wxColour color;
		} coloredRegions[3] = {
			{-FLT_MAX, -10, wxColour(117, 215, 112)},
			{-6, -5, wxColour(255, 220, 0)},
			{ 0, 0, *wxRED}};

		float maxDBShown =   0;
		float minDBShown = -30; // adjust based on size

		audioStats stats;

			
	public:
		guiAudioMeter(wxWindow* parent);
			
		void paintEvent(wxPaintEvent & evt);
		//void paintNow();
			
		void render(wxDC& dc);
			
		// some useful events
		void mouseMoved(wxMouseEvent& event);
		void mouseDown(wxMouseEvent& event);
		void mouseWheelMoved(wxMouseEvent& event);
		void mouseReleased(wxMouseEvent& event);
		void rightClick(wxMouseEvent& event);
		void mouseLeftWindow(wxMouseEvent& event);
		void keyPressed(wxKeyEvent& event);
		void keyReleased(wxKeyEvent& event);

		void onTimer(wxTimerEvent& event);
			
		DECLARE_EVENT_TABLE()
		
	private:
		void renderArea(wxDC& dc, int w, int h, float rMinDB, float rMaxDB, bool light);
		int dbToY(float db, int h) {
			return (db - maxDBShown) / (minDBShown - maxDBShown) * h;
		}
		static wxColour mixColors(wxColour c1, wxColour c2, float ratio) {
			return wxColor(
					(1 - ratio) * c1.Red()   + ratio * c2.Red(),
					(1 - ratio) * c1.Green() + ratio * c2.Green(),
					(1 - ratio) * c1.Blue()  + ratio * c2.Blue());
		}
		
};


BEGIN_EVENT_TABLE(guiAudioMeter, wxWindow)

	EVT_MOTION(guiAudioMeter::mouseMoved)
	EVT_LEFT_DOWN(guiAudioMeter::mouseDown)
	EVT_LEFT_UP(guiAudioMeter::mouseReleased)
	EVT_RIGHT_DOWN(guiAudioMeter::rightClick)
	EVT_LEAVE_WINDOW(guiAudioMeter::mouseLeftWindow)
	EVT_KEY_DOWN(guiAudioMeter::keyPressed)
	EVT_KEY_UP(guiAudioMeter::keyReleased)
	EVT_MOUSEWHEEL(guiAudioMeter::mouseWheelMoved)
	EVT_TIMER(1, guiAudioMeter::onTimer)

	// catch paint events
	EVT_PAINT(guiAudioMeter::paintEvent)

END_EVENT_TABLE()



guiAudioMeter::guiAudioMeter(wxWindow* parent) :
 wxWindow(parent, wxID_ANY), timer(this, 1)
{
	SetMinSize({10,100});
	//this->text = "XXX";
	pressedDown = false;
	timer.Start(10);
}

void guiAudioMeter::paintEvent(wxPaintEvent & evt)
{
	wxPaintDC dc(this);
	render(dc);
}

/*
void guiAudioMeter::paintNow()
{
	// XXX depending on your system you may need to look at double-buffered dcs
	wxClientDC dc(this);
	render(dc);
}
*/

void guiAudioMeter::renderArea(wxDC& dc, int w, int h, float fromDB, float toDB, bool light) {
	float db = fromDB;
	wxColour bgColor = GetBackgroundColour();
	wxColour prevColor;
	float prevMaxDB = -FLT_MAX;
	for (auto region : coloredRegions) {
		wxColour rColor = region.color;
		if (light) rColor = mixColors(bgColor, rColor, 0.25);
		if (db < region.minDB) {
			float maxDB = std::min(region.minDB, toDB);
			if (db < maxDB) {
				dc.GradientFillLinear(
						wxRect(wxPoint(0, dbToY(maxDB, h)), wxPoint(w, dbToY(db, h))),
						mixColors(prevColor, rColor,    (db - prevMaxDB) / (region.minDB - prevMaxDB)),
						mixColors(prevColor, rColor, (maxDB - prevMaxDB) / (region.minDB - prevMaxDB)),
						wxUP);
				db = region.minDB;
			}
		}
		{
			float maxDB = std::min(region.maxDB, toDB);
			if (db < maxDB) {
				dc.GradientFillLinear(
						wxRect(wxPoint(0, dbToY(maxDB, h)), wxPoint(w, dbToY(db, h))),
						rColor, rColor,
						wxUP);
				db = maxDB;
			}
			prevColor = rColor;
			prevMaxDB = region.maxDB;
		}
	}

}

void guiAudioMeter::render(wxDC&  dc)
{
	int w, h;
	GetSize(&w, &h);
	dc.SetBrush(GetBackgroundColour());
	dc.DrawRectangle( 0, 0, w, h );

	float db1 = minDBShown, db2 = std::min(stats.rmsDB, maxDBShown);
	renderArea(dc, w, h, db1, db2, false);
	db1 = db2; db2 = std::min(stats.peakDB, maxDBShown);
	if (db1 < db2) {
		renderArea(dc, w, h, db1, db2, true);
	}

	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.DrawRectangle( 0, 0, w, h );

	wxFont font = dc.GetFont();
	font.SetPointSize(8);
	dc.SetFont(font);
	wxSize strSize = dc.GetTextExtent("-00");
	if (w > strSize.GetWidth()) {
		float strDBSize =  (float)strSize.GetHeight() / h * (maxDBShown - minDBShown);
		int dbInc = 2*strDBSize + 1;
		int dbMin = std::ceil((minDBShown + strDBSize/2) / dbInc) * dbInc;
		int dbMax = std::floor((maxDBShown - strDBSize/2) / dbInc) * dbInc;
		int lx1, lx2, lx3, lx4;
		lx2 = w/2 - strSize.GetWidth()/2 - 10;
		lx3 = w/2 + strSize.GetWidth()/2 + 10;
		lx1 = std::max(1, lx2 - 15);
		lx4 = std::min(w-1, lx3 + 15);
		lx2 = std::max(lx1+2, lx2);
		lx3 = std::min(lx4-2, lx3);
		
		for (int db = dbMin; db <= dbMax; db += dbInc) {
			wxString s = wxString::Format("%d", std::abs(db));
			wxSize sSize = dc.GetTextExtent(s);
			int y = dbToY(db, h);
			dc.DrawText(s, w/2 - sSize.GetWidth()/2, y - sSize.GetHeight()/2);
			dc.DrawLine(lx1, y, lx2, y);
			dc.DrawLine(lx3, y, lx4, y);
		}
	}

	/*
	if (pressedDown)
		dc.SetBrush( *wxRED_BRUSH );
	else
		dc.SetBrush( color );
		*/

	//dc.DrawText( text, 20, 15 );
}

void guiAudioMeter::mouseDown(wxMouseEvent& event)
{
	pressedDown = true;
	//paintNow();
}
void guiAudioMeter::mouseReleased(wxMouseEvent& event)
{
	pressedDown = false;
	//paintNow();
	
}
void guiAudioMeter::mouseLeftWindow(wxMouseEvent& event)
{
	if (pressedDown)
	{
		pressedDown = false;
		Refresh();
		// paintNow();
	}
}

// currently unused events
void guiAudioMeter::mouseMoved(wxMouseEvent& event) {
}
void guiAudioMeter::mouseWheelMoved(wxMouseEvent& event) {}
void guiAudioMeter::rightClick(wxMouseEvent& event) {}
void guiAudioMeter::keyPressed(wxKeyEvent& event) {}
void guiAudioMeter::keyReleased(wxKeyEvent& event) {}

void guiAudioMeter::onTimer(wxTimerEvent& event) {
	stats.rmsDB -= 0.05;
	if (stats.rmsDB < minDBShown - 5) stats.rmsDB = maxDBShown + 1;
	stats.peakDB = stats.rmsDB + 5;
  this->Refresh();
}
