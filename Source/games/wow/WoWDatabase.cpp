/*
 * WoWDatabase.cpp
 *
 *  Created on: 9 nov. 2014
 *      Author: Jerome
 */

#include "WoWDatabase.h"

#include <QDomNamedNodeMap>
#include <QFile>

#include "Game.h"
#include "logger/Logger.h"
#include "dbdfile.h"
#include "wdb2file.h"
#include "wdb5file.h"
#include "wdb6file.h"
#include "wdc1file.h"
#include "wdc2file.h"
#include "wdc3file.h"

const std::vector<QString> POSSIBLE_DB_EXT = {".db2", ".dbc"};

wow::WoWDatabase::WoWDatabase()
  : GameDatabase()
{

}

core::TableStructure *  wow::WoWDatabase::createTableStructure()
{
  return new wow::TableStructure;
}

core::FieldStructure *  wow::WoWDatabase::createFieldStructure()
{
  return new wow::FieldStructure;
}

void wow::WoWDatabase::readSpecificTableAttributes(QDomElement & e, core::TableStructure * tblStruct)
{
  wow::TableStructure * tbl = dynamic_cast<wow::TableStructure *>(tblStruct);

  if (!tbl)
    return;

  QDomNamedNodeMap attributes = e.attributes();
  QDomNode hash = attributes.namedItem("layoutHash");

  if (!hash.isNull())
    tbl->hash = hash.nodeValue().toUInt();
}

void wow::WoWDatabase::readSpecificFieldAttributes(QDomElement & e, core::FieldStructure * fieldStruct)
{
  wow::FieldStructure * field = dynamic_cast<wow::FieldStructure *>(fieldStruct);

  if (!field)
    return;

  QDomNamedNodeMap attributes = e.attributes();

  QDomNode pos = attributes.namedItem("pos");
  QDomNode commonData = attributes.namedItem("commonData");
  QDomNode relationshipData = attributes.namedItem("relationshipData");

  if (!pos.isNull())
    field->pos = pos.nodeValue().toInt();

  if (!commonData.isNull())
    field->isCommonData = true;

  if (!relationshipData.isNull())
    field->isRelationshipData = true;
}

// Read a DB2's layout_hash (the fingerprint of its record structure) straight from the file
// header. Keying the WoWDBDefs match on this instead of the client build string lets a brand-new
// client build -- whose version string isn't in the bundled defs yet -- still resolve to the
// correct field layout automatically, as long as that structure is a known one. Returns 0 for a
// missing file or a non-WDC table (older WDB/DBC keep the field elsewhere and fall back to build
// matching), in which case the caller behaves exactly as before.
static uint32 readDB2LayoutHash(const QString & file)
{
  GameFile * f = 0;
  for (unsigned int i = 0; i < POSSIBLE_DB_EXT.size(); i++)
  {
    f = GAMEDIRECTORY.getFile("DBFilesClient\\" + file + POSSIBLE_DB_EXT[i]);
    if (f)
      break;
  }
  if (!f)
    return 0;

  uint32 layoutHash = 0;
  if (f->open(false))
  {
    // WDC1-4 keep layout_hash at byte offset 24 (magic[4] + 5 x uint32). WDC5 (WoW 11.0+) inserts
    // a preamble right after the magic -- a uint32 version + a 128-byte build string -- which
    // pushes the same field to offset 156. Read enough to cover both and pick by the format digit.
    unsigned char head[160];
    const size_t n = f->read(head, sizeof(head));
    if (n >= 28 && head[0] == 'W' && head[1] == 'D' && head[2] == 'C')
    {
      if (head[3] == '5')
      {
        if (n >= 160)
          layoutHash = *reinterpret_cast<uint32 *>(head + 156);
      }
      else
        layoutHash = *reinterpret_cast<uint32 *>(head + 24);
    }
    f->close();
  }
  return layoutHash;
}

void wow::WoWDatabase::refreshStructures(std::vector<core::TableStructure *> & tables)
{
  // The shipped database.xml carries WMV's curated column NAMES/types (which the
  // hardcoded SQL queries depend on) but version-specific field POSITIONS. For a
  // build newer than the shipped schema, refresh each field's DB2 field index
  // from its WoWDBDefs (.dbd) definition, matching fields by name. Tables/fields
  // without a matching .dbd entry simply keep their base positions.
  const QString build = GAMEDIRECTORY.version();
  if (build.isEmpty())
    return;

  LOG_INFO << "Refreshing database structures from WoWDBDefs for build" << build;

  for (core::TableStructure * t : tables)
  {
    wow::TableStructure * tbl = dynamic_cast<wow::TableStructure *>(t);
    if (!tbl)
      continue;

    // CSV-backed tables (e.g. AnimationData) read columns by order, not by DB2
    // field position, so the WoWDBDefs refresh does not apply to them.
    if (tbl->file.endsWith(".csv", Qt::CaseInsensitive))
      continue;

    const QString dbdPath = "dbd/" + tbl->name + ".dbd";
    if (!QFile::exists(dbdPath))
      continue; // no definition available -> keep base XML positions

    wow::DBDFile dbd;
    if (!dbd.parseFile(dbdPath))
    {
      LOG_WARNING << "DBD: failed to parse" << dbdPath;
      continue;
    }

    // Match by the file's actual structure fingerprint first, then by build. Exact layout-hash
    // matching means the chosen definition IS this file's real layout, so its field order is
    // authoritative -- this is what lets a new client build self-correct without curated edits.
    const uint32 fileHash = readDB2LayoutHash(tbl->file);
    const wow::DBDDefinition * def = dbd.getStructure(build, fileHash);
    if (!def)
    {
      LOG_WARNING << "DBD: no definition for table" << tbl->name << "build" << build
                  << "layoutHash" << fileHash << "- keeping base positions";
      continue;
    }

    // Map column name -> DB2 field index. Non-inline id/relation fields live in
    // the id-list / relationship section and do NOT occupy a record field slot.
    QMap<QString, int> nameToPos;
    int idx = 0;
    for (const wow::DBDField & f : def->fields)
    {
      if ((f.isID || f.isRelation) && !f.isInline)
        continue;
      nameToPos[f.name.toLower()] = idx;
      idx++;
    }

    int refreshed = 0, changed = 0, missing = 0;
    for (core::FieldStructure * cf : tbl->fields)
    {
      wow::FieldStructure * field = dynamic_cast<wow::FieldStructure *>(cf);
      if (!field || field->isKey || field->isRelationshipData)
        continue; // id / relationship fields are not read by position

      auto it = nameToPos.find(field->name.toLower());
      if (it != nameToPos.end())
      {
        if (field->pos != it.value())
        {
          // Surfaced so a regression on a known-good build is obvious: any change here means the
          // resolved layout differs from the curated base position for this field.
          LOG_INFO << "DBD:" << tbl->name << "field" << field->name << "pos" << field->pos << "->" << it.value();
          changed++;
        }
        field->pos = it.value();
        refreshed++;
      }
      else
      {
        missing++;
        LOG_WARNING << "DBD:" << tbl->name << "field" << field->name << "absent in build" << build << "- keeping pos" << field->pos;
      }
    }
    LOG_INFO << "DBD refreshed" << tbl->name << "layoutHash" << fileHash << "-" << refreshed << "fields updated,"
             << changed << "changed," << missing << "unmatched";
  }
}

DBFile * wow::TableStructure::createDBFile()
{
  DBFile * result = core::TableStructure::createDBFile();

  if (result != 0)
    return result;

  GameFile * fileToOpen = 0;
  // loop over possible extension to check if file exists
  for (unsigned int i = 0; i < POSSIBLE_DB_EXT.size(); i++)
  {
    fileToOpen = GAMEDIRECTORY.getFile("DBFilesClient\\" + file + POSSIBLE_DB_EXT[i]);
    if (fileToOpen)
      break;
  }

  if (!fileToOpen)
    return 0;

  
  if (fileToOpen)
  {
    if (fileToOpen->open(false))
    {
      char header[5];

      fileToOpen->read(header, 4);

      if (strncmp(header, "WDB2", 4) == 0)
        result = new WDB2File(fileToOpen->fullname());
      else if (strncmp(header, "WDB5", 4) == 0)
        result = new WDB5File(fileToOpen->fullname());
      else if (strncmp(header, "WDB6", 4) == 0)
        result = new WDB6File(fileToOpen->fullname());
      else if (strncmp(header, "WDC1", 4) == 0)
        result = new WDC1File(fileToOpen->fullname());
      else if (strncmp(header, "WDC2", 4) == 0)
        result = new WDC2File(fileToOpen->fullname());
      else if (strncmp(header, "WDC3", 4) == 0)
        result = new WDC3File(fileToOpen->fullname());
      else if (strncmp(header, "WDC4", 4) == 0)
        result = new WDC3File(fileToOpen->fullname());
      else if (strncmp(header, "WDC5", 4) == 0)
        result = new WDC3File(fileToOpen->fullname());
      else
        LOG_ERROR << "Unsupported database file" << header[0] << header[1] << header[2] << header[3];

      fileToOpen->close();
    }
  }

  return result;
}

void wow::WoWDatabase::createIndices()
{
  // database.xml declares only PRIMARY KEYs (autoindex on ID), so every per-load lookup/join
  // on a non-PK column was a full table scan (ItemModifiedAppearance ~156k, TextureFileData
  // ~210k, ItemDisplayInfoMaterialRes ~221k rows, plus the ChrCustomization* + Creature*
  // joins). These secondary indexes turn those scans into index seeks. Issued idempotently
  // (IF NOT EXISTS) every launch, so an existing on-disk cache gains them once -- no full
  // DB2 rebuild needed. Each runs independently so a missing table can't abort the rest.
  static const char * const indices[] = {
    "CREATE INDEX IF NOT EXISTS idx_ima_itemid      ON ItemModifiedAppearance(ItemID)",
    "CREATE INDEX IF NOT EXISTS idx_idimr_idi       ON ItemDisplayInfoMaterialRes(ItemDisplayInfoID)",
    "CREATE INDEX IF NOT EXISTS idx_tfd_mrid        ON TextureFileData(MaterialResourcesID)",
    "CREATE INDEX IF NOT EXISTS idx_mfd_mrid        ON ModelFileData(ModelResourcesID)",
    "CREATE INDEX IF NOT EXISTS idx_cmfd_race       ON ComponentModelFileData(RaceID)",
    "CREATE INDEX IF NOT EXISTS idx_ctfd_race       ON ComponentTextureFileData(RaceID)",
    "CREATE INDEX IF NOT EXISTS idx_ccelem_choice   ON ChrCustomizationElement(ChrCustomizationChoiceID)",
    "CREATE INDEX IF NOT EXISTS idx_ccchoice_option ON ChrCustomizationChoice(ChrCustomizationOptionID)",
    "CREATE INDEX IF NOT EXISTS idx_ccchoice_req    ON ChrCustomizationChoice(ChrCustomizationReqID)",
    "CREATE INDEX IF NOT EXISTS idx_ccreqchoice_req ON ChrCustomizationReqChoice(ChrCustomizationReqID)",
    "CREATE INDEX IF NOT EXISTS idx_cdigd_cdi       ON CreatureDisplayInfoGeosetData(CreatureDisplayInfoID)",
    "CREATE INDEX IF NOT EXISTS idx_cmd_fdid        ON CreatureModelData(FileDataID)",
    "CREATE INDEX IF NOT EXISTS idx_cdi_model       ON CreatureDisplayInfo(ModelID)",
  };
  for (const char * const sql : indices)
    sqlQuery(QString::fromLatin1(sql));
}
