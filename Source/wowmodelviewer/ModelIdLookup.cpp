/*
 * ModelIdLookup.cpp
 *
 * See ModelIdLookup.h.
 */

#include "ModelIdLookup.h"

#include <climits>

#include <QFileInfo>

#include "Game.h"
#include "GameDatabase.h"
#include "GameFile.h"
#include "RaceInfos.h"
#include "WoWFolder.h"

#include "logger/Logger.h"

namespace
{
  QString listfilePath(int fileDataId)
  {
    return static_cast<wow::WoWFolder &>(GAMEDIRECTORY).fileName(fileDataId);
  }

  // Whether fileDataId is an M2 the loaded client can give: found, named .m2 by the listfile (or, unnamed, starting
  // with an M2's magic), and readable. why: what follows "<subject>" (or "..., which") when it is not -- never the
  // listfile path, which can be wider than the dialog (the log has it: ModelIdLookup::Resolved::path).
  bool checkM2(int fileDataId, QString & path, wxString & why)
  {
    path = listfilePath(fileDataId);
    GameFile * file = GAMEDIRECTORY.getFile(fileDataId);
    if (!file)
    {
      why = path.isEmpty() ? _("was not found in the loaded client") : _("is in the file list but not in the loaded client");
      return false;
    }
    if (!path.isEmpty() && !path.endsWith(QStringLiteral(".m2"), Qt::CaseInsensitive))
    {
      const QString suffix = QFileInfo(path).suffix().toLower();
      why = suffix.isEmpty() ? _("is not an M2 model")
                             : wxString::Format(_("is a .%s file, not an M2 model"), wxString(suffix.toStdWString()));
      return false;
    }
    // The data itself: readable (an encrypted file whose key the client lacks, or one not downloaded yet, is not),
    // and an M2 (chunked files start with the MD21 chunk, older ones with the MD20 header), read from the whole file
    // (rawBuffer: open() may have pointed the buffer at one chunk). A file another part of the viewer holds open is
    // read from its buffer rather than opened and closed under it -- when that buffer holds the whole file.
    const bool wasOpen = file->isCurrentlyOpen();
    if (wasOpen && !(file->readComplete() && file->rawBuffer()))
    {
      why = _("is in use in the viewer and could not be checked; try again");
      return false;
    }
    const bool readable = wasOpen || (file->open() && file->readComplete() && file->rawBuffer());
    unsigned char magic[4] = { 0, 0, 0, 0 };
    const bool longEnough = readable && file->rawSize() >= 4;
    if (longEnough)
      for (int i = 0; i < 4; i++)
        magic[i] = file->rawBuffer()[i];
    if (!wasOpen)
      file->close();
    if (!readable)
    {
      why = _("is in the loaded client but could not be read (it may be encrypted, or not downloaded yet)");
      return false;
    }
    const bool m2 = longEnough && magic[0] == 'M' && magic[1] == 'D' && magic[2] == '2' && (magic[3] == '1' || magic[3] == '0');
    if (!m2)
    {
      why = path.isEmpty() ? _("is not an M2 model") : _("is named as an M2 model, but its data is not one");
      return false;
    }
    return true;
  }

  // One row, or why there is none: a query the loaded client's database cannot answer (a table or column it lacks) is
  // told apart from no such row. tables: what the query reads, for the message.
  bool queryRow(const QString & sql, const wxString & tables, std::vector<QString> & row, wxString & why)
  {
    sqlResult r = GAMEDATABASE.sqlQuery(sql);
    if (!r.valid)
    {
      why = wxString::Format(_("The loaded client's %s data could not be read."), tables);
      return false;
    }
    if (r.empty())
      return false;
    row = r.values[0];
    return true;
  }
}

namespace ModelIdLookup
{
  wxString label(Kind kind)
  {
    switch (kind)
    {
      case Kind::FileDataId: return _("M2 FileDataID");
      case Kind::CreatureDisplay: return _("Creature Display ID");
      case Kind::CreatureModel: return _("Creature Model ID");
    }
    return wxString();
  }

  wxString explanation(Kind kind)
  {
    switch (kind)
    {
      case Kind::FileDataId:
        return _("The M2 model file with this FileDataID, on its own, as Browse shows a model.");
      case Kind::CreatureDisplay:
        return _("A CreatureDisplayInfo row: its model with that display's skin, geosets and particle colours, or an NPC's gear.");
      case Kind::CreatureModel:
        return _("A CreatureModelData row: its model file on its own, as Browse shows a model, not any NPC's look.");
    }
    return wxString();
  }

  QString Resolved::describe() const
  {
    QString chain;
    switch (kind)
    {
      case Kind::FileDataId:
        chain = QString("M2 FileDataID %1").arg(id);
        break;
      case Kind::CreatureDisplay:
        chain = QString("Creature Display ID %1 -> CreatureModelData %2 -> FileDataID %3").arg(id).arg(modelDataId)
                  .arg(fileDataId);
        if (extendedDisplayId && loadFileDataId != fileDataId)
          chain += QString(" (ExtendedDisplayInfoID %1, shown on the HD model FileDataID %2)").arg(extendedDisplayId)
                     .arg(loadFileDataId);
        else if (extendedDisplayId)
          chain += QString(" (ExtendedDisplayInfoID %1)").arg(extendedDisplayId);
        break;
      case Kind::CreatureModel:
        chain = QString("Creature Model ID %1 -> FileDataID %2").arg(id).arg(fileDataId);
        break;
    }
    return chain + " " + (path.isEmpty() ? QStringLiteral("(not in the listfile)") : path);
  }

  bool parseId(const wxString & text, int & id, wxString & why)
  {
    // Trimmed of any Unicode space (a no-break space pasted from a web page too).
    const QString t = QString::fromWCharArray(text.wc_str()).trimmed();
    if (t.isEmpty())
    {
      why = _("Enter an ID.");
      return false;
    }
    const bool negative = t.startsWith(QLatin1Char('-'));
    const QString digits = negative ? t.mid(1) : t;
    bool allDigits = !digits.isEmpty();
    for (QChar c : digits)
      allDigits = allDigits && c >= QLatin1Char('0') && c <= QLatin1Char('9');
    if (!allDigits)
    {
      why = _("An ID is written with the digits 0-9 only, for example 123040.");
      return false;
    }
    if (negative)
    {
      why = _("An ID is a number above 0.");
      return false;
    }
    // Leading zeros are no part of the size.
    int first = 0;
    while (first < digits.size() && digits[first] == QLatin1Char('0'))
      first++;
    const QString significant = digits.mid(first);
    if (significant.isEmpty())
    {
      why = _("0 is not an ID.");
      return false;
    }
    bool ok = false;
    const unsigned long long value = significant.length() <= 10 ? significant.toULongLong(&ok) : 0;
    if (!ok || value > (unsigned long long)INT_MAX)
    {
      why = _("That number is too large for an ID.");
      return false;
    }
    id = (int)value;
    return true;
  }

  bool checkModelFile(int fileDataId, wxString & why)
  {
    QString path;
    return checkM2(fileDataId, path, why);
  }

  bool resolve(Kind kind, int id, Resolved & out, wxString & why)
  {
    why.clear();
    out = Resolved();
    out.kind = kind;
    out.id = id;
    wxString fileWhy;
    switch (kind)
    {
      case Kind::FileDataId:
      {
        out.fileDataId = out.loadFileDataId = id;
        if (!checkM2(id, out.path, fileWhy))
        {
          why = wxString::Format(_("FileDataID %d %s."), id, fileWhy);
          return false;
        }
        return true;
      }

      case Kind::CreatureModel:
      {
        std::vector<QString> row;
        if (!queryRow(QString("SELECT FileDataID FROM CreatureModelData WHERE ID = %1").arg(id), wxT("CreatureModelData"), row,
                      why))
        {
          if (why.empty())
            why = wxString::Format(_("Creature Model ID %d was not found in the loaded client."), id);
          return false;
        }
        out.modelDataId = id;
        out.fileDataId = out.loadFileDataId = row[0].toInt();
        if (out.fileDataId <= 0)
        {
          why = wxString::Format(_("Creature Model ID %d has no model file (its FileDataID is 0)."), id);
          return false;
        }
        if (!checkM2(out.fileDataId, out.path, fileWhy))
        {
          why = wxString::Format(_("Creature Model ID %d names FileDataID %d, which %s."), id, out.fileDataId, fileWhy);
          return false;
        }
        return true;
      }

      case Kind::CreatureDisplay:
      {
        std::vector<QString> row;
        if (!queryRow(QString("SELECT CreatureDisplayInfo.ModelID, CreatureModelData.ID, CreatureModelData.FileDataID, "
                              "CreatureDisplayInfo.ExtendedDisplayInfoID FROM CreatureDisplayInfo "
                              "LEFT JOIN CreatureModelData ON CreatureDisplayInfo.ModelID = CreatureModelData.ID "
                              "WHERE CreatureDisplayInfo.ID = %1").arg(id),
                      _("CreatureDisplayInfo and CreatureModelData"), row, why))
        {
          if (why.empty())
            why = wxString::Format(_("Creature Display ID %d was not found in the loaded client."), id);
          return false;
        }
        out.displayId = id;
        const int modelId = row[0].toInt();
        out.extendedDisplayId = row[3].toInt();
        if (modelId <= 0)
        {
          why = wxString::Format(_("Creature Display ID %d has no model (its ModelID is 0)."), id);
          return false;
        }
        if (row[1].isEmpty())
        {
          why = wxString::Format(_("Creature Display ID %d uses Creature Model ID %d, which is not in the loaded client."),
                                 id, modelId);
          return false;
        }
        out.modelDataId = modelId;
        out.fileDataId = row[2].toInt();
        if (out.fileDataId <= 0)
        {
          why = wxString::Format(_("Creature Display ID %d uses Creature Model ID %d, which has no model file "
                                   "(its FileDataID is 0)."), id, modelId);
          return false;
        }
        // A display with extended display info is a humanoid NPC: shown on its race's HD character model (or the
        // model its stored choices belong to), as View NPC shows it (ModelViewer::ShowCreatureDisplay).
        out.loadFileDataId = out.extendedDisplayId ? RaceInfos::getCreatureDisplayFileID(out.fileDataId, out.extendedDisplayId)
                                                   : out.fileDataId;
        if (!checkM2(out.loadFileDataId, out.path, fileWhy))
        {
          why = out.loadFileDataId != out.fileDataId
                  ? wxString::Format(_("Creature Display ID %d is shown on the HD character model FileDataID %d, which %s."),
                                     id, out.loadFileDataId, fileWhy)
                  : wxString::Format(_("Creature Display ID %d uses FileDataID %d, which %s."), id, out.loadFileDataId, fileWhy);
          return false;
        }
        return true;
      }
    }
    why = _("Unknown ID type.");
    return false;
  }
}
