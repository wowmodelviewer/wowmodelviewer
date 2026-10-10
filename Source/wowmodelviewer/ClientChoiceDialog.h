/*
 * ClientChoiceDialog.h
 *
 * "Choose World of Warcraft": the question File > Load World of Warcraft asks. One card per installed client, in
 * plain words -- "Classic Era", "Vanilla · 1.15.9", installed or not, the one used last -- and a click (or Enter)
 * on a card opens that client. Nothing technical is asked: no product codes, no builds, no schema to pick (the
 * schema follows from the client itself: ClientInstallations::resolveSchema). The technical facts stay one click
 * away, under Advanced, for the card in focus. Secondary: another installation folder, and a legacy (MPQ) install.
 *
 * It only chooses; ModelViewer::LoadWoW opens. Not a launcher: no news, art, accounts or patch notes.
 */
#ifndef CLIENTCHOICEDIALOG_H
#define CLIENTCHOICEDIALOG_H

#include <functional>
#include <wx/dialog.h>

#include <vector>

#include <QString>

#include "ClientInstallations.h"

class wxBoxSizer;
class wxFlexGridSizer;
class wxPanel;
class wxStaticText;
class UiButton;
class ClientChoiceDialog;
class ClientLoadingPanel;
class ClientLoadProgress;

// A card: one installation, the whole card the button. Painted with the palette of the theme in use.
class InstallCard : public wxWindow
{
public:
  InstallCard(wxWindow * parent, ClientChoiceDialog * owner, const InstalledClient & client, bool lastUsed,
              const QString & folderNote);

  const InstalledClient & client() const { return m_client; }
  bool openable() const { return m_client.data != InstalledClient::Data::NotDownloaded; }
  bool lastUsed() const { return m_lastUsed; }
  void setOpening(bool opening);
  // For the tests: the pointer over it, held down.
  void setHot(bool hot);

  bool AcceptsFocus() const override { return true; }

protected:
  wxSize DoGetBestSize() const override;

private:
  void OnPaint(wxPaintEvent &);
  void OnMouse(wxMouseEvent &);
  void OnKey(wxKeyEvent &);
  void OnFocus(wxFocusEvent &);
  wxString title() const;
  wxString detail() const;
  wxString status() const;
  wxString reason() const;

  ClientChoiceDialog * m_owner;
  InstalledClient m_client;
  bool m_lastUsed;
  QString m_folderNote;
  bool m_hot = false;
  bool m_pressed = false;
  bool m_opening = false;
};

class ClientChoiceDialog : public wxDialog
{
public:
  explicit ClientChoiceDialog(wxWindow * parent);

  // After ShowModal() returns wxID_OK:
  wxString dataPath() const { return m_dataPath; }   // "<root>\Data\" for gamePath
  core::GameConfig selectedConfig() const { return m_chosen.config(); }
  const InstalledClient & selectedClient() const { return m_chosen; }
  // The user chose "Open legacy installation..." instead of a card: the caller asks for its folder and loads it
  // (ModelViewer::PromptAndLoadLegacyMpqClient).
  bool isLegacyMpq() const { return m_legacyMpq; }

  // THE LOAD A CARD STARTS, run by the chooser itself: the chooser turns into its loading page (ClientLoadingPanel)
  // and calls it, reporting to that page. True: the client is open, and the chooser closes. False: the page says
  // why, and Back returns to the cards (the client loaded before, if any, is still the one in use). Without a
  // loader, a card closes the chooser and the caller loads.
  using Loader = std::function<bool(const InstalledClient &, ClientLoadProgress *)>;
  void setLoader(Loader loader) { m_loader = std::move(loader); }
  // For the tests: the loading page, and whether a load is running in it.
  ClientLoadingPanel * loadingPage() const { return m_loadingPage; }
  bool loading() const { return m_busy; }

  // Used by the cards.
  void openCard(InstallCard * card);
  void focusNeighbour(InstallCard * from, int step);
  void showDetails(InstallCard * card);

  // For the tests: the cards, the Advanced disclosure.
  const std::vector<InstallCard *> & cards() const { return m_cards; }
  void setAdvancedShown(bool shown);
  bool advancedShown() const { return m_advancedShown; }

private:
  void buildUI();
  void runLoad();
  void showLoadingPage(bool shown);
  // The window fitted to the page it shows, about the same centre (it shrinks or grows in place, on screen).
  void fitInPlace();
  void backToCards();
  void populate(const QStringList & roots);
  void onBrowse(wxCommandEvent &);
  void onLegacy(wxCommandEvent &);
  void onAdvanced(wxCommandEvent &);
  void relayout();

  wxPanel * m_cardsPanel = nullptr;
  wxBoxSizer * m_cardsSizer = nullptr;
  std::vector<InstallCard *> m_cards;
  wxStaticText * m_message = nullptr;        // nothing found / a folder without an installation
  UiButton * m_advancedToggle = nullptr;
  wxPanel * m_advancedPanel = nullptr;
  wxFlexGridSizer * m_advancedGrid = nullptr;
  bool m_advancedShown = false;
  InstallCard * m_detailsFor = nullptr;

  InstalledClient m_chosen;
  wxString m_dataPath;
  bool m_legacyMpq = false;

  wxPanel * m_choosePage = nullptr;               // the cards and the ways in
  ClientLoadingPanel * m_loadingPage = nullptr;   // what the chooser turns into while a card's client loads
  Loader m_loader;
  bool m_busy = false;                            // a load runs: the chooser cannot be closed
};

#endif /* CLIENTCHOICEDIALOG_H */
