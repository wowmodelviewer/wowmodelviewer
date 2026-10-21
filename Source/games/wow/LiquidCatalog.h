/*
 * LiquidCatalog.h
 *
 * THE LIQUID CATALOGUE: the liquid types of the loaded client as its own liquid tables describe them -- each type's name,
 * its kind, its material, its colour and two of its parameters, and the FileDataIDs of its animated frames and of its
 * ripple normal map (its "bump") -- built once as the client loads, from the database (LiquidType, LiquidMaterial,
 * LiquidObject and LiquidTypeXTexture: database.xml and dbd/Liquid*.dbd, each read only from a verified layout). Evidence
 * for every column and rule: docs/world-viewer/09-liquids.md 1.4 and 1.6.
 *
 * The Unity player parses MH2O and MLIQ itself and reads no table: the host sends it this catalogue with a terrain or world
 * load (protocol 19, UnityIpcServer.h LIQUIDS) and serves the frames and bumps it names, and no other file, under
 * getMapAsset's role "liquidTexture". The catalogue opens no file: a type's frames are LiquidTypeXTexture's rows of it
 * where the client has some (Retail, MoP Classic, Classic Beta), else its slot-0 template expanded and each name looked
 * up in the storage's root (Classic Era, whose table has no rows): a name the root does not list is left out and counted,
 * never guessed. Its bump is the first file of slots 1-5 whose name contains "bump": the rows after slot 0's frames, each
 * named by the listfile, else the slot templates' names looked up in the root the same way.
 *
 * One catalogue belongs to one loaded client, like the map catalogue (WorldMapCatalog): the viewer builds it as a client
 * loads and drops it when another client loads; one kept past that finds belongsToActiveClient() false. GUI thread only.
 */

#ifndef _LIQUIDCATALOG_H_
#define _LIQUIDCATALOG_H_

#include <memory>
#include <vector>

#include <QJsonObject>
#include <QString>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _LIQUIDCATALOG_API_ __declspec(dllexport)
#    else
#        define _LIQUIDCATALOG_API_ __declspec(dllimport)
#    endif
#else
#    define _LIQUIDCATALOG_API_
#endif

namespace wow
{
  // One liquid type of the client, as the catalogue sends it (docs/world-viewer/09-liquids.md 1.6).
  struct LiquidTypeEntry
  {
    int id = 0;                   // LiquidType ID (copy rows included: a copy is a type of its own ID)
    QString name;                 // LiquidType.Name
    int kind = -1;                // LiquidType.SoundBank: 0 water, 1 ocean, 2 magma, 3 slime (the kind, proven; the name
                                  // the public definitions')
    int material = 0;             // LiquidType.MaterialID: a LiquidMaterial ID
    int materialFlags = -1;       // that LiquidMaterial row's Flags (0x1 a transparent liquid: inferred); -1 when the
                                  // client has no such row
    int lvf = -1;                 // ... and its LVF, the vertex format of MH2O liquid of the material; -1 likewise
    quint32 minimapColor = 0;     // LiquidType.MinimapStaticCol, 0xAARRGGBB as stored
    double float0 = 0.0;          // LiquidType.Float[0] and Float[1] (what they are for: inferred, not proven)
    double float1 = 0.0;
    QString frameTemplate;        // LiquidType.Texture[0] as stored ("XTextures\river\lake_a.%d.blp"; empty for none)
    int framesWanted = 0;         // the frames slot 0 names: a %d template's FrameCountTexture[0] (its low byte), 1 for a
                                  // plain path, 0 for an empty slot
    std::vector<int> frames;      // those frames as FileDataIDs, in frame order (1..N), the ones not found left out
    int framesUnresolved = 0;     // framesWanted less the frames found
    bool framesByName = false;    // found through the storage root's names (the client has no rows of the type)
    int bump = 0;                 // the ripple normal map: the first texture of slots 1-5 whose file name contains "bump"
                                  // (any case), as a FileDataID; 0 for none (the names PROVEN_BYTES, the rule INFERRED)
  };

  class _LIQUIDCATALOG_API_ LiquidCatalog
  {
  public:
    enum class Status
    {
      NoClient,     // no client is loaded
      Unavailable,  // the client's liquid tables could not be read (statusDetail says why)
      Ready,
    };

    // How one of the four tables was read for the client (the database's schema check), and the rows it holds here (copy
    // rows included; -1 when it could not be counted).
    struct Table
    {
      QString name;
      QString layout;       // the client's layout hash of it (8 hex digits); empty when the client has no such file
      QString state;        // "verified", or why it was not read
      bool read = false;    // read from a verified layout
      qint64 rows = -1;
    };

    // The catalogue of the client loaded now. Never fails: one that could not be built says why (status()).
    static std::shared_ptr<LiquidCatalog> build();
    // A catalogue of no client, with the reason (before any client is loaded, while one loads).
    static std::shared_ptr<LiquidCatalog> noClient(const QString & why);

    Status status() const { return m_status; }
    QString statusDetail() const { return m_statusDetail; }
    QString clientProduct() const { return m_product; }
    QString clientBuild() const { return m_build; }
    // Is the client it was built from still the loaded one (same storage, product, build and installation)?
    bool belongsToActiveClient() const;

    const std::vector<LiquidTypeEntry> & types() const { return m_types; }
    const LiquidTypeEntry * find(int id) const;
    // Every frame of every type, sorted, each once. Never null.
    std::shared_ptr<const std::vector<int>> frameIds() const { return m_frameIds; }
    bool isFrame(int fileDataID) const;
    // Every frame and every bump of every type, sorted, each once: the files getMapAsset's role "liquidTexture" may serve
    // under a load sent with this catalogue. Never null.
    std::shared_ptr<const std::vector<int>> textureIds() const { return m_textureIds; }
    bool isTexture(int fileDataID) const;

    const std::vector<Table> & tables() const { return m_tables; }
    const Table * table(const QString & name) const;
    // The frames: every type's found, and wanted but not found; the types whose frames came from LiquidTypeXTexture's rows
    // and from the storage root's names.
    size_t frameCount() const { return m_frameCount; }
    int unresolvedFrames() const { return m_unresolved; }
    int typesFromTable() const { return m_typesFromTable; }
    int typesFromNames() const { return m_typesFromNames; }
    // LiquidTypeXTexture's frames also looked up by their template's expanded name in the storage's root, where the root
    // names files (MoP Classic): the FileDataID the root gives agreed with the row's, or not (the row's is kept).
    int framesCheckedByName() const { return m_checkedByName; }
    int framesDisagreeing() const { return m_disagreeing; }
    // The bumps: the types that have one, the distinct files, and the slot names with "bump" the storage root did not
    // list (a client without LiquidTypeXTexture rows).
    int typesWithBump() const { return m_typesWithBump; }
    size_t bumpCount() const { return m_bumpCount; }
    int unresolvedBumps() const { return m_unresolvedBumps; }
    // LiquidObject rows whose LiquidTypeID (masked to its 16 bits) is no LiquidType ID (-1: not counted).
    qint64 objectsWithoutType() const { return m_objectsWithoutType; }
    double buildMilliseconds() const { return m_buildMs; }

    // The catalogue as the player is sent it ("catalog" of a load's "liquids": product, build, state "ok" or
    // "unavailable", detail, types), made once when the catalogue is built: the same object goes with every load of the
    // client.
    const QJsonObject & toJson() const { return m_json; }
    // The same object for a load that has no catalogue of the loaded client: state "unavailable", no types.
    static QJsonObject unavailableJson(const QString & product, const QString & build, const QString & why);
    static QString statusName(Status s);

  private:
    LiquidCatalog() : m_frameIds(std::make_shared<std::vector<int>>()), m_textureIds(std::make_shared<std::vector<int>>()) {}
    void makeJson();

    Status m_status = Status::NoClient;
    QString m_statusDetail;
    QString m_product;
    QString m_build;
    QString m_dataPath;
    quint64 m_storageGeneration = 0;
    std::vector<LiquidTypeEntry> m_types;
    std::shared_ptr<const std::vector<int>> m_frameIds;
    std::shared_ptr<const std::vector<int>> m_textureIds;
    std::vector<Table> m_tables;
    size_t m_frameCount = 0;
    int m_unresolved = 0;
    int m_typesFromTable = 0;
    int m_typesFromNames = 0;
    int m_checkedByName = 0;
    int m_disagreeing = 0;
    int m_typesWithBump = 0;
    size_t m_bumpCount = 0;
    int m_unresolvedBumps = 0;
    qint64 m_objectsWithoutType = -1;
    double m_buildMs = 0.0;
    QJsonObject m_json;
  };
}

#endif /* _LIQUIDCATALOG_H_ */
