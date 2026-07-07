/*
 * WotlkDbc.cpp
 */

#include "WotlkDbc.h"

#include <algorithm> // std::find
#include <cstring>   // memcmp/memcpy

#include "Game.h"  // GAMEDIRECTORY
#include "GameFile.h"
#include "logger/Logger.h"

namespace
{
  // WotLK 3.3.5 (build 12340) fixed field layouts. Classic WDBC schemas are stable across the
  // whole expansion, so these indices are hardcoded (there are no .dbd definitions for WDBC).
  // CreatureModelData.dbc
  const unsigned int CMD_MODELNAME   = 2;   // string: "Creature\\Rabbit\\Rabbit.mdx"
  // CreatureDisplayInfo.dbc
  const unsigned int CDI_MODELID     = 1;   // -> CreatureModelData.ID
  const unsigned int CDI_TEXVAR0     = 6;   // string base names (3 slots -> GAMEOBJECT1/2/3)
  const unsigned int CDI_TEXVAR1     = 7;
  const unsigned int CDI_TEXVAR2     = 8;
  // ItemDisplayInfo.dbc
  const unsigned int IDI_MODELNAME0  = 1;   // string: model file name (left/main)
  const unsigned int IDI_MODELNAME1  = 2;   // string: model file name (right)
  const unsigned int IDI_MODELTEX0   = 3;   // string base name of the texture for model 0
  const unsigned int IDI_MODELTEX1   = 4;   // string base name of the texture for model 1
}

wow::WotlkDbc & wow::WotlkDbc::instance()
{
  static WotlkDbc s_instance;
  return s_instance;
}

void wow::WotlkDbc::reset()
{
  m_loaded = false;
  m_modelKeyToId.clear();
  m_creatureDisplays.clear();
  m_itemTextures.clear();
}

unsigned int wow::WotlkDbc::DbcTable::u(unsigned int r, unsigned int field) const
{
  if (!ok || r >= recordCount || field >= fieldCount)
    return 0;
  unsigned int v = 0;
  std::memcpy(&v, record(r) + (size_t)field * 4, 4);
  return v;
}

QString wow::WotlkDbc::DbcTable::s(unsigned int r, unsigned int field) const
{
  const unsigned int off = u(r, field);
  if (off == 0 || off >= stringSize)
    return QString();
  return QString::fromUtf8(strings() + off);
}

QString wow::WotlkDbc::normModelKey(const QString & path)
{
  QString p = path.toLower();
  p.replace('\\', '/');
  const int dot = p.lastIndexOf('.'); // drop .mdx / .m2 / .mdl so CreatureModelData(.mdx) matches
  if (dot > p.lastIndexOf('/'))
    p = p.left(dot);
  return p;
}

QString wow::WotlkDbc::baseKey(const QString & name)
{
  QString p = name.toLower();
  p.replace('\\', '/');
  const int slash = p.lastIndexOf('/');
  if (slash >= 0)
    p = p.mid(slash + 1);
  const int dot = p.lastIndexOf('.');
  if (dot >= 0)
    p = p.left(dot);
  return p;
}

wow::WotlkDbc::DbcTable wow::WotlkDbc::loadDbc(const QString & name)
{
  DbcTable t;
  GameFile * f = GAMEDIRECTORY.getFile(name);
  if (!f)
  {
    LOG_ERROR << "[wotlkdbc] file not found:" << name;
    return t;
  }
  if (!f->open())
  {
    LOG_ERROR << "[wotlkdbc] failed to open:" << name;
    return t;
  }

  const size_t sz = f->getSize();
  const unsigned char * buf = f->getBuffer();
  if (sz < 20 || buf == nullptr || std::memcmp(buf, "WDBC", 4) != 0)
  {
    LOG_ERROR << "[wotlkdbc]" << name << "is not a WDBC file (size" << (int)sz << ")";
    f->close();
    return t;
  }

  std::memcpy(&t.recordCount, buf + 4, 4);
  std::memcpy(&t.fieldCount, buf + 8, 4);
  std::memcpy(&t.recordSize, buf + 12, 4);
  std::memcpy(&t.stringSize, buf + 16, 4);

  const size_t expected = 20 + (size_t)t.recordCount * t.recordSize + t.stringSize;
  if (t.recordSize < t.fieldCount * 4 || expected > sz)
  {
    LOG_ERROR << "[wotlkdbc]" << name << "header looks wrong (records" << (int)t.recordCount
              << "fields" << (int)t.fieldCount << "recordSize" << (int)t.recordSize
              << "stringSize" << (int)t.stringSize << "file" << (int)sz << ")";
    f->close();
    return t;
  }

  t.data.assign(buf, buf + sz);
  t.ok = true;
  f->close();

  LOG_INFO << "[wotlkdbc] opened" << name << ": records=" << (int)t.recordCount
           << " fields=" << (int)t.fieldCount << " recordSize=" << (int)t.recordSize
           << " stringSize=" << (int)t.stringSize;
  return t;
}

bool wow::WotlkDbc::ensureLoaded()
{
  if (m_loaded)
    return !m_modelKeyToId.empty() || !m_itemTextures.empty();

  m_loaded = true;

  // ---- CreatureModelData: model path -> ID ----
  DbcTable cmd = loadDbc("dbfilesclient/creaturemodeldata.dbc");
  if (cmd.ok)
  {
    for (unsigned int r = 0; r < cmd.recordCount; r++)
    {
      const int id = (int)cmd.u(r, 0);
      const QString model = cmd.s(r, CMD_MODELNAME);
      if (!model.isEmpty())
        m_modelKeyToId[normModelKey(model)] = id;
    }
    if (cmd.recordCount > 0)
      LOG_INFO << "[wotlkdbc]   CreatureModelData sample: id=" << (int)cmd.u(0, 0)
               << " ModelName(field" << (int)CMD_MODELNAME << ")=" << cmd.s(0, CMD_MODELNAME);
    LOG_INFO << "[wotlkdbc]   indexed" << (int)m_modelKeyToId.size() << "CreatureModelData model paths";
  }

  // ---- CreatureDisplayInfo: ModelID -> texture variations ----
  DbcTable cdi = loadDbc("dbfilesclient/creaturedisplayinfo.dbc");
  if (cdi.ok)
  {
    for (unsigned int r = 0; r < cdi.recordCount; r++)
    {
      const int modelId = (int)cdi.u(r, CDI_MODELID);
      std::array<QString, 3> tv = { cdi.s(r, CDI_TEXVAR0), cdi.s(r, CDI_TEXVAR1), cdi.s(r, CDI_TEXVAR2) };
      if (!tv[0].isEmpty() || !tv[1].isEmpty() || !tv[2].isEmpty())
        m_creatureDisplays.insert(std::make_pair(modelId, tv));
    }
    if (cdi.recordCount > 0)
      LOG_INFO << "[wotlkdbc]   CreatureDisplayInfo sample: modelID(field" << (int)CDI_MODELID << ")="
               << (int)cdi.u(0, CDI_MODELID) << " texVar=" << cdi.s(0, CDI_TEXVAR0) << "/"
               << cdi.s(0, CDI_TEXVAR1) << "/" << cdi.s(0, CDI_TEXVAR2);
    LOG_INFO << "[wotlkdbc]   indexed" << (int)m_creatureDisplays.size() << "CreatureDisplayInfo texture rows";
  }

  // ---- ItemDisplayInfo: model name -> texture base name ----
  DbcTable idi = loadDbc("dbfilesclient/itemdisplayinfo.dbc");
  if (idi.ok)
  {
    for (unsigned int r = 0; r < idi.recordCount; r++)
    {
      const QString m0 = idi.s(r, IDI_MODELNAME0), t0 = idi.s(r, IDI_MODELTEX0);
      const QString m1 = idi.s(r, IDI_MODELNAME1), t1 = idi.s(r, IDI_MODELTEX1);
      if (!m0.isEmpty() && !t0.isEmpty())
        m_itemTextures.insert(std::make_pair(baseKey(m0), t0));
      if (!m1.isEmpty() && !t1.isEmpty())
        m_itemTextures.insert(std::make_pair(baseKey(m1), t1));
    }
    if (idi.recordCount > 0)
      LOG_INFO << "[wotlkdbc]   ItemDisplayInfo sample: model0(field" << (int)IDI_MODELNAME0 << ")="
               << idi.s(0, IDI_MODELNAME0) << " tex0(field" << (int)IDI_MODELTEX0 << ")=" << idi.s(0, IDI_MODELTEX0);
    LOG_INFO << "[wotlkdbc]   indexed" << (int)m_itemTextures.size() << "ItemDisplayInfo model->texture entries";
  }

  return !m_modelKeyToId.empty() || !m_itemTextures.empty();
}

bool wow::WotlkDbc::resolveCreatureSkins(const QString & modelPath, std::vector<CreatureSkin> & out)
{
  if (!ensureLoaded())
    return false;

  const QString key = normModelKey(modelPath);
  auto it = m_modelKeyToId.find(key);
  if (it == m_modelKeyToId.end())
  {
    LOG_INFO << "[wotlkdbc] no CreatureModelData row for" << modelPath << "(key" << key << ")";
    return false;
  }
  const int modelId = it->second;

  const int slash = modelPath.lastIndexOf('/');
  const QString dir = (slash >= 0) ? modelPath.left(slash + 1) : QString();

  auto range = m_creatureDisplays.equal_range(modelId);
  for (auto d = range.first; d != range.second; ++d)
  {
    CreatureSkin skin;
    bool any = false;
    for (int i = 0; i < 3; i++)
    {
      const QString & tv = d->second[i];
      if (!tv.isEmpty())
      {
        skin.tex[i] = (dir + tv + ".blp").toLower();
        any = true;
      }
    }
    if (any)
      out.push_back(skin);
  }

  LOG_INFO << "[wotlkdbc] creature" << modelPath << "-> modelDataID" << modelId << ":"
           << (int)out.size() << "skin variation(s)";
  return !out.empty();
}

bool wow::WotlkDbc::resolveItemTextures(const QString & modelPath, std::vector<QString> & texturePathsOut)
{
  if (!ensureLoaded())
    return false;

  const QString mk = baseKey(modelPath);
  const int slash = modelPath.lastIndexOf('/');
  const QString dir = (slash >= 0) ? modelPath.left(slash + 1) : QString();

  auto range = m_itemTextures.equal_range(mk);
  for (auto d = range.first; d != range.second; ++d)
  {
    if (!d->second.isEmpty())
    {
      const QString path = (dir + d->second + ".blp").toLower();
      if (std::find(texturePathsOut.begin(), texturePathsOut.end(), path) == texturePathsOut.end())
        texturePathsOut.push_back(path);
    }
  }

  LOG_INFO << "[wotlkdbc] item" << modelPath << "(key" << mk << ") ->"
           << (int)texturePathsOut.size() << "texture(s)";
  return !texturePathsOut.empty();
}
