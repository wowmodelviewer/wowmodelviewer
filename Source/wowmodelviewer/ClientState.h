/*
 * ClientState.h
 *
 * The loaded game client, as the viewer's windows ask about it: is one loaded (and not being replaced right now),
 * which one it is (a number that changes whenever it is replaced), and the raw bytes of one of its files -- by
 * internal path or by FileDataID -- through the same GAMEDIRECTORY file providers the rest of the app uses (CASC for
 * modern clients, the MoPaQ chain for legacy MPQ clients).
 *
 * Must be called on the GUI thread (GAMEDIRECTORY is not thread-safe).
 */

#ifndef CLIENTSTATE_H
#define CLIENTSTATE_H

#include <QByteArray>
#include <QString>

class ClientState
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
    // The file came from the client's own storage (not a custom-folder override): its bytes are the same for as long
    // as this client is loaded.
    bool fromClientStorage = false;
  };

  // A number that changes whenever the loaded client is replaced (ModelViewer::ResetClientState): the same FileDataID
  // is another file in another client, so caches keyed by it are kept under this number.
  static int clientEpoch();
  static void noteClientReplaced();

  // Normalize a WoW internal path the way the file providers expect it: lower-case,
  // forward slashes, no leading "./" or "/", trimmed.
  static QString normalizePath(const QString & path);

  // Read raw bytes by internal path, e.g. "creature/chicken/chicken.m2".
  static Result readByPath(const QString & path);

  // Read raw bytes by FileDataID. Only CASC-addressed clients support this; legacy MPQ
  // clients (name lookup only) get a clear "not supported" error.
  static Result readByFileDataID(int fileDataID);

  // Name of the active client's storage backend ("CASC"/"MPQ"/"Unknown") -- for logging.
  static QString activeProviderName();

  // True when a game client is loaded (and not currently being re-loaded) so file access is
  // possible.
  static bool hasActiveClient();

  // Scope guard placed at the top of the client-(re)load routines: while one is alive, file
  // requests are refused with "game client is still loading" instead of touching a folder
  // that is being rebuilt (the load yields to the event loop).
  struct ClientLoadGuard
  {
    ClientLoadGuard();
    ~ClientLoadGuard();
  };

  // True while a ClientLoadGuard is alive: a client load is in progress.
  static bool isClientLoading();
};

#endif // CLIENTSTATE_H
