/**********************************************************************

  Audacity: A Digital Audio Editor

  FreqWindow.cpp

  Dominic Mazzoni

*******************************************************************//**

\class FrequencyPlotDialog
\brief Displays a spectrum plot of the waveform.  Has options for
selecting parameters of the plot.

Has a feature that finds peaks and reports their value as you move
the mouse around.

*//****************************************************************//**

\class FreqPlot
\brief Works with FrequencyPlotDialog to dsplay a spectrum plot of the waveform.
This class actually does the graph display.

Has a feature that finds peaks and reports their value as you move
the mouse around.

*//****************************************************************//**

\class SpectrumAnalyst
\brief Used for finding the peaks, for snapping to peaks.

This class is used to do the 'find peaks' snapping both in FreqPlot
and in the spectrogram spectral selection.

*//*******************************************************************/

/*
  Salvo Ventura - November 2006
  Extended range check for additional FFT windows
*/


#include "Audacity.h"
#include "FreqWindow.h"

#include <algorithm>

#include <wx/setup.h> // for wxUSE_* macros

#include <wx/brush.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcclient.h>
#include <wx/font.h>
#include <wx/image.h>
#include <wx/dcmemory.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/intl.h>
#include <wx/scrolbar.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>

#include <wx/textctrl.h>
#include <wx/textfile.h>

#include <math.h>

#include "ShuttleGui.h"
#include "AColor.h"
#include "FFT.h"
#include "PitchName.h"
#include "prefs/GUISettings.h"
#include "Prefs.h"
#include "Project.h"
#include "WaveClip.h"
#include "ViewInfo.h"
#include "AllThemeResources.h"

#include "FileNames.h"

#include "WaveTrack.h"

#include "./widgets/HelpSystem.h"
#include "widgets/AudacityMessageBox.h"
#include "widgets/Ruler.h"

#if wxUSE_ACCESSIBILITY
#include "widgets/WindowAccessible.h"
#endif

DEFINE_EVENT_TYPE(EVT_FREQWINDOW_RECALC);

enum {
   FirstID = 7000,

   FreqZoomSliderID,
   FreqPanScrollerID,
   FreqExportButtonID,
   FreqAlgChoiceID,
   FreqSizeChoiceID,
   FreqFuncChoiceID,
   FreqAxisChoiceID,
   ReplotButtonID,
   GridOnOffID
};

// These specify the minimum plot window width

#define FREQ_WINDOW_WIDTH 480
#define FREQ_WINDOW_HEIGHT 330


static const char * ZoomIn[] = {
"16 16 6 1",
" 	c None",
"+	c #1C1C1C",
"@	c #AEAEAE",
"#	c #F7F7F7",
"$	c #CFCECC",
"* c #1C1CA0",
"        ++++    ",
"      @+# @$+@  ",
"      + @**  +@ ",
"     +#@ **  #+ ",
"     +@****** +@",
"     + ****** +@",
"     +#  **  #+@",
"      +  **  +@@",
"     +++#  #+@@ ",
"    +++@++++@@  ",
"   +++@@ @@@@   ",
"  +++@@         ",
" +++@@          ",
"+++@@           ",
"@+@@            ",
" @@             "};


static const char * ZoomOut[] = {
"16 16 6 1",
" 	c None",
"+	c #1C1C1C",
"@	c #AEAEAE",
"#	c #F7F7F7",
"$	c #CFCECC",
"* c #1C1CA0",
"        ++++    ",
"      @+#  $+@  ",
"      +  @@  +@ ",
"     +# @    #+ ",
"     +@****** +@",
"     + ****** +@",
"     +#      #+@",
"      +      +@@",
"     +++#  #+@@ ",
"    +++@++++@@  ",
"   +++@@ @@@@   ",
"  +++@@         ",
" +++@@          ",
"+++@@           ",
"@+@@            ",
" @@             "};

// FrequencyPlotDialog

BEGIN_EVENT_TABLE(FrequencyPlotDialog, wxDialogWrapper)
   EVT_CLOSE(FrequencyPlotDialog::OnCloseWindow)
   EVT_SIZE(FrequencyPlotDialog::OnSize)
   EVT_SLIDER(FreqZoomSliderID, FrequencyPlotDialog::OnZoomSlider)
   EVT_COMMAND_SCROLL(FreqPanScrollerID, FrequencyPlotDialog::OnPanScroller)
   EVT_CHOICE(FreqAlgChoiceID, FrequencyPlotDialog::OnAlgChoice)
   EVT_CHOICE(FreqSizeChoiceID, FrequencyPlotDialog::OnSizeChoice)
   EVT_CHOICE(FreqFuncChoiceID, FrequencyPlotDialog::OnFuncChoice)
   EVT_CHOICE(FreqAxisChoiceID, FrequencyPlotDialog::OnAxisChoice)
   EVT_BUTTON(FreqExportButtonID, FrequencyPlotDialog::OnExport)
   EVT_BUTTON(ReplotButtonID, FrequencyPlotDialog::OnReplot)
   EVT_BUTTON(wxID_CANCEL, FrequencyPlotDialog::OnCloseButton)
   EVT_BUTTON(wxID_HELP, FrequencyPlotDialog::OnGetURL)
   EVT_CHECKBOX(GridOnOffID, FrequencyPlotDialog::OnGridOnOff)
   EVT_COMMAND(wxID_ANY, EVT_FREQWINDOW_RECALC, FrequencyPlotDialog::OnRecalc)
END_EVENT_TABLE()

SpectrumAnalyst::SpectrumAnalyst()
: mAlg(Spectrum)
, mRate(0.0)
, mWindowSize(0)
{
}

SpectrumAnalyst::~SpectrumAnalyst()
{
}

FrequencyPlotDialog::FrequencyPlotDialog(wxWindow * parent, wxWindowID id,
                           const TranslatableString & title,
                           const wxPoint & pos)
:  wxDialogWrapper(parent, id, title, pos, wxDefaultSize,
            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX),
   mAnalyst(std::make_unique<SpectrumAnalyst>())
{
   SetName();

   mMouseX = 0;
   mMouseY = 0;
   mRate = 0;
   mDataLen = 0;

   p = GetActiveProject();
   if (!p)
      return;

   wxArrayStringEx algChoices{
      _("Spectrum") ,
      _("Standard Autocorrelation") ,
      _("Cuberoot Autocorrelation") ,
      _("Enhanced Autocorrelation") ,
        /* i18n-hint: This is a technical term, derived from the word
         * "spectrum".  Do not translate it unless you are sure you
         * know the correct technical word in your language. */
      _("Cepstrum") ,
   };

   wxArrayStringEx sizeChoices{
      wxT("128") ,
      wxT("256") ,
      wxT("512") ,
      wxT("1024") ,
      wxT("2048") ,
      wxT("4096") ,
      wxT("8192") ,
      wxT("16384") ,
      wxT("32768") ,
      wxT("65536") ,
   };

   wxArrayStringEx funcChoices;
   for (int i = 0, cnt = NumWindowFuncs(); i < cnt; i++)
   {
      funcChoices.push_back(
         /* i18n-hint: This refers to a "window function",
          * such as Hann or Rectangular, used in the
          * Frequency analyze dialog box. */
         XO("%s window").Format( WindowFuncName(i) ).Translation() );
   }

   wxArrayStringEx axisChoices{
      _("Linear frequency") ,
      _("Log frequency") ,
   };

   mFreqFont = wxFont(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
   mArrowCursor = std::make_unique<wxCursor>(wxCURSOR_ARROW);
   mCrossCursor = std::make_unique<wxCursor>(wxCURSOR_CROSS);

   gPrefs->Read(wxT("/FrequencyPlotDialog/DrawGrid"), &mDrawGrid, true);

   long size;
   gPrefs->Read(wxT("/FrequencyPlotDialog/SizeChoice"), &mSize, 3);
   sizeChoices[mSize].ToLong(&size);
   mWindowSize = size;

   int alg;
   gPrefs->Read(wxT("/FrequencyPlotDialog/AlgChoice"), &alg, 0);
   mAlg = static_cast<SpectrumAnalyst::Algorithm>(alg);

   gPrefs->Read(wxT("/FrequencyPlotDialog/FuncChoice"), &mFunc, 3);
   gPrefs->Read(wxT("/FrequencyPlotDialog/AxisChoice"), &mAxis, 1);
   gPrefs->Read(ENV_DB_KEY, &dBRange, ENV_DB_RANGE);
   if(dBRange < 90.)
      dBRange = 90.;

   ShuttleGui S(this, eIsCreating);

   S.SetBorder(0);

   S.AddSpace(5);

   S.SetSizerProportion(1);
   S.StartMultiColumn(3, wxEXPAND);
   {
      S.SetStretchyCol(1);
      S.SetStretchyRow(0);

      // -------------------------------------------------------------------
      // ROW 1: Freq response panel and sliders for vertical scale
      // -------------------------------------------------------------------

      S.StartVerticalLay(2);
      {
         vRuler = safenew RulerPanel(
            S.GetParent(), wxID_ANY, wxVERTICAL,
            wxSize{ 100, 100 }, // Ruler can't handle small sizes
            RulerPanel::Range{ 0.0, -dBRange },
            Ruler::LinearDBFormat,
            _("dB"),
            RulerPanel::Options{}
               .LabelEdges(true)
               .TickColour( theTheme.Colour( clrGraphLabels ) )
         );

         S.AddSpace(wxDefaultCoord, 1);
         S.Prop(1)
            .Position(wxALIGN_RIGHT | wxALIGN_TOP)
            .AddWindow(vRuler);
         S.AddSpace(wxDefaultCoord, 1);
      }
      S.EndVerticalLay();

      mFreqPlot = safenew FreqPlot(S.GetParent(), wxID_ANY);
      S.Prop(1)
         .Position(wxEXPAND)
         .MinSize( { wxDefaultCoord, FREQ_WINDOW_HEIGHT } )
         .AddWindow(mFreqPlot);

      S.StartHorizontalLay(wxEXPAND, 0);
      {
         S.StartVerticalLay();
         {
            mPanScroller = safenew wxScrollBar(S.GetParent(), FreqPanScrollerID,
               wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL);
#if wxUSE_ACCESSIBILITY
            // so that name can be set on a standard control
            mPanScroller->SetAccessible(safenew WindowAccessible(mPanScroller));
#endif
            S.Prop(1);
            S
               .Name(XO("Scroll"))
               .Position( wxALIGN_LEFT | wxTOP)
               .AddWindow(mPanScroller);
         }
         S.EndVerticalLay();

         S.StartVerticalLay();
         {
            wxStaticBitmap *zi = safenew wxStaticBitmap(S.GetParent(), wxID_ANY, wxBitmap(ZoomIn));
            S.Position(wxALIGN_CENTER)
               .AddWindow(zi);

            S.AddSpace(5);

            mZoomSlider = safenew wxSliderWrapper(S.GetParent(), FreqZoomSliderID, 100, 1, 100,
               wxDefaultPosition, wxDefaultSize, wxSL_VERTICAL);
            S.Prop(1);
            S
               .Name(XO("Zoom"))
               .Position(wxALIGN_CENTER_HORIZONTAL)
               .AddWindow(mZoomSlider);
#if wxUSE_ACCESSIBILITY
            // so that name can be set on a standard control
            mZoomSlider->SetAccessible(safenew WindowAccessible(mZoomSlider));
#endif

            S.AddSpace(5);

            wxStaticBitmap *zo = safenew wxStaticBitmap(S.GetParent(), wxID_ANY, wxBitmap(ZoomOut));
            S.Position(wxALIGN_CENTER)
               .AddWindow(zo);
         }
         S.EndVerticalLay();

         S.AddSpace(5, wxDefaultCoord);
      }
      S.EndHorizontalLay();

      // -------------------------------------------------------------------
      // ROW 2: Frequency ruler
      // -------------------------------------------------------------------

      S.AddSpace(1);

      S.StartHorizontalLay(wxEXPAND, 0);
      {
         hRuler  = safenew RulerPanel(
            S.GetParent(), wxID_ANY, wxHORIZONTAL,
            wxSize{ 100, 100 }, // Ruler can't handle small sizes
            RulerPanel::Range{ 10, 20000 },
            Ruler::RealFormat,
            _("Hz"),
            RulerPanel::Options{}
               .Log(true)
               .Flip(true)
               .LabelEdges(true)
               .TickColour( theTheme.Colour( clrGraphLabels ) )
         );

         S.AddSpace(1, wxDefaultCoord);
         S.Prop(1)
            .Position(wxALIGN_LEFT | wxALIGN_TOP)
            .AddWindow(hRuler);
         S.AddSpace(1, wxDefaultCoord);
      }
      S.EndHorizontalLay();

      S.AddSpace(1);

      // -------------------------------------------------------------------
      // ROW 3: Spacer
      // -------------------------------------------------------------------
      S.AddSpace(5);
      S.AddSpace(5);
      S.AddSpace(5);

      // -------------------------------------------------------------------
      // ROW 4: Info
      // -------------------------------------------------------------------

      S.AddSpace(1);

      S.StartHorizontalLay(wxEXPAND);
      {
         S.SetSizerProportion(1);
         S.StartMultiColumn(6);
         S.SetStretchyCol(1);
         S.SetStretchyCol(3);
         {
            S.AddPrompt(_("Cursor:"));

            mCursorText = S.Style(wxTE_READONLY)
               .AddTextBox( {}, wxT(""), 10);

            S.AddPrompt(_("Peak:"));

            mPeakText = S.Style(wxTE_READONLY)
               .AddTextBox( {}, wxT(""), 10);
            S.AddSpace(5);

            mGridOnOff = S.Id(GridOnOffID).AddCheckBox(_("&Grids"), mDrawGrid);
         }
         S.EndMultiColumn();
      }
      S.EndHorizontalLay();

      S.AddSpace(1);
   }
   S.EndMultiColumn();

   // -------------------------------------------------------------------
   // ROW 5: Spacer
   // -------------------------------------------------------------------
   
   S.AddSpace(5);

   S.SetBorder(2);
   S.SetSizerProportion(0);
   S.StartMultiColumn(9, wxALIGN_CENTER);
   {
      // ----------------------------------------------------------------
      // ROW 6: Algorithm, Size, Export, Replot
      // ----------------------------------------------------------------

      S.AddSpace(5);

      mAlgChoice = S.Id(FreqAlgChoiceID).Focus()
         .MinSize( { wxDefaultCoord, wxDefaultCoord } )
         .AddChoice(_("&Algorithm:"), algChoices, mAlg);

      S.AddSpace(5);

      mSizeChoice = S.Id(FreqSizeChoiceID)
         .MinSize( { wxDefaultCoord, wxDefaultCoord } )
         .AddChoice(_("&Size:"), sizeChoices, mSize);

      S.AddSpace(5);

      mExportButton = S.Id(FreqExportButtonID).AddButton(_("&Export..."));

      S.AddSpace(5);


      // ----------------------------------------------------------------
      // ROW 7: Function, Axix, Grids, Close
      // ----------------------------------------------------------------

      S.AddSpace(5);

      mFuncChoice = S.Id(FreqFuncChoiceID)
         .MinSize( { wxDefaultCoord, wxDefaultCoord } )
         .AddChoice(_("&Function:"), funcChoices, mFunc);
      mFuncChoice->MoveAfterInTabOrder(mSizeChoice);

      S.AddSpace(5);

      mAxisChoice = S.Id(FreqAxisChoiceID)
         .MinSize( { wxDefaultCoord, wxDefaultCoord } )
         .AddChoice(_("&Axis:"), axisChoices, mAxis);
      mAxisChoice->MoveAfterInTabOrder(mFuncChoice);

      S.AddSpace(5);

      mReplotButton = S.Id(ReplotButtonID).AddButton(_("&Replot..."));

      S.AddSpace(5);

      //mCloseButton = S.Id(wxID_CANCEL).AddButton(_("&Close"));

      //S.AddSpace(5);
   }
   S.EndMultiColumn();
   S.AddStandardButtons( eHelpButton | eCloseButton );

   // -------------------------------------------------------------------
   // ROW 8: Spacer
   // -------------------------------------------------------------------

   S.AddSpace(5);

   mProgress = safenew FreqGauge(S.GetParent(), wxID_ANY); //, wxST_SIZEGRIP);
   S.Position(wxEXPAND)
      .AddWindow(mProgress);

   // Log-frequency axis works for spectrum plots only.
   if (mAlg != SpectrumAnalyst::Spectrum)
   {
      mAxis = 0;
      mAxisChoice->Disable();
   }
   mLogAxis = mAxis != 0;

   mCloseButton = static_cast<wxButton*>(FindWindowById( wxID_CANCEL ));
   mCloseButton->SetDefault();

   Layout();
   Fit();
   // Bug 1607:
   Center();

   SetMinSize(GetSize());

#if defined(__WXGTK__)
   // This should be rechecked with wx3.
   //
   // The scrollbar (focus some reason) doesn't allow tabbing past it
   // because it can't receive focus.  So, convince it otherwise.
   //
   // Unfortunately, this still doesn't let you adjust the scrollbar
   // from the keyboard.  Near as I can tell, wxWGTK is capturing the
   // keyboard input, so the GTK widget doesn't see it, preventing
   // the normal scroll events from being generated.
   //
   // I guess the only way round it would be to handle key actions
   // ourselves, but we'll leave that for a future date.
//   gtk_widget_set_can_focus(mPanScroller->m_widget, true);
#endif
}

FrequencyPlotDialog::~FrequencyPlotDialog()
{
}

void FrequencyPlotDialog::OnGetURL(wxCommandEvent & WXUNUSED(event))
{
   // Original help page is back on-line (March 2016), but the manual should be more reliable.
   // http://www.eramp.com/WCAG_2_audio_contrast_tool_help.htm
   HelpSystem::ShowHelp(this, wxT("Plot Spectrum"));
}

bool FrequencyPlotDialog::Show(bool show)
{
   if (!show)
   {
      mFreqPlot->SetCursor(*mArrowCursor);
   }

   bool shown = IsShown();

   if (show && !shown)
   {
      gPrefs->Read(ENV_DB_KEY, &dBRange, ENV_DB_RANGE);
      if(dBRange < 90.)
         dBRange = 90.;
      GetAudio();
      // Don't send an event.  We need the recalc right away.
      // so that mAnalyst is valid when we paint.
      //SendRecalcEvent();
      Recalc();
   }

   bool res = wxDialogWrapper::Show(show);

   return res;
}

void FrequencyPlotDialog::GetAudio()
{
   mData.reset();
   mDataLen = 0;

   int selcount = 0;
   bool warning = false;
   for (auto track : TrackList::Get( *p ).Selected< const WaveTrack >()) {
      auto &selectedRegion = ViewInfo::Get( *p ).selectedRegion;
      if (selcount==0) {
         mRate = track->GetRate();
         auto start = track->TimeToLongSamples(selectedRegion.t0());
         auto end = track->TimeToLongSamples(selectedRegion.t1());
         auto dataLen = end - start;
         if (dataLen > 10485760) {
            warning = true;
            mDataLen = 10485760;
         }
         else
            // dataLen is not more than 10 * 2 ^ 20
            mDataLen = dataLen.as_size_t();
         mData = Floats{ mDataLen };
         // Don't allow throw for bad reads
         track->Get((samplePtr)mData.get(), floatSample, start, mDataLen,
                    fillZero, false);
      }
      else {
         if (track->GetRate() != mRate) {
            AudacityMessageBox(
               XO(
"To plot the spectrum, all selected tracks must be the same sample rate.") );
            mData.reset();
            mDataLen = 0;
            return;
         }
         auto start = track->TimeToLongSamples(selectedRegion.t0());
         Floats buffer2{ mDataLen };
         // Again, stop exceptions
         track->Get((samplePtr)buffer2.get(), floatSample, start, mDataLen,
                    fillZero, false);
         for (size_t i = 0; i < mDataLen; i++)
            mData[i] += buffer2[i];
      }
      selcount++;
   }

   if (selcount == 0)
      return;

   if (warning) {
      auto msg = XO(
"Too much audio was selected. Only the first %.1f seconds of audio will be analyzed.")
         .Format(mDataLen / mRate);
      AudacityMessageBox( msg );
   }
}

void FrequencyPlotDialog::OnSize(wxSizeEvent & WXUNUSED(event))
{
   Layout();

   DrawPlot();

   Refresh(true);
}

void FrequencyPlotDialog::DrawBackground(wxMemoryDC & dc)
{
   Layout();

   mBitmap.reset();

   mPlotRect = mFreqPlot->GetClientRect();

   mBitmap = std::make_unique<wxBitmap>(mPlotRect.width, mPlotRect.height,24);

   dc.SelectObject(*mBitmap);

   dc.SetBackground(wxBrush(wxColour(254, 254, 254)));// DONT-THEME Mask colour.
   dc.Clear();

   dc.SetPen(*wxBLACK_PEN);
   dc.SetBrush(*wxWHITE_BRUSH);
   dc.DrawRectangle(mPlotRect);

   dc.SetFont(mFreqFont);
}

void FrequencyPlotDialog::DrawPlot()
{
   if (!mData || mDataLen < mWindowSize || mAnalyst->GetProcessedSize() == 0) {
      wxMemoryDC memDC;

      vRuler->ruler.SetLog(false);
      vRuler->ruler.SetRange(0.0, -dBRange);

      hRuler->ruler.SetLog(false);
      hRuler->ruler.SetRange(0, 1);

      DrawBackground(memDC);

      if (mDataLen < mWindowSize) {
         wxString msg = _("Not enough data selected.");
         wxSize sz = memDC.GetTextExtent(msg);
         memDC.DrawText(msg,
                        (mPlotRect.GetWidth() - sz.GetWidth()) / 2,
                        (mPlotRect.GetHeight() - sz.GetHeight()) / 2);
      }

      memDC.SelectObject(wxNullBitmap);
      
      mFreqPlot->Refresh();

      Refresh();

      return;
   }

   float yRange = mYMax - mYMin;
   float yTotal = yRange * ((float) mZoomSlider->GetValue() / 100.0f);

   int sTotal = yTotal * 100;
   int sRange = yRange * 100;
   int sPos = mPanScroller->GetThumbPosition() + ((mPanScroller->GetThumbSize() - sTotal) / 2);
    mPanScroller->SetScrollbar(sPos, sTotal, sRange, sTotal);

   float yMax = mYMax - ((float)sPos / 100);
   float yMin = yMax - yTotal;

   // Set up y axis ruler

   if (mAlg == SpectrumAnalyst::Spectrum) {
      vRuler->ruler.SetUnits(_("dB"));
      vRuler->ruler.SetFormat(Ruler::LinearDBFormat);
   } else {
      vRuler->ruler.SetUnits(wxT(""));
      vRuler->ruler.SetFormat(Ruler::RealFormat);
   }
   int w1, w2, h;
   vRuler->ruler.GetMaxSize(&w1, &h);
   vRuler->ruler.SetRange(yMax, yMin); // Note inversion for vertical.
   vRuler->ruler.GetMaxSize(&w2, &h);
   if( w1 != w2 )   // Reduces flicker
   {
      vRuler->SetMinSize(wxSize(w2,h));
      Layout();
   }
   vRuler->Refresh(false);

   wxMemoryDC memDC;
   DrawBackground(memDC);

   // Get the plot dimensions
   //
   // Must be done after setting the vertical ruler above since the
   // the width could change.
   wxRect r = mPlotRect;

   // Set up x axis ruler

   int width = r.width - 2;

   float xMin, xMax, xRatio, xStep;

   if (mAlg == SpectrumAnalyst::Spectrum) {
      xMin = mRate / mWindowSize;
      xMax = mRate / 2;
      xRatio = xMax / xMin;
      if (mLogAxis)
      {
         xStep = pow(2.0f, (log(xRatio) / log(2.0f)) / width);
         hRuler->ruler.SetLog(true);
      }
      else
      {
         xStep = (xMax - xMin) / width;
         hRuler->ruler.SetLog(false);
      }
      hRuler->ruler.SetUnits(_("Hz"));
   } else {
      xMin = 0;
      xMax = mAnalyst->GetProcessedSize() / mRate;
      xStep = (xMax - xMin) / width;
      hRuler->ruler.SetLog(false);
      hRuler->ruler.SetUnits(_("s"));
   }
   hRuler->ruler.SetRange(xMin, xMax-xStep);
   hRuler->Refresh(false);

   // Draw the plot
   if (mAlg == SpectrumAnalyst::Spectrum)
      memDC.SetPen(wxPen(theTheme.Colour( clrHzPlot ), 1, wxPENSTYLE_SOLID));
   else
      memDC.SetPen(wxPen(theTheme.Colour( clrWavelengthPlot), 1, wxPENSTYLE_SOLID));

   float xPos = xMin;

   for (int i = 0; i < width; i++) {
      float y;

      if (mLogAxis)
         y = mAnalyst->GetProcessedValue(xPos, xPos * xStep);
      else
         y = mAnalyst->GetProcessedValue(xPos, xPos + xStep);

      float ynorm = (y - yMin) / yTotal;

      int lineheight = (int)(ynorm * (r.height - 1));

      if (lineheight > r.height - 2)
         lineheight = r.height - 2;

      if (ynorm > 0.0)
         AColor::Line(memDC, r.x + 1 + i, r.y + r.height - 1 - lineheight,
                        r.x + 1 + i, r.y + r.height - 1);

      if (mLogAxis)
         xPos *= xStep;
      else
         xPos += xStep;
   }

   // Outline the graph
   memDC.SetPen(*wxBLACK_PEN);
   memDC.SetBrush(*wxTRANSPARENT_BRUSH);
   memDC.DrawRectangle(r);

   if(mDrawGrid)
   {
      hRuler->ruler.DrawGrid(memDC, r.height, true, true, 1, 1);
      vRuler->ruler.DrawGrid(memDC, r.width, true, true, 1, 1);
   }

   memDC.SelectObject( wxNullBitmap );

   mFreqPlot->Refresh();
}


void FrequencyPlotDialog::PlotMouseEvent(wxMouseEvent & event)
{
   if (event.Moving() && (event.m_x != mMouseX || event.m_y != mMouseY)) {
      mMouseX = event.m_x;
      mMouseY = event.m_y;

      if (mPlotRect.Contains(mMouseX, mMouseY))
         mFreqPlot->SetCursor(*mCrossCursor);
      else
         mFreqPlot->SetCursor(*mArrowCursor);

      mFreqPlot->Refresh(false);
   }
}

void FrequencyPlotDialog::OnPanScroller(wxScrollEvent & WXUNUSED(event))
{
   DrawPlot();
}

void FrequencyPlotDialog::OnZoomSlider(wxCommandEvent & WXUNUSED(event))
{
   DrawPlot();
}

void FrequencyPlotDialog::OnAlgChoice(wxCommandEvent & WXUNUSED(event))
{
   mAlg = SpectrumAnalyst::Algorithm(mAlgChoice->GetSelection());

   // Log-frequency axis works for spectrum plots only.
   if (mAlg == SpectrumAnalyst::Spectrum) {
      mAxisChoice->Enable(true);
      mLogAxis = mAxisChoice->GetSelection() ? true : false;
   }
   else {
      mAxisChoice->Disable();
      mLogAxis = false;
   }

   SendRecalcEvent();
}

void FrequencyPlotDialog::OnSizeChoice(wxCommandEvent & WXUNUSED(event))
{
   long windowSize = 0;
   mSizeChoice->GetStringSelection().ToLong(&windowSize);
   mWindowSize = windowSize;

   SendRecalcEvent();
}

void FrequencyPlotDialog::OnFuncChoice(wxCommandEvent & WXUNUSED(event))
{
   SendRecalcEvent();
}

void FrequencyPlotDialog::OnAxisChoice(wxCommandEvent & WXUNUSED(event))
{
   mLogAxis = mAxisChoice->GetSelection() ? true : false;
   DrawPlot();
}

void FrequencyPlotDialog::PlotPaint(wxPaintEvent & event)
{
   wxPaintDC dc( (wxWindow *) event.GetEventObject() );

   dc.DrawBitmap( *mBitmap, 0, 0, true );
   // Fix for Bug 1226 "Plot Spectrum freezes... if insufficient samples selected"
   if (!mData || mDataLen < mWindowSize)
      return;

   dc.SetFont(mFreqFont);

   wxRect r = mPlotRect;

   int width = r.width - 2;

   float xMin, xMax, xRatio, xStep;

   if (mAlg == SpectrumAnalyst::Spectrum) {
      xMin = mRate / mWindowSize;
      xMax = mRate / 2;
      xRatio = xMax / xMin;
      if (mLogAxis)
         xStep = pow(2.0f, (log(xRatio) / log(2.0f)) / width);
      else
         xStep = (xMax - xMin) / width;
   } else {
      xMin = 0;
      xMax = mAnalyst->GetProcessedSize() / mRate;
      xStep = (xMax - xMin) / width;
   }

   float xPos = xMin;

   // Find the peak nearest the cursor and plot it
   if ( r.Contains(mMouseX, mMouseY) & (mMouseX!=0) & (mMouseX!=r.width-1) ) {
      if (mLogAxis)
         xPos = xMin * pow(xStep, mMouseX - (r.x + 1));
      else
         xPos = xMin + xStep * (mMouseX - (r.x + 1));

      float bestValue = 0;
      float bestpeak = mAnalyst->FindPeak(xPos, &bestValue);

      int px;
      if (mLogAxis)
         px = (int)(log(bestpeak / xMin) / log(xStep));
      else
         px = (int)((bestpeak - xMin) * width / (xMax - xMin));

      dc.SetPen(wxPen(wxColour(160,160,160), 1, wxPENSTYLE_SOLID));
      AColor::Line(dc, r.x + 1 + px, r.y, r.x + 1 + px, r.y + r.height);

       // print out info about the cursor location

      float value;

      if (mLogAxis) {
         xPos = xMin * pow(xStep, mMouseX - (r.x + 1));
         value = mAnalyst->GetProcessedValue(xPos, xPos * xStep);
      } else {
         xPos = xMin + xStep * (mMouseX - (r.x + 1));
         value = mAnalyst->GetProcessedValue(xPos, xPos + xStep);
      }

      TranslatableString cursor;
      TranslatableString peak;

      if (mAlg == SpectrumAnalyst::Spectrum) {
         auto xp = PitchName_Absolute(FreqToMIDInote(xPos));
         auto pp = PitchName_Absolute(FreqToMIDInote(bestpeak));
         /* i18n-hint: The %d's are replaced by numbers, the %s by musical notes, e.g. A#*/
         cursor = XO("%d Hz (%s) = %d dB")
            .Format( (int)(xPos + 0.5), xp, (int)(value + 0.5));
         /* i18n-hint: The %d's are replaced by numbers, the %s by musical notes, e.g. A#*/
         peak = XO("%d Hz (%s) = %.1f dB")
            .Format( (int)(bestpeak + 0.5), pp, bestValue );
      } else if (xPos > 0.0 && bestpeak > 0.0) {
         auto xp = PitchName_Absolute(FreqToMIDInote(1.0 / xPos));
         auto pp = PitchName_Absolute(FreqToMIDInote(1.0 / bestpeak));
         /* i18n-hint: The %d's are replaced by numbers, the %s by musical notes, e.g. A#
          * the %.4f are numbers, and 'sec' should be an abbreviation for seconds */
         cursor = XO("%.4f sec (%d Hz) (%s) = %f")
            .Format( xPos, (int)(1.0 / xPos + 0.5), xp, value );
         /* i18n-hint: The %d's are replaced by numbers, the %s by musical notes, e.g. A#
          * the %.4f are numbers, and 'sec' should be an abbreviation for seconds */
         peak = XO("%.4f sec (%d Hz) (%s) = %.3f")
            .Format( bestpeak, (int)(1.0 / bestpeak + 0.5), pp, bestValue );
      }
      mCursorText->SetValue( cursor.Translation() );
      mPeakText->SetValue( peak.Translation() );
   }
   else {
      mCursorText->SetValue(wxT(""));
      mPeakText->SetValue(wxT(""));
   }


   // Outline the graph
   dc.SetPen(*wxBLACK_PEN);
   dc.SetBrush(*wxTRANSPARENT_BRUSH);
   dc.DrawRectangle(r);
}

void FrequencyPlotDialog::OnCloseWindow(wxCloseEvent & WXUNUSED(event))
{
   Show(false);
}

void FrequencyPlotDialog::OnCloseButton(wxCommandEvent & WXUNUSED(event))
{
   gPrefs->Write(wxT("/FrequencyPlotDialog/DrawGrid"), mDrawGrid);
   gPrefs->Write(wxT("/FrequencyPlotDialog/SizeChoice"), mSizeChoice->GetSelection());
   gPrefs->Write(wxT("/FrequencyPlotDialog/AlgChoice"), mAlgChoice->GetSelection());
   gPrefs->Write(wxT("/FrequencyPlotDialog/FuncChoice"), mFuncChoice->GetSelection());
   gPrefs->Write(wxT("/FrequencyPlotDialog/AxisChoice"), mAxisChoice->GetSelection());
   gPrefs->Flush();
   Show(false);
}

void FrequencyPlotDialog::SendRecalcEvent()
{
   wxCommandEvent e(EVT_FREQWINDOW_RECALC, wxID_ANY);
   GetEventHandler()->AddPendingEvent(e);
}

void FrequencyPlotDialog::Recalc()
{
   if (!mData || mDataLen < mWindowSize) {
      DrawPlot();
      return;
   }

   SpectrumAnalyst::Algorithm alg =
      SpectrumAnalyst::Algorithm(mAlgChoice->GetSelection());
   int windowFunc = mFuncChoice->GetSelection();

   wxWindow *hadFocus = FindFocus();
   // In wxMac, the skipped window MUST be a top level window.  I'd originally made it
   // just the mProgress window with the idea of preventing user interaction with the
   // controls while the plot was being recalculated.  This doesn't appear to be necessary
   // so just use the the top level window instead.
   {
      Maybe<wxWindowDisabler> blocker;
      if (IsShown())
         blocker.create(this);
      wxYieldIfNeeded();

      mAnalyst->Calculate(alg, windowFunc, mWindowSize, mRate,
         mData.get(), mDataLen,
         &mYMin, &mYMax, mProgress);
   }
   if (hadFocus) {
      hadFocus->SetFocus();
   }

   if (alg == SpectrumAnalyst::Spectrum) {
      if(mYMin < -dBRange)
         mYMin = -dBRange;
      if(mYMax <= -dBRange)
         mYMax = -dBRange + 10.; // it's all out of range, but show a scale.
      else
         mYMax += .5;
   }

   // Prime the scrollbar
   mPanScroller->SetScrollbar(0, (mYMax - mYMin) * 100, (mYMax - mYMin) * 100, 1);

   DrawPlot();
}

void FrequencyPlotDialog::OnExport(wxCommandEvent & WXUNUSED(event))
{
   wxString fName = _("spectrum.txt");

   fName = FileNames::SelectFile(FileNames::Operation::Export,
                                 XO("Export Spectral Data As:"),
                                 wxEmptyString,
                                 fName,
                                 wxT("txt"),
                                 _("Text files (*.txt)|*.txt|All files|*"),
                                 wxFD_SAVE | wxRESIZE_BORDER,
                                 this);

   if (fName.empty())
      return;

   wxTextFile f(fName);
#ifdef __WXMAC__
   wxFile{}.Create(fName);
#else
   f.Create();
#endif
   f.Open();
   if (!f.IsOpened()) {
      AudacityMessageBox( XO("Couldn't write to file: %s").Format( fName ) );
      return;
   }

   const int processedSize = mAnalyst->GetProcessedSize();
   const float *const processed = mAnalyst->GetProcessed();
   if (mAlgChoice->GetSelection() == 0) {
      f.AddLine(_("Frequency (Hz)\tLevel (dB)"));
      for (int i = 1; i < processedSize; i++)
         f.AddLine(wxString::
                   Format(wxT("%f\t%f"), i * mRate / mWindowSize,
                          processed[i]));
   } else {
      f.AddLine(_("Lag (seconds)\tFrequency (Hz)\tLevel"));
      for (int i = 1; i < processedSize; i++)
         f.AddLine(wxString::Format(wxT("%f\t%f\t%f"),
                                    i / mRate, mRate / i, processed[i]));
   }

#ifdef __WXMAC__
   f.Write(wxTextFileType_Mac);
#else
   f.Write();
#endif
   f.Close();
}

void FrequencyPlotDialog::OnReplot(wxCommandEvent & WXUNUSED(event))
{
   gPrefs->Read(ENV_DB_KEY, &dBRange, ENV_DB_RANGE);
   if(dBRange < 90.)
      dBRange = 90.;
   GetAudio();
   SendRecalcEvent();
}

void FrequencyPlotDialog::OnGridOnOff(wxCommandEvent & WXUNUSED(event))
{
   mDrawGrid = mGridOnOff->IsChecked();

   DrawPlot();
}

void FrequencyPlotDialog::OnRecalc(wxCommandEvent & WXUNUSED(event))
{
   Recalc();
}

BEGIN_EVENT_TABLE(FreqPlot, wxWindow)
   EVT_ERASE_BACKGROUND(FreqPlot::OnErase)
   EVT_PAINT(FreqPlot::OnPaint)
   EVT_MOUSE_EVENTS(FreqPlot::OnMouseEvent)
END_EVENT_TABLE()

FreqPlot::FreqPlot(wxWindow *parent, wxWindowID winid)
:  wxWindow(parent, winid)
{
   freqWindow = (FrequencyPlotDialog *) parent;
}

bool FreqPlot::AcceptsFocus() const
{
   return false;
}

void FreqPlot::OnErase(wxEraseEvent & WXUNUSED(event))
{
   // Ignore it to prevent flashing
}

void FreqPlot::OnPaint(wxPaintEvent & evt)
{
   freqWindow->PlotPaint(evt);
}

void FreqPlot::OnMouseEvent(wxMouseEvent & event)
{
   freqWindow->PlotMouseEvent(event);
}

FreqGauge::FreqGauge(wxWindow * parent, wxWindowID winid)
:  wxStatusBar(parent, winid, wxST_SIZEGRIP)
{
   mRange = 0;
}

void FreqGauge::SetRange(int range, int bar, int gap)
{
   mRange = range;
   mBar = bar;
   mGap = gap;

   GetFieldRect(0, mRect);
   mRect.Inflate(-1);

   mInterval = mRange / (mRect.width / (mBar + mGap));
   mRect.width = mBar;
   mMargin = mRect.x;
   mLast = -1;

   Update();
}

void FreqGauge::SetValue(int value)
{
   mCur = value / mInterval;

   if (mCur != mLast)
   {
      wxClientDC dc(this);
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.SetBrush(wxColour(100, 100, 220));

      while (mLast < mCur)
      {
         mLast++;
         mRect.x = mMargin + mLast * (mBar + mGap);
         dc.DrawRectangle(mRect);
      }
      Update();
   }
}

void FreqGauge::Reset()
{
   mRange = 0;
   Refresh(true);
}

bool SpectrumAnalyst::Calculate(Algorithm alg, int windowFunc,
                                size_t windowSize, double rate,
                                const float *data, size_t dataLen,
                                float *pYMin, float *pYMax,
                                FreqGauge *progress)
{
   // Wipe old data
   mProcessed.resize(0);
   mRate = 0.0;
   mWindowSize = 0;

   // Validate inputs
   int f = NumWindowFuncs();

   if (!(windowSize >= 32 && windowSize <= 65536 &&
         alg >= SpectrumAnalyst::Spectrum &&
         alg < SpectrumAnalyst::NumAlgorithms &&
         windowFunc >= 0 && windowFunc < f)) {
      return false;
   }

   if (dataLen < windowSize) {
      return false;
   }

   // Now repopulate
   mRate = rate;
   mWindowSize = windowSize;
   mAlg = alg;

   auto half = mWindowSize / 2;
   mProcessed.resize(mWindowSize);

   Floats in{ mWindowSize };
   Floats out{ mWindowSize };
   Floats out2{ mWindowSize };
   Floats win{ mWindowSize };

   for (size_t i = 0; i < mWindowSize; i++) {
      mProcessed[i] = 0.0f;
      win[i] = 1.0f;
   }

   WindowFunc(windowFunc, mWindowSize, win.get());

   // Scale window such that an amplitude of 1.0 in the time domain
   // shows an amplitude of 0dB in the frequency domain
   double wss = 0;
   for (size_t i = 0; i<mWindowSize; i++)
      wss += win[i];
   if(wss > 0)
      wss = 4.0 / (wss*wss);
   else
      wss = 1.0;

   if (progress) {
      progress->SetRange(dataLen);
   }

   size_t start = 0;
   int windows = 0;
   while (start + mWindowSize <= dataLen) {
      for (size_t i = 0; i < mWindowSize; i++)
         in[i] = win[i] * data[start + i];

      switch (alg) {
         case Spectrum:
            PowerSpectrum(mWindowSize, in.get(), out.get());

            for (size_t i = 0; i < half; i++)
               mProcessed[i] += out[i];
            break;

         case Autocorrelation:
         case CubeRootAutocorrelation:
         case EnhancedAutocorrelation:

            // Take FFT
            RealFFT(mWindowSize, in.get(), out.get(), out2.get());
            // Compute power
            for (size_t i = 0; i < mWindowSize; i++)
               in[i] = (out[i] * out[i]) + (out2[i] * out2[i]);

            if (alg == Autocorrelation) {
               for (size_t i = 0; i < mWindowSize; i++)
                  in[i] = sqrt(in[i]);
            }
            if (alg == CubeRootAutocorrelation ||
                alg == EnhancedAutocorrelation) {
               // Tolonen and Karjalainen recommend taking the cube root
               // of the power, instead of the square root

               for (size_t i = 0; i < mWindowSize; i++)
                  in[i] = pow(in[i], 1.0f / 3.0f);
            }
            // Take FFT
            RealFFT(mWindowSize, in.get(), out.get(), out2.get());

            // Take real part of result
            for (size_t i = 0; i < half; i++)
               mProcessed[i] += out[i];
            break;

         case Cepstrum:
            RealFFT(mWindowSize, in.get(), out.get(), out2.get());

            // Compute log power
            // Set a sane lower limit assuming maximum time amplitude of 1.0
            {
               float power;
               float minpower = 1e-20*mWindowSize*mWindowSize;
               for (size_t i = 0; i < mWindowSize; i++)
               {
                  power = (out[i] * out[i]) + (out2[i] * out2[i]);
                  if(power < minpower)
                     in[i] = log(minpower);
                  else
                     in[i] = log(power);
               }
               // Take IFFT
               InverseRealFFT(mWindowSize, in.get(), NULL, out.get());

               // Take real part of result
               for (size_t i = 0; i < half; i++)
                  mProcessed[i] += out[i];
            }

            break;

         default:
            wxASSERT(false);
            break;
      }                         //switch

      // Update the progress bar
      if (progress) {
         progress->SetValue(start);
      }

      start += half;
      windows++;
   }

   if (progress) {
      // Reset for next time
      progress->Reset();
   }

   float mYMin = 1000000, mYMax = -1000000;
   double scale;
   switch (alg) {
   case Spectrum:
      // Convert to decibels
      mYMin = 1000000.;
      mYMax = -1000000.;
      scale = wss / (double)windows;
      for (size_t i = 0; i < half; i++)
      {
         mProcessed[i] = 10 * log10(mProcessed[i] * scale);
         if(mProcessed[i] > mYMax)
            mYMax = mProcessed[i];
         else if(mProcessed[i] < mYMin)
            mYMin = mProcessed[i];
      }
      break;

   case Autocorrelation:
   case CubeRootAutocorrelation:
      for (size_t i = 0; i < half; i++)
         mProcessed[i] = mProcessed[i] / windows;

      // Find min/max
      mYMin = mProcessed[0];
      mYMax = mProcessed[0];
      for (size_t i = 1; i < half; i++)
         if (mProcessed[i] > mYMax)
            mYMax = mProcessed[i];
         else if (mProcessed[i] < mYMin)
            mYMin = mProcessed[i];
      break;

   case EnhancedAutocorrelation:
      for (size_t i = 0; i < half; i++)
         mProcessed[i] = mProcessed[i] / windows;

      // Peak Pruning as described by Tolonen and Karjalainen, 2000

      // Clip at zero, copy to temp array
      for (size_t i = 0; i < half; i++) {
         if (mProcessed[i] < 0.0)
            mProcessed[i] = float(0.0);
         out[i] = mProcessed[i];
      }

      // Subtract a time-doubled signal (linearly interp.) from the original
      // (clipped) signal
      for (size_t i = 0; i < half; i++)
         if ((i % 2) == 0)
            mProcessed[i] -= out[i / 2];
         else
            mProcessed[i] -= ((out[i / 2] + out[i / 2 + 1]) / 2);

      // Clip at zero again
      for (size_t i = 0; i < half; i++)
         if (mProcessed[i] < 0.0)
            mProcessed[i] = float(0.0);

      // Find NEW min/max
      mYMin = mProcessed[0];
      mYMax = mProcessed[0];
      for (size_t i = 1; i < half; i++)
         if (mProcessed[i] > mYMax)
            mYMax = mProcessed[i];
         else if (mProcessed[i] < mYMin)
            mYMin = mProcessed[i];
      break;

   case Cepstrum:
      for (size_t i = 0; i < half; i++)
         mProcessed[i] = mProcessed[i] / windows;

      // Find min/max, ignoring first and last few values
      {
         size_t ignore = 4;
         mYMin = mProcessed[ignore];
         mYMax = mProcessed[ignore];
         for (size_t i = ignore + 1; i + ignore < half; i++)
            if (mProcessed[i] > mYMax)
               mYMax = mProcessed[i];
            else if (mProcessed[i] < mYMin)
               mYMin = mProcessed[i];
      }
      break;

   default:
      wxASSERT(false);
      break;
   }

   if (pYMin)
      *pYMin = mYMin;
   if (pYMax)
      *pYMax = mYMax;

   return true;
}

const float *SpectrumAnalyst::GetProcessed() const
{
   return &mProcessed[0];
}

int SpectrumAnalyst::GetProcessedSize() const
{
   return mProcessed.size() / 2;
}

float SpectrumAnalyst::GetProcessedValue(float freq0, float freq1) const
{
   float bin0, bin1, binwidth;

   if (mAlg == Spectrum) {
      bin0 = freq0 * mWindowSize / mRate;
      bin1 = freq1 * mWindowSize / mRate;
   } else {
      bin0 = freq0 * mRate;
      bin1 = freq1 * mRate;
   }
   binwidth = bin1 - bin0;

   float value = float(0.0);

   if (binwidth < 1.0) {
      float binmid = (bin0 + bin1) / 2.0;
      int ibin = (int)(binmid) - 1;
      if (ibin < 1)
         ibin = 1;
      if (ibin >= GetProcessedSize() - 3)
         ibin = std::max(0, GetProcessedSize() - 4);

      value = CubicInterpolate(mProcessed[ibin],
                               mProcessed[ibin + 1],
                               mProcessed[ibin + 2],
                               mProcessed[ibin + 3], binmid - ibin);

   } else {
      if (bin0 < 0)
         bin0 = 0;
      if (bin1 >= GetProcessedSize())
         bin1 = GetProcessedSize() - 1;

      if ((int)(bin1) > (int)(bin0))
         value += mProcessed[(int)(bin0)] * ((int)(bin0) + 1 - bin0);
      bin0 = 1 + (int)(bin0);
      while (bin0 < (int)(bin1)) {
         value += mProcessed[(int)(bin0)];
         bin0 += 1.0;
      }
      value += mProcessed[(int)(bin1)] * (bin1 - (int)(bin1));

      value /= binwidth;
   }

   return value;
}

float SpectrumAnalyst::FindPeak(float xPos, float *pY) const
{
   float bestpeak = 0.0f;
   float bestValue = 0.0;
   if (GetProcessedSize() > 1) {
      bool up = (mProcessed[1] > mProcessed[0]);
      float bestdist = 1000000;
      for (int bin = 3; bin < GetProcessedSize() - 1; bin++) {
         bool nowUp = mProcessed[bin] > mProcessed[bin - 1];
         if (!nowUp && up) {
            // Local maximum.  Find actual value by cubic interpolation
            int leftbin = bin - 2;
            /*
            if (leftbin < 1)
               leftbin = 1;
               */
            float valueAtMax = 0.0;
            float max = leftbin + CubicMaximize(mProcessed[leftbin],
                                                mProcessed[leftbin + 1],
                                                mProcessed[leftbin + 2],
                                                mProcessed[leftbin + 3],
                                                &valueAtMax);

            float thispeak;
            if (mAlg == Spectrum)
               thispeak = max * mRate / mWindowSize;
            else
               thispeak = max / mRate;

            if (fabs(thispeak - xPos) < bestdist) {
               bestpeak = thispeak;
               bestdist = fabs(thispeak - xPos);
               bestValue = valueAtMax;
               // Should this test come after the enclosing if?
               if (thispeak > xPos)
                  break;
            }
         }
         up = nowUp;
      }
   }

   if (pY)
      *pY = bestValue;
   return bestpeak;
}

// If f(0)=y0, f(1)=y1, f(2)=y2, and f(3)=y3, this function finds
// the degree-three polynomial which best fits these points and
// returns the value of this polynomial at a value x.  Usually
// 0 < x < 3

float SpectrumAnalyst::CubicInterpolate(float y0, float y1, float y2, float y3, float x) const
{
   float a, b, c, d;

   a = y0 / -6.0 + y1 / 2.0 - y2 / 2.0 + y3 / 6.0;
   b = y0 - 5.0 * y1 / 2.0 + 2.0 * y2 - y3 / 2.0;
   c = -11.0 * y0 / 6.0 + 3.0 * y1 - 3.0 * y2 / 2.0 + y3 / 3.0;
   d = y0;

   float xx = x * x;
   float xxx = xx * x;

   return (a * xxx + b * xx + c * x + d);
}

float SpectrumAnalyst::CubicMaximize(float y0, float y1, float y2, float y3, float * max) const
{
   // Find coefficients of cubic

   float a, b, c, d;

   a = y0 / -6.0 + y1 / 2.0 - y2 / 2.0 + y3 / 6.0;
   b = y0 - 5.0 * y1 / 2.0 + 2.0 * y2 - y3 / 2.0;
   c = -11.0 * y0 / 6.0 + 3.0 * y1 - 3.0 * y2 / 2.0 + y3 / 3.0;
   d = y0;

   // Take derivative

   float da, db, dc;

   da = 3 * a;
   db = 2 * b;
   dc = c;

   // Find zeroes of derivative using quadratic equation

   float discriminant = db * db - 4 * da * dc;
   if (discriminant < 0.0)
      return float(-1.0);              // error

   float x1 = (-db + sqrt(discriminant)) / (2 * da);
   float x2 = (-db - sqrt(discriminant)) / (2 * da);

   // The one which corresponds to a local _maximum_ in the
   // cubic is the one we want - the one with a negative
   // second derivative

   float dda = 2 * da;
   float ddb = db;

   if (dda * x1 + ddb < 0)
   {
      *max = a*x1*x1*x1+b*x1*x1+c*x1+d;
      return x1;
   }
   else
   {
      *max = a*x2*x2*x2+b*x2*x2+c*x2+d;
      return x2;
   }
}
