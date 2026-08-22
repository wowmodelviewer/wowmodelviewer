/*
 * UnityAssetAccess.h
 *
 * Narrow read-only asset access for the embedded Unity renderer's IPC: "give me the raw
 * bytes of this WoW file from the ACTIVE client" -- by internal path or by FileDataID.
 *
 * It goes through the same GAMEDIRECTORY file providers the rest of the app uses (CASC for
 * modern clients, the MoPaQ chain for legacy MPQ clients), so the Unity viewport renders
 * from exactly the data the legacy OpenGL viewport renders from. Nothing is written to disk
 * and nothing is converted: this is runtime access, not an export workflow.
 *
 * Must be called on the GUI thread (GAMEDIRECTORY is not thread-safe); the IPC server polls
 * on the GUI thread, so that holds by construction.
 */

#ifndef UNITYASSETACCESS_H
#define UNITYASSETACCESS_H

#include <QByteArray>
#include <QString>
#include <vector>

class UnityAssetAccess
{
public:
  struct Result
  {
    bool ok = false;
    QString error;        // human-readable reason when !ok ("not found", "no client loaded", ...)
    QString path;         // normalized path (when known)
    int fileDataID = 0;   // FileDataID (when known / applicable)
    QString provider;     // "CASC" / "MPQ" / "Unknown" -- the active client's storage backend
    QByteArray data;      // raw file bytes when ok
  };

  // Normalize a WoW internal path the way the file providers expect it: lower-case,
  // forward slashes, no leading "./" or "/", trimmed.
  static QString normalizePath(const QString & path);

  // Read raw bytes by internal path, e.g. "creature/chicken/chicken.m2".
  static Result readByPath(const QString & path);

  // Read raw bytes by FileDataID. Only CASC-addressed clients support this; legacy MPQ
  // clients (name lookup only) get a clear "not supported" error.
  static Result readByFileDataID(int fileDataID);

  // One texture the renderer needs for a model. Modern M2s leave "replaceable" textures
  // (creature skins, TextureType != 0) out of the file itself: the M2's TXID entry is 0 and
  // the texture array carries no filename, because the actual skin comes from the client
  // database. Only WMV can answer that -- it owns the DB -- so the renderer asks for it.
  struct ModelTexture
  {
    // Where the answer came from. Reported to the renderer so a naming guess is never mistaken
    // for database truth, and so a stale default is never mistaken for what is on screen.
    enum Source
    {
      Selection,   // the skin the viewport is showing right now (the app's own skin selector)
      Database,    // CreatureDisplayInfo -- authoritative, but only about the DEFAULT skin
      Convention,  // a listfile naming guess, for models no creature display references
    };

    int index = 0;       // position among the display's texture variations
    int type = 0;        // WoW texture TYPE this feeds: TEXTURE_GAMEOBJECT1 (11) is the first
                         // creature skin, 12 and 13 the second and third. The renderer maps it
                         // onto the M2 texture slot(s) declaring that type -- which is the only
                         // correct mapping, since variation order and slot order need not agree.
    int fileDataID = 0;  // resolved texture FileDataID (fetch it with readByFileDataID)
    Source source = Database;
  };

  // "database" / "selection" / "convention" -- the wire spelling of ModelTexture::Source.
  static const char * sourceName(ModelTexture::Source source);

  // The geosets the displayed skin switches on, as the numbers the model's own submeshes are
  // identified by. A creature display can differ from another by GEOMETRY rather than texture,
  // and the renderer cannot work that out for itself: only the app knows which display is
  // selected. An empty list with a true return means "this display switches none on", which is
  // not the same as not knowing -- see WoWModel::setCreatureGeosetData, where a submesh is drawn
  // when its id is 0 or appears in this set. False when there is no selection to report.
  static bool selectedModelGeosets(int m2FileDataID, std::vector<int> & out);

  // Resolve the texture(s) of a model that the M2 itself does not name.
  //
  // Tries, in order:
  //   1. the skin the viewport is CURRENTLY showing, from the app's own skin selector. A model
  //      usually has several skins (chicken2 has seven) and the database can only say which is
  //      the default, so this is the only source that answers "what is on screen".
  //   2. CreatureDisplayInfo -> CreatureModelData, the same relation the viewer's skin list is
  //      built from -- the model's default skin, used before a selection exists.
  //   3. the conventional sibling skin from the listfile, for legacy models no creature display
  //      references any more.
  // False with a reason when none of them can answer.
  static bool resolveModelTextures(int m2FileDataID, std::vector<ModelTexture> & out, QString & error);

  // Name of the active client's storage backend ("CASC"/"MPQ"/"Unknown") -- for logging.
  static QString activeProviderName();

  // True when a game client is loaded (and not currently being re-loaded) so file access is
  // possible.
  static bool hasActiveClient();

  // Scope guard placed at the top of the client-(re)load routines: while one is alive, asset
  // requests are refused with "game client is still loading" instead of touching a folder
  // that is being rebuilt (the load yields to the event loop, so IPC polls can land mid-load).
  struct ClientLoadGuard
  {
    ClientLoadGuard();
    ~ClientLoadGuard();
  };
};

#endif // UNITYASSETACCESS_H
