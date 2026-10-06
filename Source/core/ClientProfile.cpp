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

  ProductFamily ClientProfile::familyFromProduct(const QString & product)
  {
    const QString p = product.toLower();
    if (p == "wow") return ProductFamily::Retail;
    if (p == "wowt" || p == "wowxptr") return ProductFamily::RetailPTR;
    if (p == "wow_beta") return ProductFamily::RetailBeta;
    if (p == "wow_alpha") return ProductFamily::RetailAlpha;
    if (p == "wow_classic") return ProductFamily::Classic;
    if (p == "wow_classic_ptr") return ProductFamily::ClassicPTR;
    if (p == "wow_classic_beta") return ProductFamily::ClassicBeta;
    if (p == "wow_classic_era") return ProductFamily::ClassicEra;
    if (p == "wow_classic_era_ptr") return ProductFamily::ClassicEraPTR;
    if (p == "mpq") return ProductFamily::LegacyMpq;
    return ProductFamily::Unknown;
  }

  ClientEra ClientProfile::expansionFor(ProductFamily family, int major, int minor)
  {
    switch (family)
    {
      case ProductFamily::Retail:
      case ProductFamily::RetailPTR:
      case ProductFamily::RetailBeta:
      case ProductFamily::RetailAlpha:
        // Retail's major version is its expansion; nothing before Warlords is a Battle.net Retail client.
        return major >= 6 ? eraFromMajorVersion(major) : ClientEra::Unknown;
      case ProductFamily::Classic:
      case ProductFamily::ClassicPTR:
      case ProductFamily::ClassicBeta:
      case ProductFamily::ClassicEra:
      case ProductFamily::ClassicEraPTR:
        // The Classic re-release lines, by major.minor: Classic Era 1.13-1.15 (tested: 1.15.9.70003), Burning
        // Crusade Classic 2.5, Wrath Classic 3.4, Cataclysm Classic 4.4, Mists of Pandaria Classic 5.5 (5.5.4.70032
        // is listed on this machine). Any other Classic version (e.g. the 1.60.1 Classic Beta) claims no expansion.
        if (major == 1 && minor >= 13 && minor <= 15) return ClientEra::Vanilla;
        if (major == 2 && minor == 5) return ClientEra::TBC;
        if (major == 3 && minor == 4) return ClientEra::WotLK;
        if (major == 4 && minor == 4) return ClientEra::Cataclysm;
        if (major == 5 && minor == 5) return ClientEra::MoP;
        return ClientEra::Unknown;
      case ProductFamily::LegacyMpq:
        return eraFromMajorVersion(major);
      default:
        return ClientEra::Unknown;
    }
  }

  ClientProfile ClientProfile::fromGameConfig(const GameConfig & config)
  {
    ClientProfile p;
    p.versionString = config.version;
    p.product = config.product;
    p.family = familyFromProduct(config.product);

    // Version strings look like "12.0.7.68235" (major.minor.patch.build). Be tolerant of
    // shorter/empty strings -- unparsed fields stay 0.
    const QStringList parts = config.version.split(QLatin1Char('.'), QString::SkipEmptyParts);
    if (parts.size() > 0) p.major = parts.at(0).toInt();
    if (parts.size() > 1) p.minor = parts.at(1).toInt();
    if (parts.size() > 2) p.patch = parts.at(2).toInt();
    if (parts.size() > 3) p.build = parts.at(parts.size() - 1).toInt();

    p.era = expansionFor(p.family, p.major, p.minor);

    // A Battle.net product (a .build.info row) is a CASC client whatever its version: the Classic clients are the
    // modern engine on CASC storage, addressed by FileDataID, like Retail. (Deciding storage from the major version
    // classified them as MPQ, and every file of theirs then went to an empty MPQ provider.)
    p.storage = StorageType::CASC;
    p.lookupMode = FileLookupMode::FileDataID;
    p.hasFileDataId = true;
    return p;
  }

  bool ClientProfile::isClassicFamily() const
  {
    return family == ProductFamily::Classic || family == ProductFamily::ClassicPTR || family == ProductFamily::ClassicBeta ||
           family == ProductFamily::ClassicEra || family == ProductFamily::ClassicEraPTR;
  }

  bool ClientProfile::isRetailFamily() const
  {
    return family == ProductFamily::Retail || family == ProductFamily::RetailPTR || family == ProductFamily::RetailBeta ||
           family == ProductFamily::RetailAlpha;
  }

  QString ClientProfile::familyName(ProductFamily family)
  {
    switch (family)
    {
      case ProductFamily::Retail:        return "Retail";
      case ProductFamily::RetailPTR:     return "PTR";
      case ProductFamily::RetailBeta:    return "Beta";
      case ProductFamily::RetailAlpha:   return "Alpha";
      case ProductFamily::Classic:       return "Classic";
      case ProductFamily::ClassicPTR:    return "Classic PTR";
      case ProductFamily::ClassicBeta:   return "Classic Beta";
      case ProductFamily::ClassicEra:    return "Classic Era";
      case ProductFamily::ClassicEraPTR: return "Classic Era PTR";
      case ProductFamily::LegacyMpq:     return "Legacy";
      default:                           return QString();
    }
  }

  QString ClientProfile::expansionName(ClientEra era)
  {
    switch (era)
    {
      case ClientEra::Vanilla:      return "Vanilla";
      case ClientEra::TBC:          return "The Burning Crusade";
      case ClientEra::WotLK:        return "Wrath of the Lich King";
      case ClientEra::Cataclysm:    return "Cataclysm";
      case ClientEra::MoP:          return "Mists of Pandaria";
      case ClientEra::WoD:          return "Warlords of Draenor";
      case ClientEra::Legion:       return "Legion";
      case ClientEra::BfA:          return "Battle for Azeroth";
      case ClientEra::Shadowlands:  return "Shadowlands";
      case ClientEra::Dragonflight: return "Dragonflight";
      case ClientEra::WarWithin:    return "The War Within";
      case ClientEra::Midnight:     return "Midnight";
      default:                      return QString();
    }
  }

  QString ClientProfile::friendlyName() const
  {
    const QString name = familyName(family);
    return name.isEmpty() ? product : name;
  }

  QString ClientProfile::versionLabel() const
  {
    const QString version = QString("%1.%2.%3").arg(major).arg(minor).arg(patch);
    const QString expansion = expansionName(era);
    return expansion.isEmpty() ? QString("Version %1").arg(version)
                               : expansion + QString(" ") + QChar(0x00B7) + QString(" ") + version;
  }

  QString ClientProfile::eraName() const
  {
    const QString name = expansionName(era);
    return name.isEmpty() ? (era == ClientEra::Modern ? QString("Modern") : QString("Unknown")) : name;
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
    return QString("family=%1 product=%2 version=%3 build=%4 expansion=%5 storage=%6 lookup=%7")
        .arg(friendlyName().isEmpty() ? QString("?") : friendlyName())
        .arg(product.isEmpty() ? QString("?") : product)
        .arg(versionString.isEmpty() ? QString("?") : versionString)
        .arg(build)
        .arg(eraName())
        .arg(storageName())
        .arg(lookupModeName());
  }
}
