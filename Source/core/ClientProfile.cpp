/*
 * ClientProfile.cpp
 */

#include "ClientProfile.h"

#include <QStringList>

#include "GameFolder.h" // core::GameConfig

namespace core
{
  ClientEra ClientProfile::eraFromMajorVersion(int major)
  {
    switch (major)
    {
      case 1:  return ClientEra::Vanilla;
      case 2:  return ClientEra::TBC;
      case 3:  return ClientEra::WotLK;
      case 4:  return ClientEra::Cataclysm;
      case 5:  return ClientEra::MoP;
      case 6:  return ClientEra::WoD;
      case 7:  return ClientEra::Legion;
      case 8:  return ClientEra::BfA;
      case 9:  return ClientEra::Shadowlands;
      case 10: return ClientEra::Dragonflight;
      case 11: return ClientEra::WarWithin;
      case 12: return ClientEra::Midnight;
      default:
        return (major > 12) ? ClientEra::Modern : ClientEra::Unknown;
    }
  }

  ClientProfile ClientProfile::fromGameConfig(const GameConfig & config)
  {
    ClientProfile p;
    p.versionString = config.version;
    p.product = config.product;

    // Version strings look like "12.0.7.68235" (major.minor.patch.build). Be tolerant of
    // shorter/empty strings -- unparsed fields stay 0.
    const QStringList parts = config.version.split(QLatin1Char('.'), QString::SkipEmptyParts);
    if (parts.size() > 0) p.major = parts.at(0).toInt();
    if (parts.size() > 1) p.minor = parts.at(1).toInt();
    if (parts.size() > 2) p.patch = parts.at(2).toInt();
    if (parts.size() > 3) p.build = parts.at(parts.size() - 1).toInt();

    p.era = eraFromMajorVersion(p.major);

    // CASC was introduced in Warlords of Draenor (6.0). Everything before ships as MPQ.
    // Only CASC clients actually reach this code today; the MPQ branch is here so a truthful
    // profile can be built once an old client can be opened (later milestone).
    if (p.major >= 6)
      p.storage = StorageType::CASC;
    else if (p.major >= 1)
      p.storage = StorageType::MPQ;
    else
      p.storage = StorageType::Unknown;

    p.lookupMode = (p.storage == StorageType::CASC) ? FileLookupMode::FileDataID
                 : (p.storage == StorageType::MPQ)  ? FileLookupMode::Name
                                                    : FileLookupMode::Unknown;

    // Coarse, version-keyed capability heuristics (informational for now).
    p.hasFileDataId        = (p.storage == StorageType::CASC);
    p.chunkedM2            = (p.major >= 7); // MD21 chunked container, Legion+
    p.externalAnimFiles    = (p.major >= 7);
    p.hasComponentFileData = (p.major >= 4); // ModelFileData/ComponentModelFileData indirection
    p.hasChrCustomization  = (p.major >= 9); // modern ChrCustomization* customization system

    return p;
  }

  QString ClientProfile::eraName() const
  {
    switch (era)
    {
      case ClientEra::Vanilla:      return "Vanilla (1.x)";
      case ClientEra::TBC:          return "The Burning Crusade (2.x)";
      case ClientEra::WotLK:        return "Wrath of the Lich King (3.x)";
      case ClientEra::Cataclysm:    return "Cataclysm (4.x)";
      case ClientEra::MoP:          return "Mists of Pandaria (5.x)";
      case ClientEra::WoD:          return "Warlords of Draenor (6.x)";
      case ClientEra::Legion:       return "Legion (7.x)";
      case ClientEra::BfA:          return "Battle for Azeroth (8.x)";
      case ClientEra::Shadowlands:  return "Shadowlands (9.x)";
      case ClientEra::Dragonflight: return "Dragonflight (10.x)";
      case ClientEra::WarWithin:    return "The War Within (11.x)";
      case ClientEra::Midnight:     return "Midnight (12.x)";
      case ClientEra::Modern:       return "Modern";
      default:                      return "Unknown";
    }
  }

  QString ClientProfile::storageName() const
  {
    switch (storage)
    {
      case StorageType::CASC: return "CASC";
      case StorageType::MPQ:  return "MPQ";
      default:                return "Unknown";
    }
  }

  QString ClientProfile::lookupModeName() const
  {
    switch (lookupMode)
    {
      case FileLookupMode::FileDataID: return "FileDataID";
      case FileLookupMode::Name:       return "Name";
      case FileLookupMode::Both:       return "FileDataID+Name";
      default:                         return "Unknown";
    }
  }

  QString ClientProfile::describe() const
  {
    return QString("era=%1 version=%2 build=%3 product=%4 storage=%5 lookup=%6")
        .arg(eraName())
        .arg(versionString.isEmpty() ? QString("?") : versionString)
        .arg(build)
        .arg(product.isEmpty() ? QString("?") : product)
        .arg(storageName())
        .arg(lookupModeName());
  }
}
