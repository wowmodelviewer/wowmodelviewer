/*
 * ViewportBackground.cpp
 *
 * See ViewportBackground.h.
 */

#include "ViewportBackground.h"

#include <QSettings>
#include <QString>
#include <QStringList>

#include "util.h"

namespace
{
  const char * const ColourKey = "ModelViewport/BackgroundColor";
  const char * const PresetsKey = "ModelViewport/BackgroundPresets";

  QString configPath() { return QString::fromWCharArray(cfgPath.c_str()); }
}

namespace ViewportBackground
{
  wxColour defaultColour()
  {
    // WmvMain.cs ViewportClear (0.10, 0.10, 0.12) as it displays: measured (25,25,30) on the player's own captures.
    return wxColour(0x19, 0x19, 0x1E);
  }

  const std::vector<Preset> & builtIns()
  {
    // Every one at or below #BB in each channel: brighter, a background starts to feed the viewport's bloom (see
    // WmvMain.ApplyViewportBackground), so Light is the lightest grey that stays clear of it.
    static const std::vector<Preset> presets = {
      { _("Default"), defaultColour() },
      { _("Dark"), wxColour(0x00, 0x00, 0x00) },
      { _("Slate"), wxColour(0x20, 0x24, 0x28) },
      { _("Neutral Grey"), wxColour(0x80, 0x80, 0x80) },
      { _("Light"), wxColour(0xBB, 0xBB, 0xBB) },
    };
    return presets;
  }

  const Preset * builtInFor(const wxColour & colour)
  {
    for (const Preset & p : builtIns())
      if (sameColour(p.colour, colour))
        return &p;
    return nullptr;
  }

  bool parseHex(const wxString & text, wxColour & colour)
  {
    wxString t = text;
    t.Trim(true).Trim(false);
    if (t.StartsWith(wxT("#")))
      t.Remove(0, 1);
    if (t.length() != 6)
      return false;
    unsigned long value = 0;
    for (wxUniChar c : t)
    {
      const wxChar ch = c;
      int digit;
      if (ch >= wxT('0') && ch <= wxT('9'))
        digit = ch - wxT('0');
      else if (ch >= wxT('a') && ch <= wxT('f'))
        digit = ch - wxT('a') + 10;
      else if (ch >= wxT('A') && ch <= wxT('F'))
        digit = ch - wxT('A') + 10;
      else
        return false;
      value = value * 16 + digit;
    }
    colour = wxColour((unsigned char)(value >> 16), (unsigned char)(value >> 8), (unsigned char)value);
    return true;
  }

  wxString formatHex(const wxColour & colour)
  {
    return wxString::Format(wxT("#%02X%02X%02X"), colour.Red(), colour.Green(), colour.Blue());
  }

  bool sameColour(const wxColour & a, const wxColour & b)
  {
    return a.IsOk() && b.IsOk() && a.Red() == b.Red() && a.Green() == b.Green() && a.Blue() == b.Blue();
  }

  wxColour loadColour()
  {
    QSettings config(configPath(), QSettings::IniFormat);
    wxColour colour = defaultColour();
    const QString stored = config.value(ColourKey).toString();
    if (!stored.isEmpty() && !parseHex(wxString::FromUTF8(stored.toUtf8().constData()), colour))
      colour = defaultColour();
    return colour;
  }

  void saveColour(const wxColour & colour)
  {
    QSettings config(configPath(), QSettings::IniFormat);
    config.setValue(ColourKey, QString::fromUtf8(formatHex(colour).utf8_str()));
    config.sync();
  }

  std::vector<wxColour> loadCustomPresets()
  {
    QSettings config(configPath(), QSettings::IniFormat);
    // QSettings keeps a list as comma-separated values; one entry comes back as a plain string.
    const QStringList stored = config.value(PresetsKey).toStringList();
    std::vector<wxColour> presets;
    for (const QString & s : stored)
    {
      wxColour c;
      if (!parseHex(wxString::FromUTF8(s.toUtf8().constData()), c) || builtInFor(c))
        continue;
      bool seen = false;
      for (const wxColour & p : presets)
        seen = seen || sameColour(p, c);
      if (!seen && presets.size() < MaxCustomPresets)
        presets.push_back(c);
    }
    return presets;
  }

  void saveCustomPresets(const std::vector<wxColour> & presets)
  {
    QSettings config(configPath(), QSettings::IniFormat);
    QStringList list;
    for (const wxColour & c : presets)
      list << QString::fromUtf8(formatHex(c).utf8_str());
    if (list.isEmpty())
      config.remove(PresetsKey);
    else
      config.setValue(PresetsKey, list);
    config.sync();
  }
}
