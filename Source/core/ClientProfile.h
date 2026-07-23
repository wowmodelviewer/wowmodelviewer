/*
 * ClientProfile.h
 *
 * Describes WHICH WoW client the viewer is currently looking at: its era (expansion),
 * version/build, storage backend (CASC vs MPQ) and how files are addressed in that
 * storage (by FileDataID vs by name). This is the single "which client" descriptor that
 * later version-specific code paths (archive backend, DB format, model/texture format,
 * customization/equipment providers) dispatch on, instead of hardcoding assumptions into
 * the renderer.
 *
 * Milestone 1 scaffolding: the profile is populated for the modern (CASC) client that the
 * viewer already supports; the MPQ/old-era fields exist so the abstraction is complete, but
 * no old client is actually loaded yet (see IFileProvider / the placeholder MPQ provider).
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

  // Expansion "era". Derived from the client's major version. Old eras are recognised so a
  // truthful profile can be built for them, even though only modern eras actually load today.
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
    Modern         // anything newer than we explicitly name
  };

  // The on-disk archive backend the client ships its data in.
  enum class StorageType
  {
    Unknown = 0,
    CASC,   // Blizzard content-addressable storage (WoD 6.0+); addressed by FileDataID
    MPQ     // classic MoPaQ archives (Vanilla..Cataclysm); addressed by file name/path
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
      QString versionString;          // e.g. "12.0.7.68235"
      QString product;                // e.g. "wow", "wowt", "wow_classic"
      int major = 0;
      int minor = 0;
      int patch = 0;
      int build = 0;                  // trailing build number, e.g. 68235

      // Storage / addressing
      StorageType storage = StorageType::Unknown;
      FileLookupMode lookupMode = FileLookupMode::Unknown;

      // Coarse capability flags (informational for now; future code paths will gate on these
      // instead of on the version string). Heuristics keyed on the major version.
      bool hasFileDataId = false;         // files are referenced by FileDataID
      bool chunkedM2 = false;             // M2 uses the chunked MD21 container (Legion+)
      bool externalAnimFiles = false;     // animation keyframes live in external .anim files
      bool hasChrCustomization = false;   // modern ChrCustomization* DB2 customization system
      bool hasComponentFileData = false;  // ModelFileData/ComponentModelFileData indirection

      // Build a profile from a detected GameConfig (locale/version/product).
      static ClientProfile fromGameConfig(const GameConfig & config);
      // Map a client major version to its era.
      static ClientEra eraFromMajorVersion(int major);

      // Human-readable field names for logging.
      QString eraName() const;
      QString storageName() const;
      QString lookupModeName() const;

      // Compact single-line summary for logs.
      QString describe() const;

      bool isValid() const { return era != ClientEra::Unknown && major > 0; }
  };
}

#endif /* _CLIENTPROFILE_H_ */
