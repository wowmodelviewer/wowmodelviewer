/*
 * WotlkDbc.h
 *
 * WotLK 3.3.5 DBC lookup for legacy MPQ clients. Retail resolves DB-driven creature/item textures
 * from the sqlite GAMEDATABASE (built from DB2/WDCx); legacy MPQ clients have an empty GAMEDATABASE
 * and ship classic fixed-schema WDBC .dbc files that reference assets by STRING PATH (no FileDataID).
 *
 * This is a SEPARATE, self-contained reader/adapter: it reads a handful of WotLK .dbc files directly
 * (via GAMEDIRECTORY.getFile -> MpqFile, then a small in-place WDBC parse -- it does NOT go through
 * WDB2File, which reads via CascLib and would mishandle a StormLib-backed MpqFile) and exposes
 * normalized lookups that return texture PATHS. The Retail DB2 path is entirely untouched -- callers
 * only consult this when clientProfile().storage == MPQ.
 *
 * Milestone 4: minimum tables only -- CreatureModelData, CreatureDisplayInfo, ItemDisplayInfo.
 * No character customization / equipment DBCs yet.
 */

#ifndef _WOTLKDBC_H_
#define _WOTLKDBC_H_

#include <array>
#include <map>
#include <vector>

#include <QString>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WOTLKDBC_API_ __declspec(dllexport)
#    else
#        define _WOTLKDBC_API_ __declspec(dllimport)
#    endif
#else
#    define _WOTLKDBC_API_
#endif

namespace wow
{
  class _WOTLKDBC_API_ WotlkDbc
  {
    public:
      static WotlkDbc & instance();

      // Discard cached tables/indices (call when a different client is loaded).
      void reset();

      // One creature skin variation: up to 3 texture paths for TEXTURE_GAMEOBJECT1/2/3 (empty when
      // that slot is unused). Paths are normalized lowercase '/'.
      struct CreatureSkin { std::array<QString, 3> tex; };

      // Resolve a creature's skin variations from CreatureModelData -> CreatureDisplayInfo.
      // modelPath e.g. "creature/rabbit/rabbit.m2". Fills `out` (one entry per display) and returns
      // true when at least one texture path was produced.
      bool resolveCreatureSkins(const QString & modelPath, std::vector<CreatureSkin> & out);

      // Resolve an item component's texture path(s) from ItemDisplayInfo, matched by model file name.
      // modelPath e.g. "item/objectcomponents/weapon/sword_1h_short_a_01.m2".
      bool resolveItemTextures(const QString & modelPath, std::vector<QString> & texturePathsOut);

    private:
      WotlkDbc() {}
      WotlkDbc(const WotlkDbc &) = delete;
      void operator=(const WotlkDbc &) = delete;

      // Minimal in-memory WDBC table: owns the raw file bytes + header, with typed field accessors.
      struct DbcTable
      {
        bool ok = false;
        unsigned int recordCount = 0, fieldCount = 0, recordSize = 0, stringSize = 0;
        std::vector<unsigned char> data; // whole .dbc file (header + records + string block)

        const unsigned char * record(unsigned int r) const { return data.data() + 20 + (size_t)r * recordSize; }
        const char * strings() const { return reinterpret_cast<const char *>(data.data() + 20 + (size_t)recordCount * recordSize); }
        unsigned int u(unsigned int r, unsigned int field) const;
        QString      s(unsigned int r, unsigned int field) const; // string-pool field
      };

      bool ensureLoaded();                  // lazily read + index the DBCs (once)
      DbcTable loadDbc(const QString & name);
      static QString normModelKey(const QString & path); // lowercase, '\\'->'/', drop extension
      static QString baseKey(const QString & name);      // basename without dir or extension

      bool m_loaded = false;
      // CreatureModelData: normalized model key ("creature/rabbit/rabbit") -> CreatureModelData.ID
      std::map<QString, int> m_modelKeyToId;
      // CreatureDisplayInfo: CreatureModelData.ID -> texture-variation base names [3]
      std::multimap<int, std::array<QString, 3>> m_creatureDisplays;
      // ItemDisplayInfo: model base name ("sword_1h_short_a_01") -> texture base names
      std::multimap<QString, QString> m_itemTextures;
  };
}

#endif /* _WOTLKDBC_H_ */
