/*
 * ClientInstallations.h
 *
 * The World of Warcraft installations on this computer, as the client chooser offers them: every active product of
 * every Battle.net install folder (.build.info), each described in plain words (ClientProfile::friendlyName,
 * versionLabel) and checked, cheaply, for whether its game data is on this computer -- its build configuration, and
 * its ENCODING manifest in the local archive index (Battle.net can list a product whose data it has not downloaded:
 * WoW Classic 5.5.4.70032 here, 2026-10-05). Nothing here opens a storage: that is ModelViewer::LoadWoW's.
 *
 * Also the one rule that picks a client's database schema (resolveSchema), shared by LoadWoW and the chooser's
 * Advanced details, so what the chooser says is what the load does.
 */
#ifndef CLIENTINSTALLATIONS_H
#define CLIENTINSTALLATIONS_H

#include <vector>

#include <QString>
#include <QStringList>

#include "ClientProfile.h"
#include "GameFolder.h" // core::GameConfig

struct InstalledClient
{
  enum class Data
  {
    Available,     // the build's configuration and its ENCODING manifest are on this computer
    NotDownloaded, // listed by Battle.net, but its game data is not on this computer
    Unknown        // could not be told (an unreadable index, say); opening it decides
  };

  QString root;            // the install folder holding .build.info, e.g. "E:\World of Warcraft"
  QString product;         // "wow", "wow_classic_era"...
  QString version;         // "1.15.9.70003"
  QString region;          // the .build.info branch: "eu", "us"...
  QString locale;          // the text locale: "enUS"
  QString buildKey;
  core::ClientProfile profile;
  Data data = Data::Unknown;
  QString dataDetail;      // why, for the Advanced details

  core::GameConfig config() const;
  QString dataPath() const; // "<root>\Data\" (gamePath's form)
};

namespace ClientInstallations
{
  // The active products of the install folder at root (or of the install root above a product folder such as
  // "_retail_", or a "Data" folder), in the chooser's order: Retail, PTR, Beta, Classic..., then unknown products.
  // error: why there is none ("" when found).
  std::vector<InstalledClient> discover(const QString & root, QString * error = nullptr);
  // The install folders worth looking in: the one in use (gamePath), the last one loaded, and the ones the
  // Battle.net registry names, each once and only when it has a .build.info.
  QStringList candidateRoots();
  // The folder holding .build.info for any path inside an install ("Data", "_retail_"...), or "" when none.
  QString rootOf(const QString & anyPath);

  // THE SCHEMA a client's database is read with: games/wow/<major.minor> when one ships, otherwise the newest shipped
  // schema as the base, every table of which is then checked against the client's own layout
  // (WoWDatabase::refreshStructures). how: the same in words. Empty when no schema ships.
  QString resolveSchema(const core::ClientProfile & profile, QString * how = nullptr);
}

#endif // CLIENTINSTALLATIONS_H
