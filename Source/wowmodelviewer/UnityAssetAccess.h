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
    int index = 0;         // slot in the model's texture array / variation index
    int fileDataID = 0;    // resolved texture FileDataID (fetch it with readByFileDataID)
    bool fromDatabase = true; // true: CreatureDisplayInfo (authoritative). false: the model is
                              // not referenced by any creature display any more (legacy asset)
                              // and this is the conventional sibling skin found in the listfile.
  };

  // Resolve the texture(s) of a model that the M2 itself does not name, using the same
  // CreatureDisplayInfo -> CreatureModelData relation the viewer uses for its own skin list.
  // Returns the FIRST display's variations (the viewer's own default skin). False with a
  // reason when there is no client, no database or no matching creature display.
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
