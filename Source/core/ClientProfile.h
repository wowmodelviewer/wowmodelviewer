/*
 * ClientProfile.h
 *
 * Describes WHICH WoW client the viewer is currently looking at: the product family it was installed as
 * (Retail, PTR, Classic, Classic Era...), its version/build, the expansion that version is (where the version
 * says so), its storage backend (CASC vs MPQ) and how files are addressed in that storage (by FileDataID vs by
 * name).
 *
 * WHAT DECIDES WHAT. A Battle.net product code (.build.info) names a channel, not an expansion: "wow_classic"
 * has been Wrath, Cataclysm and Mists of Pandaria in turn. So:
 *   - the storage is how the client was opened: every Battle.net product is CASC (Classic included -- they are
 *     the modern engine), and only the legacy loader (WoWFolder::initMpq) is MPQ;
 *   - the product family comes from the product code (one signal: its channel);
 *   - the expansion comes from the version, and only for version lines that identify one (Retail majors; the
 *     Classic re-release lines 1.13-1.15, 2.5, 3.4, 4.4, 5.5) -- otherwise none is claimed;
 *   - what the client's game data can do is not decided here at all: the database schema is verified table by
 *     table against the client's own files (WoWDatabase::refreshStructures), and the viewer's capabilities follow
 *     from what loaded (ModelViewer::clientCapabilities).
 */

#ifndef _CLIENTPROFILE_H_
#define _CLIENTPROFILE_H_

#include <QString>

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _CLIENTPROFILE_API_ __declspec(dllexport)
#    else
#        define _CLIENTPROFILE_API_ __declspec(dllimport)
#    endif
#else
#    define _CLIENTPROFILE_API_
#endif

namespace core
{
  // Forward declared to avoid a circular include with GameFolder.h (which owns GameConfig
  // and includes this header). The definition is only needed in ClientProfile.cpp.
  class GameConfig;

  // Expansion "era", where the version identifies one (see expansionFor); Unknown otherwise.
  enum class ClientEra
  {
    Unknown = 0,
    Vanilla,       // 1.x
    TBC,           // 2.x
    WotLK,         // 3.x
    Cataclysm,     // 4.x
    MoP,           // 5.x
    WoD,           // 6.x  (CASC introduced here)
    Legion,        // 7.x
    BfA,           // 8.x
    Shadowlands,   // 9.x
    Dragonflight,  // 10.x
    WarWithin,     // 11.x
    Midnight,      // 12.x
    Modern         // a Retail major newer than we explicitly name
  };

  // The channel a client was installed as, from its Battle.net product code.
  enum class ProductFamily
  {
    Unknown = 0,   // a product code this viewer does not name (shown as the code itself)
    Retail,        // wow
    RetailPTR,     // wowt, wowxptr
    RetailBeta,    // wow_beta
    RetailAlpha,   // wow_alpha
    Classic,       // wow_classic
    ClassicPTR,    // wow_classic_ptr
    ClassicBeta,   // wow_classic_beta
    ClassicEra,    // wow_classic_era
    ClassicEraPTR, // wow_classic_era_ptr
    LegacyMpq      // a pre-Battle.net install opened from its MPQ archives
  };

  // The on-disk archive backend the client ships its data in.
  enum class StorageType
  {
    Unknown = 0,
    CASC,   // Blizzard content-addressable storage (every Battle.net client); addressed by FileDataID
    MPQ     // classic MoPaQ archives (pre-Battle.net installs); addressed by file name/path
  };

  // How files are looked up in the active storage.
  enum class FileLookupMode
  {
    Unknown = 0,
    FileDataID, // numeric id is the primary key (CASC / modern)
    Name,       // string path is the primary key (MPQ / old clients)
    Both        // both are usable
  };

  class _CLIENTPROFILE_API_ ClientProfile
  {
    public:
      ClientProfile() = default;

      // Parsed identity
      ClientEra era = ClientEra::Unknown;
      ProductFamily family = ProductFamily::Unknown;
      QString versionString;          // e.g. "12.0.7.68235"
      QString product;                // e.g. "wow", "wowt", "wow_classic"
      int major = 0;
      int minor = 0;
      int patch = 0;
      int build = 0;                  // trailing build number, e.g. 68235

      // Storage / addressing
      StorageType storage = StorageType::Unknown;
      FileLookupMode lookupMode = FileLookupMode::Unknown;
      bool hasFileDataId = false;     // files are referenced by FileDataID (every CASC client)

      // Build a profile from a detected Battle.net config (locale/version/product): a CASC client. The legacy MPQ
      // loader builds its own (WoWFolder::initMpq).
      static ClientProfile fromGameConfig(const GameConfig & config);
      // A Retail major version to its era (the Retail expansions; also the legacy MPQ client's version).
      static ClientEra eraFromMajorVersion(int major);
      // The family a Battle.net product code belongs to.
      static ProductFamily familyFromProduct(const QString & product);
      // The expansion a version of a family is, where the version identifies one; Unknown otherwise.
      static ClientEra expansionFor(ProductFamily family, int major, int minor);
      static QString familyName(ProductFamily family);
      static QString expansionName(ClientEra era);   // "Midnight", "Mists of Pandaria", "" for Unknown

      bool isClassicFamily() const;
      bool isRetailFamily() const;
      // The plain-language name: "Retail", "Classic Era", "PTR"... (the product code for an unknown product).
      QString friendlyName() const;
      // "Mists of Pandaria . 5.5.4" / "Version 1.60.1": the expansion where known, and the version without the build.
      QString versionLabel() const;

      // Human-readable field names for logging.
      QString eraName() const;
      QString storageName() const;
      QString lookupModeName() const;

      // Compact single-line summary for logs.
      QString describe() const;

      bool isValid() const { return major > 0 && storage != StorageType::Unknown; }
  };
}

#endif /* _CLIENTPROFILE_H_ */
