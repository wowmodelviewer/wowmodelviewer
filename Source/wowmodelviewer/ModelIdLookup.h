/*
 * ModelIdLookup.h
 *
 * LOADING A MODEL BY ID (Character > Load NPC / Model..., its ID page): what a typed number means, decided by the
 * ID type the user picked -- never by trying one namespace after another, since the same number is often valid in
 * several (3876 is a CreatureDisplayInfo row showing a Scourge male and a CreatureModelData row naming the
 * Pyrogryph). Resolved from the loaded client only: its DB2 tables (GAMEDATABASE) and file index (GAMEDIRECTORY),
 * offline.
 *
 *   M2 FileDataID        the file itself, on its own, as Browse shows a model. It has to be in the client and be an
 *                        M2: a listfile path ending in .m2, or, for a file the listfile does not name, data that
 *                        starts with MD21 / MD20.
 *   Creature Display ID  CreatureDisplayInfo.ID -> .ModelID -> CreatureModelData.ID -> .FileDataID. Shown with that
 *                        display's appearance as View NPC shows it (ModelViewer::ShowCreatureDisplay): its texture
 *                        variations, geosets and particle colours, or, for a display with ExtendedDisplayInfoID,
 *                        the race's HD character model with the NPC's equipment (NpcModelItemSlotDisplayInfo).
 *   Creature Model ID    CreatureModelData.ID -> .FileDataID: the model file on its own, as Browse shows a model; a
 *                        model row is not an NPC and chooses no appearance.
 *
 * NPC (Creature) IDs are not offered here: the client's Creature table holds only part of the NPCs, and an NPC can
 * have up to four displays whose probabilities are not loaded. The page's URL mode (a Wowhead NPC link) and
 * View > View NPC cover NPCs.
 */

#ifndef MODELIDLOOKUP_H
#define MODELIDLOOKUP_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <QString>

class GameFile;

namespace ModelIdLookup
{
  enum class Kind
  {
    FileDataId,        // M2 FileDataID
    CreatureDisplay,   // CreatureDisplayInfo.ID
    CreatureModel      // CreatureModelData.ID
  };
  const int KindCount = 3;

  // The ID type's name in the selector, and one line on what loading it shows.
  wxString label(Kind kind);
  wxString explanation(Kind kind);

  // What an ID resolved to in the loaded client.
  struct Resolved
  {
    Kind kind = Kind::FileDataId;
    int id = 0;                  // as typed
    int displayId = 0;           // CreatureDisplayInfo.ID (Creature Display ID)
    int modelDataId = 0;         // CreatureModelData.ID (Creature Display ID, Creature Model ID)
    int extendedDisplayId = 0;   // CreatureDisplayInfo.ExtendedDisplayInfoID (Creature Display ID)
    int fileDataId = 0;          // the M2 the ID names (CreatureModelData.FileDataID, or the FileDataID itself)
    int loadFileDataId = 0;      // the M2 that is loaded: an extended display's HD character model, otherwise fileDataId
    QString path;                // the listfile's path for loadFileDataId; empty when the listfile does not name it
    // One line for the log: the chain from the ID to the file.
    QString describe() const;
  };

  // A whole number above 0 that fits an ID; why not otherwise (empty, not a number, negative, 0, too large).
  bool parseId(const wxString & text, int & id, wxString & why);

  // Resolves id as kind in the loaded client, checking that the file it ends in is there, readable and an M2; why
  // not otherwise, naming the step that failed. Nothing is loaded; a file the listfile does not name is added to the
  // client's file index on the way (GameFolder::getFile by FileDataID), as any load of it would be.
  bool resolve(Kind kind, int id, Resolved & out, wxString & why);

  // Whether fileDataId is an M2 the loaded client can give -- found, readable, an M2 -- and why not otherwise (what
  // follows "<subject>" in a sentence). Nothing is loaded.
  // keptOpen (may be null): a file this check opened and read whole is left open there, its buffer ready for the
  // load that follows (GameFile::open returns at once for an open file, and WoWModel closes it once it has read it);
  // the caller closes it if no load does. Null there when nothing was left open.
  bool checkModelFile(int fileDataId, wxString & why, GameFile ** keptOpen = nullptr);
}

#endif // MODELIDLOOKUP_H
