/**********************************************************************

  Audacity: A Digital Audio Editor

  KeyConfigPrefs.cpp

  Brian Gunlogson
  Dominic Mazzoni
  James Crook

*******************************************************************//*!

\class KeyConfigPrefs
\brief A PrefsPanel for keybindings.

The code for displaying keybindings is similar to code in MousePrefs.
It would be nice to create a NEW 'Bindings' class which both
KeyConfigPrefs and MousePrefs use.

*//*********************************************************************/

#include "../Audacity.h"

#include "KeyConfigPrefs.h"

#include <wx/setup.h> // for wxUSE_* macros
#include <wx/defs.h>
#include <wx/ffile.h>
#include <wx/intl.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/button.h>
#include <wx/radiobut.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/textctrl.h>

#include "../Prefs.h"
#include "../Project.h"
#include "../commands/CommandManager.h"
#include "../xml/XMLFileReader.h"

#include "../ShuttleGui.h"

#include "../FileNames.h"

#include "../widgets/KeyView.h"
#include "../widgets/AudacityMessageBox.h"

#if wxUSE_ACCESSIBILITY
#include "../widgets/WindowAccessible.h"
#endif

//
// KeyConfigPrefs
//
#define AssignDefaultsButtonID  17001
#define CurrentComboID          17002
#define SetButtonID             17003
#define ClearButtonID           17004
#define CommandsListID          17005
#define ExportButtonID          17006
#define ImportButtonID          17007
#define FilterID                17008
#define ViewByTreeID            17009
#define ViewByNameID            17010
#define ViewByKeyID             17011
#define FilterTimerID           17012

BEGIN_EVENT_TABLE(KeyConfigPrefs, PrefsPanel)
   EVT_BUTTON(AssignDefaultsButtonID, KeyConfigPrefs::OnDefaults)
   EVT_BUTTON(SetButtonID, KeyConfigPrefs::OnSet)
   EVT_BUTTON(ClearButtonID, KeyConfigPrefs::OnClear)
   EVT_BUTTON(ExportButtonID, KeyConfigPrefs::OnExport)
   EVT_BUTTON(ImportButtonID, KeyConfigPrefs::OnImport)
   EVT_LISTBOX(CommandsListID, KeyConfigPrefs::OnSelected)
   EVT_RADIOBUTTON(ViewByTreeID, KeyConfigPrefs::OnViewBy)
   EVT_RADIOBUTTON(ViewByNameID, KeyConfigPrefs::OnViewBy)
   EVT_RADIOBUTTON(ViewByKeyID, KeyConfigPrefs::OnViewBy)
   EVT_TIMER(FilterTimerID, KeyConfigPrefs::OnFilterTimer)
END_EVENT_TABLE()

KeyConfigPrefs::KeyConfigPrefs(wxWindow * parent, wxWindowID winid,
                               const CommandID &name)
/* i18n-hint: as in computer keyboard (not musical!) */
:  PrefsPanel(parent, winid, XO("Keyboard")),
   mView(NULL),
   mKey(NULL),
   mFilter(NULL),
   mFilterTimer(this, FilterTimerID),
   mFilterPending(false)
{
   Populate();
   if (!name.empty()) {
      auto index = mView->GetIndexByName(name);
      mView->SelectNode(index);
   }
}

ComponentInterfaceSymbol KeyConfigPrefs::GetSymbol()
{
   return KEY_CONFIG_PREFS_PLUGIN_SYMBOL;
}

TranslatableString KeyConfigPrefs::GetDescription()
{
   return XO("Preferences for KeyConfig");
}

wxString KeyConfigPrefs::HelpPageName()
{
   return "Keyboard_Preferences";
}

void KeyConfigPrefs::Populate()
{
   ShuttleGui S(this, eIsCreatingFromPrefs);
   AudacityProject *project = GetActiveProject();

   if (!project) {
      S.StartVerticalLay(true);
      {
         S.StartStatic( {}, true);
         {
            S.AddTitle(_("Keyboard preferences currently unavailable."));
            S.AddTitle(_("Open a new project to modify keyboard shortcuts."));
         }
         S.EndStatic();
      }
      S.EndVerticalLay();

      return;
   }

   PopulateOrExchange(S);

   mCommandSelected = wxNOT_FOUND;

   mManager = &CommandManager::Get( *project );

   // For speed, don't sort here.  We're just creating.
   // Instead sort when we do SetView later in this function.
   RefreshBindings(false);

   if (mViewByTree->GetValue()) {
      mViewType = ViewByTree;
   }
   else if (mViewByName->GetValue()) {
      mViewType = ViewByName;
   }
   else if (mViewByKey->GetValue()) {
      mViewType = ViewByKey;
      mFilterLabel->SetLabel(_("&Hotkey:"));
      mFilter->SetName(wxStripMenuCodes(mFilterLabel->GetLabel()));
   }

   mView->SetView(mViewType);
}

/// Normally in classes derived from PrefsPanel this function
/// is used both to populate the panel and to exchange data with it.
/// With KeyConfigPrefs all the exchanges are handled specially,
/// so this is only used in populating the panel.
void KeyConfigPrefs::PopulateOrExchange(ShuttleGui & S)
{
   S.SetBorder(2);

   S.StartStatic(_("Key Bindings"), 1);
   {
      S.StartMultiColumn(3, wxEXPAND);
      {
         S.SetStretchyCol(1);

         S.StartHorizontalLay(wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL, 0);
         {
            S.AddTitle(_("View by:"));
            S.StartRadioButtonGroup({
               wxT("/Prefs/KeyConfig/ViewBy"),
               {
                  { wxT("tree"), XO("&Tree") },
                  { wxT("name"), XO("&Name") },
                  { wxT("key"), XO("&Key") },
               },
               0 // tree
            });
            {
               mViewByTree = S.Id(ViewByTreeID)
                  .Name(XO("View by tree"))
                  .TieRadioButton();
               mViewByName = S.Id(ViewByNameID)
                  .Name(XO("View by name"))
                  .TieRadioButton();
               mViewByKey = S.Id(ViewByKeyID)
                  .Name(XO("View by key"))
                  .TieRadioButton();
#if wxUSE_ACCESSIBILITY
               // so that name can be set on a standard control
               if (mViewByTree) mViewByTree->SetAccessible(safenew WindowAccessible(mViewByTree));
               if (mViewByName) mViewByName->SetAccessible(safenew WindowAccessible(mViewByName));
               if (mViewByKey) mViewByKey->SetAccessible(safenew WindowAccessible(mViewByKey));
#endif
            }
            S.EndRadioButtonGroup();
         }
         S.EndHorizontalLay();

         S.StartHorizontalLay(wxALIGN_CENTER|wxALIGN_CENTER_VERTICAL, 0);
         {
            // just a spacer
         }
         S.EndHorizontalLay();

         S.StartHorizontalLay(wxALIGN_RIGHT|wxALIGN_CENTER_VERTICAL, 0);
         {
            mFilterLabel = S.AddVariableText(_("Searc&h:"));

            if (!mFilter) {
               mFilter = safenew wxTextCtrl(S.GetParent(),
                                        FilterID,
                                        wxT(""),
                                        wxDefaultPosition,
#if defined(__WXMAC__)
                                        wxSize(300, -1),
#else
                                        wxSize(210, -1),
#endif
                                        wxTE_PROCESS_ENTER);
               mFilter->SetName(wxStripMenuCodes(mFilterLabel->GetLabel()));
            }
            S.Position(wxALIGN_NOT | wxALIGN_LEFT)
               .ConnectRoot(wxEVT_KEY_DOWN,
                            &KeyConfigPrefs::OnFilterKeyDown)
               .ConnectRoot(wxEVT_CHAR,
                            &KeyConfigPrefs::OnFilterChar)
               .AddWindow(mFilter);
         }
         S.EndHorizontalLay();
      }
      S.EndThreeColumn();
      S.AddSpace(-1, 2);

      S.StartHorizontalLay(wxEXPAND, 1);
      {
         if (!mView) {
            mView = safenew KeyView(S.GetParent(), CommandsListID);
            mView->SetName(_("Bindings"));
         }
         S.Prop(true)
            .Position(wxEXPAND)
            .AddWindow(mView);
      }
      S.EndHorizontalLay();

      S.StartThreeColumn();
      {
         if (!mKey) {
            mKey = safenew wxTextCtrl(S.GetParent(),
                                  CurrentComboID,
                                  wxT(""),
                                  wxDefaultPosition,
#if defined(__WXMAC__)
                                  wxSize(300, -1),
#else
                                  wxSize(210, -1),
#endif
                                  wxTE_PROCESS_ENTER);
#if wxUSE_ACCESSIBILITY
            // so that name can be set on a standard control
            mKey->SetAccessible(safenew WindowAccessible(mKey));
#endif
            mKey->SetName(_("Short cut"));
         }
         S
            .ConnectRoot(wxEVT_KEY_DOWN,
                      &KeyConfigPrefs::OnHotkeyKeyDown)
            .ConnectRoot(wxEVT_CHAR,
                      &KeyConfigPrefs::OnHotkeyChar)
            .ConnectRoot(wxEVT_KILL_FOCUS,
                      &KeyConfigPrefs::OnHotkeyKillFocus)
            .AddWindow(mKey);

         /* i18n-hint: (verb)*/
         mSet = S.Id(SetButtonID).AddButton(_("&Set"));
         mClear = S.Id(ClearButtonID).AddButton(_("Cl&ear"));
      }
      S.EndThreeColumn();

#if defined(__WXMAC__)
      S.AddFixedText(_("Note: Pressing Cmd+Q will quit. All other keys are valid."));
#endif

      S.StartThreeColumn();
      {
         S.Id(ImportButtonID).AddButton(_("&Import..."));
         S.Id(ExportButtonID).AddButton(_("&Export..."));
         S.Id(AssignDefaultsButtonID).AddButton(_("&Defaults"));
      }
      S.EndThreeColumn();
   }
   S.EndStatic();


   // Need to layout so that the KeyView is properly sized before populating.
   // Otherwise, the initial selection is not scrolled into view.
   Layout();
}

void KeyConfigPrefs::RefreshBindings(bool bSort)
{
   TranslatableStrings Labels;
   wxArrayString Categories;
   TranslatableStrings Prefixes;

   mNames.clear();
   mKeys.clear();
   mDefaultKeys.clear();
   mStandardDefaultKeys.clear();
   mManager->GetAllCommandData(
      mNames,
      mKeys,
      mDefaultKeys,
      Labels,
      Categories,
      Prefixes,
      true); // True to include effects (list items), false otherwise.

   mStandardDefaultKeys = mDefaultKeys;
   FilterKeys( mStandardDefaultKeys );

   mView->RefreshBindings(mNames,
                          Categories,
                          Prefixes,
                          Labels,
                          mKeys,
                          bSort);
   //Not needed as NEW nodes are already shown expanded.
   //mView->ExpandAll();

   mNewKeys = mKeys;
}

void KeyConfigPrefs::OnImport(wxCommandEvent & WXUNUSED(event))
{
   wxString file = wxT("Audacity-keys.xml");

   file = FileNames::SelectFile(FileNames::Operation::Open,
                        XO("Select an XML file containing Audacity keyboard shortcuts..."),
                       wxEmptyString,
                       file,
                       wxT(""),
                       _("XML files (*.xml)|*.xml|All files|*"),
                       wxRESIZE_BORDER,
                       this);

   if (!file) {
      return;
   }

   XMLFileReader reader;
   if (!reader.Parse(mManager, file)) {
      AudacityMessageBox(
         reader.GetErrorStr(),
         XO("Error Importing Keyboard Shortcuts"),
         wxOK | wxCENTRE,
         this);
   }

   RefreshBindings(true);
}

void KeyConfigPrefs::OnExport(wxCommandEvent & WXUNUSED(event))
{
   wxString file = wxT("Audacity-keys.xml");

   file = FileNames::SelectFile(FileNames::Operation::Export,
                       XO("Export Keyboard Shortcuts As:"),
                       wxEmptyString,
                       file,
                       wxT("xml"),
                       _("XML files (*.xml)|*.xml|All files|*"),
                       wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxRESIZE_BORDER,
                       this);

   if (!file) {
      return;
   }

   GuardedCall( [&] {
      XMLFileWriter prefFile{ file, XO("Error Exporting Keyboard Shortcuts") };
      mManager->WriteXML(prefFile);
      prefFile.Commit();
   } );
}



// There currently is only one clickable AButton
// so we just do what it needs.
void KeyConfigPrefs::OnDefaults(wxCommandEvent & WXUNUSED(event))
{
   wxMenu Menu;
   Menu.Append( 1, _("Standard") );
   Menu.Append( 2, _("Full") );
   Menu.Bind( wxEVT_COMMAND_MENU_SELECTED, &KeyConfigPrefs::OnImportDefaults, this );
   // Pop it up where the mouse is.
   PopupMenu(&Menu);//, wxPoint(0, 0));
}

void KeyConfigPrefs::FilterKeys( std::vector<NormalizedKeyString> & arr )
{
   const auto &MaxListOnly = CommandManager::ExcludedList();

   // Remove items that are in MaxList.
   for (size_t i = 0; i < arr.size(); i++) {
      if( std::binary_search(MaxListOnly.begin(), MaxListOnly.end(), arr[i]) )
         arr[i] = {};
   }
}

void KeyConfigPrefs::OnImportDefaults(wxCommandEvent & event)
{
   gPrefs->DeleteEntry(wxT("/GUI/Shortcuts/FullDefaults"));
   gPrefs->Flush();

   mNewKeys = mDefaultKeys;
   if( event.GetId() == 1 )
      FilterKeys( mNewKeys );

   for (size_t i = 0; i < mNewKeys.size(); i++) {
      mManager->SetKeyFromIndex(i, mNewKeys[i]);
   }

   RefreshBindings(true);
}

void KeyConfigPrefs::OnHotkeyKeyDown(wxKeyEvent & e)
{
   wxTextCtrl *t = (wxTextCtrl *)e.GetEventObject();

   // Make sure we can navigate away from the hotkey textctrl.
   // On Linux and OSX, it can get stuck, but it doesn't hurt
   // to do it for Windows as well.
   //
   // Mac note:  Don't waste time trying to figure out why the
   // focus goes back to the prefs tree.  Unless Voiceover is
   // active, buttons on the Mac do not accept focus and all the
   // controls between this one and the tree control are buttons.
   if (e.GetKeyCode() == WXK_TAB) {
      t->Navigate(e.ShiftDown()
                 ? wxNavigationKeyEvent::IsBackward
                 : wxNavigationKeyEvent::IsForward);
      return;
   }

   t->SetValue(KeyEventToKeyString(e).Display());
}

void KeyConfigPrefs::OnHotkeyChar(wxEvent & WXUNUSED(e))
{
   // event.Skip() not performed, so event will not be processed further.
}

void KeyConfigPrefs::OnHotkeyKillFocus(wxEvent & e)
{
   if (mKey->GetValue().empty() && mCommandSelected != wxNOT_FOUND) {
      mKey->AppendText(mView->GetKey(mCommandSelected).Display());
   }

   e.Skip();
}

void KeyConfigPrefs::OnFilterTimer(wxTimerEvent & WXUNUSED(e))
{
   // The filter timer has expired, so set the filter
   if (mFilterPending)
   {
      // Do not reset mFilterPending here...possible race
      mView->SetFilter(mFilter->GetValue());
   }
}

void KeyConfigPrefs::OnFilterKeyDown(wxKeyEvent & e)
{
   wxTextCtrl *t = (wxTextCtrl *)e.GetEventObject();
   int keycode = e.GetKeyCode();

   // Make sure we can navigate away from the hotkey textctrl.
   // On Linux and OSX, it an get stuck, but it doesn't hurt
   // to do it for Windows as well.
   if (keycode == WXK_TAB) {
      wxNavigationKeyEvent nevent;
      nevent.SetWindowChange(e.ControlDown());
      nevent.SetDirection(!e.ShiftDown());
      nevent.SetEventObject(t);
      nevent.SetCurrentFocus(t);
      t->GetParent()->GetEventHandler()->ProcessEvent(nevent);

      return;
   }

   if (mViewType == ViewByKey) {
      wxString key = KeyEventToKeyString(e).Display();
      t->SetValue(key);

      if (!key.empty()) {
         mView->SetFilter(t->GetValue());
      }
   }
   else
   {
      if (keycode == WXK_RETURN) {
         mFilterPending = false;
         mView->SetFilter(t->GetValue());
      }
      else {
         mFilterPending = true;
         mFilterTimer.Start(500, wxTIMER_ONE_SHOT);

         e.Skip();
      }
   }
}

void KeyConfigPrefs::OnFilterChar(wxEvent & e)
{
   if (mViewType != ViewByKey)
   {
      e.Skip();
   }
}

// Given a hotkey combination, returns the name (description) of the
// corresponding command, or the empty string if none is found.
CommandID KeyConfigPrefs::NameFromKey(const NormalizedKeyString & key)
{
   return mView->GetNameByKey(key);
}

// Sets the selected command to have this key
// This is not yet a committed change, which will happen on a save.
void KeyConfigPrefs::SetKeyForSelected(const NormalizedKeyString & key)
{
   auto name = mView->GetName(mCommandSelected);

   if (!mView->CanSetKey(mCommandSelected))
   {
      AudacityMessageBox(
         XO("You may not assign a key to this entry"),
         XO("Error"),
         wxICON_ERROR | wxCENTRE,
         this);
      return;
   }

   mView->SetKey(mCommandSelected, key);
   mManager->SetKeyFromName(name, key);
   mNewKeys[ make_iterator_range( mNames ).index( name ) ] = key;
}


void KeyConfigPrefs::OnSet(wxCommandEvent & WXUNUSED(event))
{
   if (mCommandSelected == wxNOT_FOUND) {
      AudacityMessageBox(
         XO("You must select a binding before assigning a shortcut"),
         XO("Error"),
         wxICON_WARNING | wxCENTRE,
         this);
      return;
   }

   NormalizedKeyString key { mKey->GetValue() };
   auto oldname = mView->GetNameByKey(key);
   auto newname = mView->GetName(mCommandSelected);

   // Just ignore it if they are the same
   if (oldname == newname) {
      return;
   }

   // Prevent same hotkey combination being used twice.
   if (!oldname.empty()) {
      auto oldlabel = Verbatim( wxT("%s - %s") )
         .Format(
            mManager->GetCategoryFromName(oldname),
            mManager->GetPrefixedLabelFromName(oldname) );
      auto newlabel = Verbatim( wxT("%s - %s") )
         .Format(
            mManager->GetCategoryFromName(newname),
            mManager->GetPrefixedLabelFromName(newname) );
      if (wxCANCEL == AudacityMessageBox(
            XO(
"The keyboard shortcut '%s' is already assigned to:\n\n\t'%s'\n\nClick OK to assign the shortcut to\n\n\t'%s'\n\ninstead. Otherwise, click Cancel.")
               .Format(
                  mKey->GetValue(),
                  oldlabel,
                  newlabel
               ),
            XO("Error"),
            wxOK | wxCANCEL | wxICON_STOP | wxCENTRE,
            this))
      {
         return;
      }

      mView->SetKeyByName(oldname, {});
      mManager->SetKeyFromName(oldname, {});
      mNewKeys[ make_iterator_range( mNames ).index( oldname ) ] = {};

   }

   SetKeyForSelected(key);
}

void KeyConfigPrefs::OnClear(wxCommandEvent& WXUNUSED(event))
{
   mKey->Clear();

   if (mCommandSelected != wxNOT_FOUND) {
      SetKeyForSelected({});
   }
}

void KeyConfigPrefs::OnSelected(wxCommandEvent & WXUNUSED(e))
{
   mCommandSelected = mView->GetSelected();
   mKey->Clear();

   if (mCommandSelected != wxNOT_FOUND) {
      bool canset = mView->CanSetKey(mCommandSelected);
      if (canset) {
         mKey->AppendText(mView->GetKey(mCommandSelected).Display());
      }

      mKey->Enable(canset);
      mSet->Enable(canset);
      mClear->Enable(canset);
   }
}

void KeyConfigPrefs::OnViewBy(wxCommandEvent & e)
{
   switch (e.GetId())
   {
      case ViewByTreeID:
         mViewType = ViewByTree;
         mFilterLabel->SetLabel(_("Searc&h:"));
      break;

      case ViewByNameID:
         mViewType = ViewByName;
         mFilterLabel->SetLabel(_("Searc&h:"));
      break;

      case ViewByKeyID:
         mViewType = ViewByKey;
         mFilterLabel->SetLabel(_("&Hotkey:"));
      break;
   }

   mView->SetView(mViewType);
   mFilter->SetName(wxStripMenuCodes(mFilterLabel->GetLabel()));
}

bool KeyConfigPrefs::Commit()
{
   // On the Mac, preferences may be changed without any active
   // projects.  This means that the CommandManager isn't availabe
   // either.  So we can't attempt to save preferences, otherwise
   // NULL ptr dereferences will happen in ShuttleGui because the
   // radio buttons are never created.  (See Populate() above.)
   if (!GetActiveProject()) {
      return true;
   }

   ShuttleGui S(this, eIsSavingToPrefs);
   PopulateOrExchange(S);

   bool bFull = gPrefs->ReadBool(wxT("/GUI/Shortcuts/FullDefaults"), false);
   for (size_t i = 0; i < mNames.size(); i++) {
      const auto &dkey = bFull ? mDefaultKeys[i] : mStandardDefaultKeys[i];
      // using GET to interpret CommandID as a config path component
      auto name = wxT("/NewKeys/") + mNames[i].GET();
      const auto &key = mNewKeys[i];

      if (gPrefs->HasEntry(name)) {
         if (key != NormalizedKeyString{ gPrefs->ReadObject(name, key) } ) {
            gPrefs->Write(name, key);
         }
         if (key == dkey) {
            gPrefs->DeleteEntry(name);
         }
      }
      else {
         if (key != dkey) {
            gPrefs->Write(name, key);
         }
      }
   }

   return gPrefs->Flush();
}

void KeyConfigPrefs::Cancel()
{
   // Restore original key values
   for (size_t i = 0; i < mNames.size(); i++) {
      mManager->SetKeyFromIndex(i, mKeys[i]);
   }

   return;
}

PrefsPanel::Factory
KeyConfigPrefsFactory( const CommandID &name )
{
   return [=](wxWindow *parent, wxWindowID winid)
   {
      wxASSERT(parent); // to justify safenew
      auto result = safenew KeyConfigPrefs{ parent, winid, name };
      return result;
   };
}
