/*
 * dbdfile.h
 *
 * Parser for WoWDBDefs ".dbd" database definition files.
 *
 * A .dbd file describes the layout of one DB2 table across many game builds:
 *   - a COLUMNS section listing every column and its base type
 *   - one or more definition blocks, each tagged with LAYOUT hash(es) and/or
 *     BUILD version(s)/range(s), followed by the ordered field list for that
 *     layout (the order is the DB2 field index, 0-based).
 *
 * Used to resolve, at runtime, the correct field order/types for the WoW
 * build that is actually loaded -- replacing the per-version database.xml
 * positions so the viewer adapts to new patches. See WoWDBDefs:
 *   https://github.com/wowdev/WoWDBDefs
 */

#ifndef DBDFILE_H
#define DBDFILE_H

#include <vector>
#include <utility>

#include <QString>
#include <QStringList>
#include <QMap>

#include "types.h"

namespace wow
{
  // A single field within a definition block, in DB2 field-index order.
  struct DBDField
  {
    QString name;
    QString baseType;          // from COLUMNS: int / float / string / locstring
    bool    isSigned = true;
    int     size = -1;         // bit width from <..>; -1 = unset (int defaults to 32)
    bool    isID = false;
    bool    isInline = true;
    bool    isRelation = false;
    int     arrayLength = -1;  // -1 = scalar field

    // Maps to the WMV TableStructure type string used by WDC3File::get()
    // (int8/uint8/int16/uint16/int32/uint32/int64/uint64/float/text).
    QString wmvType() const;
    unsigned int wmvArraySize() const { return arrayLength > 0 ? static_cast<unsigned int>(arrayLength) : 1; }
  };

  struct DBDDefinition
  {
    std::vector<uint32>                       layoutHashes;  // parsed from LAYOUT hex
    QStringList                               builds;        // exact "x.y.z.w"
    std::vector<std::pair<QString, QString> > buildRanges;   // (min, max)
    std::vector<DBDField>                     fields;        // DB2 field-index order

    bool isValidFor(const QString & build, uint32 layoutHash) const;
  };

  class DBDFile
  {
  public:
    DBDFile() {}

    bool parse(const QString & text);
    bool parseFile(const QString & path);

    // First definition matching the given build/layoutHash (layout takes
    // priority), or nullptr if none.
    const DBDDefinition * getStructure(const QString & build, uint32 layoutHash) const;
    // The first definition whose LAYOUT list holds this exact hash -- never one chosen by its BUILD lines (a table read
    // only from a verified layout, WoWDatabase verifiedLayoutOnly), or nullptr.
    const DBDDefinition * getStructureByLayout(uint32 layoutHash) const;

    bool isValid() const { return !m_defs.empty(); }

  private:
    void parseColumns(const QStringList & lines);
    void parseDefinition(const QStringList & lines);

    QMap<QString, QString>     m_columns;  // column name -> base type
    std::vector<DBDDefinition> m_defs;
  };
}

#endif // DBDFILE_H
