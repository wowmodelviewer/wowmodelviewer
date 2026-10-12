/*
 * dbdfile.cpp
 */

#include "dbdfile.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace
{
  // Parse "major.minor.patch.rev" into out[4]. Returns false if malformed.
  bool parseBuild(const QString & b, int out[4])
  {
    static const QRegularExpression re("(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)");
    QRegularExpressionMatch m = re.match(b);
    if (!m.hasMatch())
      return false;
    for (int i = 0; i < 4; i++)
      out[i] = m.captured(i + 1).toInt();
    return true;
  }

  // 4-tuple version comparison: <0 if x<y, 0 if equal, >0 if x>y.
  int cmpBuild(const int x[4], const int y[4])
  {
    for (int i = 0; i < 4; i++)
      if (x[i] != y[i])
        return x[i] < y[i] ? -1 : 1;
    return 0;
  }

  bool buildInRange(const QString & build, const QString & min, const QString & max)
  {
    int b[4], lo[4], hi[4];
    if (!parseBuild(build, b) || !parseBuild(min, lo) || !parseBuild(max, hi))
      return false;
    return cmpBuild(lo, b) <= 0 && cmpBuild(b, hi) <= 0;
  }
}

QString wow::DBDField::wmvType() const
{
  if (baseType == "float")
    return "float";
  if (baseType == "string" || baseType == "locstring")
    return "text";

  // integer: width comes from the <..> annotation, defaulting to 32 bits
  const int bits = (size > 0) ? size : 32;
  QString base;
  switch (bits)
  {
    case 8:  base = "int8";  break;
    case 16: base = "int16"; break;
    case 64: base = "int64"; break;
    case 32:
    default: base = "int32"; break;
  }
  if (!isSigned)
    base = "u" + base;  // -> uint8 / uint16 / uint32 / uint64
  return base;
}

bool wow::DBDDefinition::isValidFor(const QString & build, uint32 layoutHash) const
{
  // layout hash match first (exact, fastest)
  for (uint32 h : layoutHashes)
    if (h == layoutHash)
      return true;

  if (builds.contains(build))
    return true;

  for (const auto & r : buildRanges)
    if (buildInRange(build, r.first, r.second))
      return true;

  return false;
}

bool wow::DBDFile::parse(const QString & text)
{
  m_columns.clear();
  m_defs.clear();

  // Split into chunks separated by blank lines.
  std::vector<QStringList> chunks;
  QStringList current;
  const QStringList lines = text.split('\n');
  for (QString line : lines)
  {
    if (line.endsWith('\r'))
      line.chop(1);
    if (line.trimmed().isEmpty())
    {
      if (!current.isEmpty())
      {
        chunks.push_back(current);
        current.clear();
      }
    }
    else
    {
      current.push_back(line);
    }
  }
  if (!current.isEmpty())
    chunks.push_back(current);

  if (chunks.empty())
    return false;

  bool columnsParsed = false;
  for (const QStringList & chunk : chunks)
  {
    if (!columnsParsed)
    {
      if (chunk.first().trimmed() != "COLUMNS")
        return false;  // malformed: first chunk must be COLUMNS
      parseColumns(chunk);
      columnsParsed = true;
      continue;
    }
    parseDefinition(chunk);
  }

  return columnsParsed;
}

bool wow::DBDFile::parseFile(const QString & path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return false;
  QTextStream in(&f);
  const QString text = in.readAll();
  f.close();
  return parse(text);
}

void wow::DBDFile::parseColumns(const QStringList & lines)
{
  // <baseType>[<Table::Column>] <columnName>[?]
  static const QRegularExpression re("^(int|float|locstring|string)(<[^:]+::[^>]+>)?\\s+(\\S+)");

  for (const QString & raw : lines)
  {
    const QString line = raw.trimmed();
    if (line.isEmpty() || line == "COLUMNS" || line.startsWith("COMMENT"))
      continue;

    QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch())
      continue;

    const QString type = m.captured(1);
    QString name = m.captured(3);
    if (name.endsWith('?'))           // unverified marker
      name.chop(1);

    m_columns[name] = type;
  }
}

void wow::DBDFile::parseDefinition(const QStringList & lines)
{
  static const QRegularExpression layoutRe("^LAYOUT\\s+(.*)");
  static const QRegularExpression buildRe("^BUILD\\s+(.*)");
  // [$annotations$] name [<[u]bits>] [[arrayLen]]
  static const QRegularExpression fieldRe("^(\\$([^$]+)\\$)?([^<\\[]+)(<(u?)(\\d+)>)?(\\[(\\d+)\\])?$");

  DBDDefinition def;

  for (const QString & raw : lines)
  {
    const QString line = raw.trimmed();
    if (line.isEmpty() || line.startsWith("COMMENT"))
      continue;

    QRegularExpressionMatch lm = layoutRe.match(line);
    if (lm.hasMatch())
    {
      const QStringList hashes = lm.captured(1).split(',', QString::SkipEmptyParts);
      for (const QString & h : hashes)
      {
        bool ok = false;
        const uint32 v = h.trimmed().toUInt(&ok, 16);
        if (ok)
          def.layoutHashes.push_back(v);
      }
      continue;
    }

    QRegularExpressionMatch bm = buildRe.match(line);
    if (bm.hasMatch())
    {
      const QStringList parts = bm.captured(1).split(',', QString::SkipEmptyParts);
      for (QString p : parts)
      {
        p = p.trimmed();
        if (p.contains('-'))
        {
          const QStringList mm = p.split('-');
          if (mm.size() == 2)
            def.buildRanges.push_back(std::make_pair(mm[0].trimmed(), mm[1].trimmed()));
        }
        else
        {
          def.builds.push_back(p);
        }
      }
      continue;
    }

    QRegularExpressionMatch fm = fieldRe.match(line);
    if (!fm.hasMatch())
      continue;

    DBDField field;
    const QString annotations = fm.captured(2);  // contents of $...$
    field.name = fm.captured(3).trimmed();
    const QString signedness = fm.captured(5);   // "u" or ""
    const QString sizeStr = fm.captured(6);
    const QString arrStr = fm.captured(8);

    if (!annotations.isEmpty())
    {
      const QStringList flags = annotations.split(',', QString::SkipEmptyParts);
      for (QString f : flags)
      {
        f = f.trimmed();
        if (f == "id")            field.isID = true;
        else if (f == "noninline") field.isInline = false;
        else if (f == "relation")  field.isRelation = true;
      }
    }

    if (signedness == "u")
      field.isSigned = false;
    if (!sizeStr.isEmpty())
      field.size = sizeStr.toInt();
    if (!arrStr.isEmpty())
      field.arrayLength = arrStr.toInt();

    field.baseType = m_columns.value(field.name, "int");

    def.fields.push_back(field);
  }

  if (!def.fields.empty())
    m_defs.push_back(def);
}

const wow::DBDDefinition * wow::DBDFile::getStructureByLayout(uint32 layoutHash) const
{
  for (const DBDDefinition & def : m_defs)
    for (uint32 h : def.layoutHashes)
      if (h == layoutHash)
        return &def;
  return nullptr;
}

const wow::DBDDefinition * wow::DBDFile::getStructure(const QString & build, uint32 layoutHash) const
{
  // Exact match only -- listed layout hash, build, or build range. When the build isn't
  // listed (a client newer than the bundled WoWDBDefs), return nullptr so the caller keeps
  // the curated base positions from database.xml.
  //
  // NOTE: a "nearest layout below the build" fallback was tried here, but the DBD field-index
  // it produces does not match how WMV reads several tables' records (arrays / id handling),
  // so applying it broadly mis-read columns and broke character + other models. The base
  // database.xml positions are hand-curated for the current 12.x layout and are the source of
  // truth; the only stale one (CreatureDisplayInfo.TextureVariationFileDataID) is fixed there.
  for (const DBDDefinition & def : m_defs)
    if (def.isValidFor(build, layoutHash))
      return &def;
  return nullptr;
}
