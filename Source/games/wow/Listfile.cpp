/*
 * Listfile.cpp
 */

#include "Listfile.h"

#include <algorithm>
#include <cstring>

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "LoadTimeline.h"
#include "logger/Logger.h"

namespace
{
  std::shared_ptr<const wow::Listfile> s_current; // the listfile read last, kept for the next client

  // The first field of a line as QString::toInt read it before: optional sign, decimal digits, surrounding blanks;
  // anything else (or a number out of range) is 0.
  int parseId(const uchar * begin, const uchar * end)
  {
    while (begin < end && (*begin == ' ' || *begin == '\t'))
      begin++;
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t'))
      end--;
    bool negative = false;
    if (begin < end && (*begin == '+' || *begin == '-'))
      negative = *begin++ == '-';
    if (begin == end)
      return 0;
    long long value = 0;
    for (const uchar * c = begin; c < end; c++)
    {
      if (*c < '0' || *c > '9')
        return 0;
      value = value * 10 + (*c - '0');
      if (value > 2147483648LL)
        return 0;
    }
    if (negative)
      value = -value;
    return (value > 2147483647LL || value < -2147483648LL) ? 0 : (int)value;
  }

  // The order file's header: what it was sorted from.
  struct OrderHeader
  {
    char magic[8];
    qint64 listSize;
    qint64 listModifiedMs;
    quint32 count;
    quint32 reserved;
  };
  const char ORDER_MAGIC[8] = {'W', 'M', 'V', 'L', 'S', 'T', 'O', '1'};
}

std::shared_ptr<const wow::Listfile> wow::Listfile::get(const QString & path, const std::function<void(float)> & progress,
                                                        bool & reused)
{
  reused = false;
  const QFileInfo info(path);
  if (!info.exists())
    return nullptr;
  if (s_current && s_current->m_file == info.absoluteFilePath() && s_current->m_size == info.size() &&
      s_current->m_modified == info.lastModified())
  {
    reused = true;
    return s_current;
  }
  std::shared_ptr<Listfile> list(new Listfile());
  if (!list->read(info.absoluteFilePath(), progress))
    return nullptr;
  s_current = list;
  return list;
}

bool wow::Listfile::read(const QString & fileName, const std::function<void(float)> & progress)
{
  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  const QFileInfo info(fileName);
  m_file = fileName;
  m_size = info.size();
  m_modified = info.lastModified();

  core::LoadTimeline::instance().begin("listfile read");
  // One pass over the bytes (mapped, not copied): ~2.3 million lines "id;path", split by hand.
  const qint64 size = file.size();
  QByteArray whole;
  const uchar * data = size > 0 ? file.map(0, size) : nullptr;
  const bool mapped = data != nullptr;
  if (!data && size > 0)
  {
    whole = file.readAll();
    if (whole.size() < size)
    {
      // A short read (an I/O error, a file cut while it was read) is not parsed: a partial list would be kept for the
      // whole session under the full file's size and time.
      LOG_ERROR << "Listfile: read" << whole.size() << "of" << size << "bytes of" << fileName;
      core::LoadTimeline::instance().end("listfile read", "read failed");
      return false;
    }
    data = reinterpret_cast<const uchar *>(whole.constData());
  }
  m_ids.reserve((size_t)(size / 60));
  m_paths.reserve((size_t)(size / 60));
  qint64 pos = 0, nextReport = 0;
  if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
    pos = 3; // a UTF-8 byte-order mark (an edited listfile), skipped as QTextStream skipped it
  const qint64 reportEvery = size > 0 ? size / 60 : 1;
  int previousId = 0;
  while (pos < size)
  {
    const uchar * line = data + pos;
    const uchar * newline = static_cast<const uchar *>(memchr(line, '\n', (size_t)(size - pos)));
    qint64 length = newline ? newline - line : size - pos;
    pos += length + 1;
    if (length > 0 && line[length - 1] == '\r')
      length--;
    if (progress && pos >= nextReport)
    {
      progress((float)std::min(pos, size) / (float)size);
      nextReport = pos + reportEvery;
    }
    const uchar * end = line + length;
    const uchar * semicolon = static_cast<const uchar *>(memchr(line, ';', (size_t)length));
    if (!semicolon)
      continue; // fewer than two fields
    const int id = parseId(line, semicolon);
    const uchar * name = semicolon + 1;
    if (const uchar * another = static_cast<const uchar *>(memchr(name, ';', (size_t)(end - name))))
      end = another; // the path is the second field
    const int n = (int)(end - name);
    bool ascii = true;
    for (int i = 0; i < n && ascii; i++)
      ascii = name[i] < 0x80;
    QString path;
    if (ascii)
    {
      path.resize(n);
      QChar * out = path.data();
      for (int i = 0; i < n; i++)
      {
        const uchar c = name[i];
        out[i] = QChar((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
      }
    }
    else
      path = QString::fromUtf8(reinterpret_cast<const char *>(name), n).toLower();
    if (!m_ids.empty() && id < previousId)
      m_idsAscending = false;
    previousId = id;
    m_ids.push_back(id);
    m_paths.push_back(std::move(path));
  }
  if (mapped)
    file.unmap(const_cast<uchar *>(data));
  core::LoadTimeline::instance().end("listfile read", QString("%1 entries%2").arg(m_ids.size())
                                                        .arg(m_idsAscending ? "" : ", not sorted by id"));

  if (!m_idsAscending)
  {
    m_byId.resize(m_ids.size());
    for (size_t i = 0; i < m_byId.size(); i++)
      m_byId[i] = (quint32)i;
    std::stable_sort(m_byId.begin(), m_byId.end(), [this](quint32 a, quint32 b) { return m_ids[a] < m_ids[b]; });
  }

  core::LoadTimeline::Stage order("listfile path order");
  const QString orderFile = fileName + ".order";
  if (readOrder(orderFile))
    order.note("from " + QFileInfo(orderFile).fileName());
  else
  {
    sortOrder();
    writeOrder(orderFile);
    order.note(QString("sorted %1 paths, kept in %2").arg(m_byPath.size()).arg(QFileInfo(orderFile).fileName()));
  }
  return true;
}

bool wow::Listfile::pathLess(quint32 a, quint32 b) const
{
  const int c = QString::compare(m_paths[a], m_paths[b], Qt::CaseSensitive);
  return c < 0 || (c == 0 && a < b);
}

void wow::Listfile::sortOrder()
{
  m_byPath.resize(m_paths.size());
  for (size_t i = 0; i < m_byPath.size(); i++)
    m_byPath[i] = (quint32)i;
  std::sort(m_byPath.begin(), m_byPath.end(), [this](quint32 a, quint32 b) { return pathLess(a, b); });
}

bool wow::Listfile::readOrder(const QString & orderFile)
{
  QFile file(orderFile);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  OrderHeader header;
  if (file.read(reinterpret_cast<char *>(&header), sizeof(header)) != (qint64)sizeof(header) ||
      memcmp(header.magic, ORDER_MAGIC, sizeof(ORDER_MAGIC)) != 0 || header.listSize != m_size ||
      header.listModifiedMs != m_modified.toMSecsSinceEpoch() || header.count != m_paths.size())
    return false;
  std::vector<quint32> order(header.count);
  const qint64 bytes = (qint64)order.size() * (qint64)sizeof(quint32);
  if (file.read(reinterpret_cast<char *>(order.data()), bytes) != bytes)
    return false;
  // Trusted only as what it claims to be: every entry once, in path order. (A wrong order would make paths
  // unfindable, so it is checked rather than believed: one comparison per entry.)
  std::vector<bool> seen(order.size());
  for (size_t i = 0; i < order.size(); i++)
  {
    const quint32 e = order[i];
    if (e >= order.size() || seen[e])
      return false;
    seen[e] = true;
    if (i > 0 && !pathLess(order[i - 1], e))
      return false;
  }
  m_byPath.swap(order);
  return true;
}

void wow::Listfile::writeOrder(const QString & orderFile) const
{
  QSaveFile file(orderFile);
  if (!file.open(QIODevice::WriteOnly))
  {
    LOG_WARNING << "Listfile: could not keep the path order in" << orderFile;
    return;
  }
  OrderHeader header;
  memcpy(header.magic, ORDER_MAGIC, sizeof(ORDER_MAGIC));
  header.listSize = m_size;
  header.listModifiedMs = m_modified.toMSecsSinceEpoch();
  header.count = (quint32)m_byPath.size();
  header.reserved = 0;
  file.write(reinterpret_cast<const char *>(&header), sizeof(header));
  file.write(reinterpret_cast<const char *>(m_byPath.data()), (qint64)m_byPath.size() * (qint64)sizeof(quint32));
  if (!file.commit())
    LOG_WARNING << "Listfile: could not keep the path order in" << orderFile;
}

QString wow::Listfile::pathOf(int id) const
{
  if (m_idsAscending)
  {
    // the last line with this id
    auto it = std::upper_bound(m_ids.begin(), m_ids.end(), id);
    if (it == m_ids.begin() || *(it - 1) != id)
      return QString();
    return m_paths[(size_t)(it - 1 - m_ids.begin())];
  }
  auto it = std::upper_bound(m_byId.begin(), m_byId.end(), id, [this](int v, quint32 e) { return v < m_ids[e]; });
  if (it == m_byId.begin() || m_ids[*(it - 1)] != id)
    return QString();
  return m_paths[*(it - 1)];
}

int wow::Listfile::idOf(const QString & path) const
{
  // the last line with this path: the end of its run in path order
  auto it = std::upper_bound(m_byPath.begin(), m_byPath.end(), path,
                             [this](const QString & p, quint32 e) { return QString::compare(p, m_paths[e]) < 0; });
  if (it == m_byPath.begin() || m_paths[*(it - 1)] != path)
    return -1;
  return m_ids[*(it - 1)];
}
