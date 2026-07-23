/*
 * ClientChoiceDialog.cpp
 */
#include "ClientChoiceDialog.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/dir.h>
#include <wx/msgdlg.h>

#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QRegularExpression>

#include "enums.h"
#include "util.h"             // gamePath
#include "logger/Logger.h"

BEGIN_EVENT_TABLE(ClientChoiceDialog, wxDialog)
  EVT_BUTTON(ID_CC_BROWSE,  ClientChoiceDialog::onBrowse)
  EVT_BUTTON(ID_CC_LOAD,    ClientChoiceDialog::onLoad)
  EVT_BUTTON(ID_CC_LEGACY,  ClientChoiceDialog::onLegacyMpq)
  EVT_CHOICE(ID_CC_PRODUCT, ClientChoiceDialog::onProductChanged)
END_EVENT_TABLE()

ClientChoiceDialog::ClientChoiceDialog(wxWindow * parent)
  : wxDialog(parent, wxID_ANY, wxT("Client Choice"), wxDefaultPosition, wxDefaultSize),
    m_folder(0), m_detected(0), m_product(0), m_profile(0), m_load(0), m_legacyMpq(false)
{
  const wxString initialRoot = rootOf(gamePath);
  buildUI(initialRoot);
  populateProfiles();

  if (!initialRoot.IsEmpty() && wxDirExists(initialRoot))
    detect(initialRoot);
  else
    m_detected->SetLabel(wxT("(choose your World of Warcraft folder)"));

  GetSizer()->Fit(this);
  Centre();
}

void ClientChoiceDialog::buildUI(const wxString & initialRoot)
{
  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);

  wxFlexGridSizer * grid = new wxFlexGridSizer(2, 8, 10);
  grid->AddGrowableCol(1, 1);

  // Folder: [path] [Browse]
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Folder:")), 0, wxALIGN_CENTER_VERTICAL);
  {
    wxBoxSizer * row = new wxBoxSizer(wxHORIZONTAL);
    m_folder = new wxTextCtrl(this, wxID_ANY, initialRoot, wxDefaultPosition, wxSize(330, -1), wxTE_READONLY);
    row->Add(m_folder, 1, wxALIGN_CENTER_VERTICAL);
    row->Add(new wxButton(this, ID_CC_BROWSE, wxT("Browse"), wxDefaultPosition, wxSize(72, -1)), 0, wxLEFT, 6);
    grid->Add(row, 1, wxEXPAND);
  }

  // Detected: [....]
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Detected:")), 0, wxALIGN_CENTER_VERTICAL);
  m_detected = new wxStaticText(this, wxID_ANY, wxEmptyString);
  grid->Add(m_detected, 0, wxALIGN_CENTER_VERTICAL);

  // Product: [dropdown]
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Product:")), 0, wxALIGN_CENTER_VERTICAL);
  m_product = new wxChoice(this, ID_CC_PRODUCT);
  grid->Add(m_product, 1, wxEXPAND);

  // Profile: [dropdown]
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Profile:")), 0, wxALIGN_CENTER_VERTICAL);
  m_profile = new wxChoice(this, ID_CC_PROFILE);
  grid->Add(m_profile, 1, wxEXPAND);

  top->Add(grid, 0, wxEXPAND | wxALL, 12);

  // Modern (CASC) Load, and a separate button for a legacy MoPaQ client.
  wxBoxSizer * btnRow = new wxBoxSizer(wxHORIZONTAL);
  m_load = new wxButton(this, ID_CC_LOAD, wxT("Load"));
  btnRow->Add(m_load, 0, wxRIGHT, 12);
  btnRow->Add(new wxButton(this, ID_CC_LEGACY, wxT("Legacy MPQ client...")), 0);
  top->Add(btnRow, 0, wxALIGN_CENTER | wxBOTTOM, 6);

  top->Add(new wxStaticText(this, wxID_ANY,
             wxT("Legacy MPQ client: Vanilla / The Burning Crusade / Wrath -- pick the WoW folder or its Data folder.")),
           0, wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  SetSizer(top);
}

// Parse the install's .build.info (a pipe-delimited text manifest) and fill the Detected
// label + Product dropdown. Mirrors CASCFolder::initBuildInfo, but standalone so we can
// re-detect freely when the user changes folders, without touching the Game singleton.
void ClientChoiceDialog::detect(const wxString & rootFolder)
{
  m_configs.clear();
  if (m_product)
    m_product->Clear();

  const wxString buildInfo = rootFolder + wxT("\\.build.info");
  QFile file(QString::fromWCharArray(buildInfo.c_str()));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    m_detected->SetLabel(wxT("No .build.info in that folder (is it the WoW root?)"));
    m_load->Enable(false);
    GetSizer()->Fit(this);
    return;
  }

  QTextStream in(&file);
  QString line = in.readLine();
  const QStringList headers = line.split('|');
  int activeIndex = 0, versionIndex = 0, tagIndex = 0, productIndex = 0;
  for (int i = 0; i < headers.size(); i++)
  {
    if (headers[i].contains("Active", Qt::CaseInsensitive))        activeIndex = i;
    else if (headers[i].contains("Version", Qt::CaseInsensitive))  versionIndex = i;
    else if (headers[i].contains("Tags", Qt::CaseInsensitive))     tagIndex = i;
    else if (headers[i].contains("Product", Qt::CaseInsensitive))  productIndex = i;
  }

  QRegularExpression re("^(\\d+).(\\d+).(\\d+).(\\d+)$");
  while (in.readLineInto(&line))
  {
    QStringList values = line.split('|');
    if (activeIndex >= values.size() || values[activeIndex] == "0")
      continue; // inactive install

    core::GameConfig cfg;
    if (versionIndex < values.size())
    {
      QRegularExpressionMatch m = re.match(values[versionIndex]);
      if (m.hasMatch())
        cfg.version = m.captured(1) + "." + m.captured(2) + "." + m.captured(3) + "." + m.captured(4);
    }
    if (productIndex < values.size())
      cfg.product = values[productIndex];
    if (tagIndex < values.size())
    {
      const QStringList tags = values[tagIndex].split(':');
      for (int i = 0; i < tags.size(); i++)
        if (tags[i].contains("text?"))
        {
          const QStringList t = tags[i].split(" ");
          if (t.size() >= 2)
            cfg.locale = t[t.size() - 2];
        }
    }
    m_configs.push_back(cfg);
  }

  if (m_configs.empty())
  {
    m_detected->SetLabel(wxT("No playable client detected in that folder"));
    m_load->Enable(false);
    GetSizer()->Fit(this);
    return;
  }

  // Detected summary, e.g. "[wow 12.0.7.68235 enUS] [wow_beta 12.0.1.66220 enUS]".
  QString summary;
  for (size_t i = 0; i < m_configs.size(); i++)
    summary += "[" + m_configs[i].product + " " + m_configs[i].version + " " + m_configs[i].locale + "] ";
  m_detected->SetLabel(wxString(summary.trimmed().toStdWString().c_str()));

  // Product dropdown -- one entry per detected (active) config.
  int sel = 0;
  for (size_t i = 0; i < m_configs.size(); i++)
  {
    m_product->Append(wxString(m_configs[i].product.toStdWString().c_str()));
    if (m_configs[i].product == "wow")
      sel = (int)i; // default to the retail client when present
  }
  m_product->SetSelection(sel);
  m_load->Enable(true);

  selectProfileForVersion(m_configs[sel].version);
  GetSizer()->Fit(this);
}

void ClientChoiceDialog::populateProfiles()
{
  m_profile->Clear();
  m_profileDirs.clear();

  wxDir dir(wxT("games\\wow"));
  if (dir.IsOpened())
  {
    wxString name;
    bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_DIRS);
    while (more)
    {
      const QString d = QString::fromWCharArray(name.c_str());
      QString friendly;
      if (d == "9.2")             friendly = "Shadowlands";
      else if (d.startsWith("10")) friendly = "Dragonflight";
      else if (d.startsWith("11")) friendly = "The War Within";
      else if (d.startsWith("12")) friendly = "Midnight";

      m_profileDirs.push_back(d);
      const QString label = friendly.isEmpty() ? d : (friendly + " - " + d);
      m_profile->Append(wxString(label.toStdWString().c_str()));
      more = dir.GetNext(&name);
    }
  }

  if (m_profile->GetCount() == 0)
  {
    // No schema dirs found (unexpected working dir): fall back to deriving the profile
    // from the client version inside LoadWoW (an empty profile string means "auto").
    m_profile->Append(wxT("(auto-detect)"));
    m_profileDirs.push_back(QString());
  }
  m_profile->SetSelection(0);
}

void ClientChoiceDialog::selectProfileForVersion(const QString & version)
{
  const QStringList v = version.split('.');
  if (v.size() < 2)
    return;
  const QString want = v[0] + "." + v[1];
  for (size_t i = 0; i < m_profileDirs.size(); i++)
    if (m_profileDirs[i] == want)
    {
      m_profile->SetSelection((int)i);
      return;
    }
}

void ClientChoiceDialog::onBrowse(wxCommandEvent &)
{
  wxString start = m_folder->GetValue();
  if (start.IsEmpty() || !wxDirExists(start))
    start = wxT("C:\\");

  const wxString picked = wxDirSelector(wxT("Select your World of Warcraft folder"), start,
                                        wxDD_DEFAULT_STYLE, wxDefaultPosition, this);
  if (picked.IsEmpty())
    return; // cancelled

  const wxString root = rootOf(picked);
  m_folder->SetValue(root);
  detect(root);
}

void ClientChoiceDialog::onProductChanged(wxCommandEvent &)
{
  const int sel = m_product->GetSelection();
  if (sel >= 0 && sel < (int)m_configs.size())
    selectProfileForVersion(m_configs[sel].version);
}

void ClientChoiceDialog::onLoad(wxCommandEvent &)
{
  if (m_configs.empty() || m_product->GetSelection() < 0)
  {
    wxMessageBox(wxT("Please choose a World of Warcraft folder with a detected client."),
                 wxT("Client Choice"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  m_dataPath = dataPathOf(m_folder->GetValue());
  EndModal(wxID_OK);
}

// Legacy (pre-CASC) client: pick a MoPaQ install and hand the folder back to the caller, which
// loads it through ModelViewer::LoadWoWFromMpq (auto-detects Data + locale). No .build.info needed.
// If the folder already chosen above is valid, reuse it; otherwise browse for one.
void ClientChoiceDialog::onLegacyMpq(wxCommandEvent &)
{
  wxString folder = m_folder ? m_folder->GetValue() : wxString();
  if (folder.IsEmpty() || !wxDirExists(folder))
  {
    const wxString start = (!folder.IsEmpty() && wxDirExists(folder)) ? folder : wxT("C:\\");
    folder = wxDirSelector(
      wxT("Select a legacy (pre-CASC) WoW folder -- the WoW install folder or its Data folder"),
      start, wxDD_DEFAULT_STYLE, wxDefaultPosition, this);
    if (folder.IsEmpty())
      return; // cancelled -> stay on the client-choice dialog
  }

  m_legacyMpq = true;
  m_mpqFolder = folder;
  EndModal(wxID_OK);
}

core::GameConfig ClientChoiceDialog::selectedConfig() const
{
  const int sel = m_product->GetSelection();
  if (sel >= 0 && sel < (int)m_configs.size())
    return m_configs[sel];
  return core::GameConfig();
}

QString ClientChoiceDialog::selectedProfile() const
{
  const int sel = m_profile->GetSelection();
  if (sel >= 0 && sel < (int)m_profileDirs.size())
    return m_profileDirs[sel];
  return QString();
}

// "<root>\Data\" -> "<root>"; also tolerates a path that already is the root.
wxString ClientChoiceDialog::rootOf(const wxString & anyPath)
{
  wxString p = anyPath;
  p.Replace(wxT("/"), wxT("\\"));
  while (!p.IsEmpty() && p.Last() == '\\')
    p.RemoveLast();
  if (p.Lower().EndsWith(wxT("\\data")))
    p = p.Left(p.Length() - 5);
  while (!p.IsEmpty() && p.Last() == '\\')
    p.RemoveLast();
  return p;
}

wxString ClientChoiceDialog::dataPathOf(const wxString & root)
{
  wxString p = root;
  p.Replace(wxT("/"), wxT("\\"));
  while (!p.IsEmpty() && p.Last() == '\\')
    p.RemoveLast();
  return p + wxT("\\Data\\");
}
