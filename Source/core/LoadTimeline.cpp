/*
 * LoadTimeline.cpp
 */

#include "LoadTimeline.h"

#include "logger/Logger.h"

#ifdef _WIN32
#  include <windows.h>
#  include <psapi.h>
#endif

namespace
{
  // "  1234.56" -- milliseconds, right-aligned for the summary.
  QString ms(double v)
  {
    return QString::number(v, 'f', 2).rightJustified(9);
  }
}

core::LoadTimeline & core::LoadTimeline::instance()
{
  static LoadTimeline timeline;
  return timeline;
}

void core::LoadTimeline::start(const QString & what)
{
  m_clock.start();
  m_running = true;
  m_what = what;
  m_entries.clear();
  m_open.clear();
  m_caches.clear();
  LOG_INFO << "[clientload] ===== load started:" << what.toUtf8().constData() << "|" << memory().toUtf8().constData();
}

void core::LoadTimeline::startIfIdle(const QString & what)
{
  if (!m_running)
    start(what);
}

double core::LoadTimeline::nowMs() const
{
  return m_clock.isValid() ? m_clock.nsecsElapsed() / 1.0e6 : 0.0;
}

void core::LoadTimeline::begin(const QString & stage)
{
  if (!m_running)
    return;
  Entry e;
  e.name = stage;
  e.start = nowMs();
  e.depth = (int)m_open.size();
  m_entries.push_back(e);
  m_open.push_back(m_entries.size() - 1);
}

void core::LoadTimeline::end(const QString & stage, const QString & note)
{
  if (!m_running)
    return;
  // The innermost open stage of that name (a stage left open by an early return is closed with its parent).
  for (size_t i = m_open.size(); i-- > 0;)
  {
    Entry & e = m_entries[m_open[i]];
    if (e.name != stage)
      continue;
    e.end = nowMs();
    e.note = note;
    LOG_INFO << "[clientload]" << QString("%1%2").arg(QString(e.depth * 2, ' ')).arg(e.name).leftJustified(28).toUtf8().constData()
             << "start" << ms(e.start).toUtf8().constData() << "ms  end" << ms(e.end).toUtf8().constData()
             << "ms  duration" << ms(e.end - e.start).toUtf8().constData() << "ms"
             << (note.isEmpty() ? QString() : "| " + note).toUtf8().constData();
    m_open.erase(m_open.begin() + i, m_open.end());
    return;
  }
}

void core::LoadTimeline::cache(const QString & name, const QString & outcome, const QString & detail)
{
  const QString line = name + " " + outcome + (detail.isEmpty() ? QString() : " (" + detail + ")");
  LOG_INFO << "[clientload] cache" << line.toUtf8().constData();
  if (m_running)
    m_caches << line;
}

void core::LoadTimeline::mark(const QString & what, const QString & note)
{
  if (!m_running)
    return;
  Entry e;
  e.name = what;
  e.start = e.end = nowMs();
  e.depth = (int)m_open.size();
  e.note = note;
  e.mark = true;
  m_entries.push_back(e);
  LOG_INFO << "[clientload]" << QString("%1%2").arg(QString(e.depth * 2, ' ')).arg(what).leftJustified(28).toUtf8().constData()
           << "at   " << ms(e.start).toUtf8().constData() << "ms"
           << (note.isEmpty() ? QString() : "| " + note).toUtf8().constData();
}

void core::LoadTimeline::finish(const QString & outcome)
{
  if (!m_running)
    return;
  const double total = nowMs();
  // Stages still open (a failure returned early) end now.
  for (size_t i : m_open)
    if (m_entries[i].end < 0)
      m_entries[i].end = total;
  m_open.clear();

  LOG_INFO << "[clientload] ===== summary:" << m_what.toUtf8().constData() << "|" << outcome.toUtf8().constData() << "after"
           << QString::number(total, 'f', 1).toUtf8().constData() << "ms";
  LOG_INFO << "[clientload]   stage                        start       end  duration";
  for (const Entry & e : m_entries)
  {
    const QString name = (QString(e.depth * 2, ' ') + e.name).leftJustified(28);
    if (e.mark)
      LOG_INFO << "[clientload]  " << name.toUtf8().constData() << ms(e.start).toUtf8().constData() << "        (mark)"
               << (e.note.isEmpty() ? QString() : "| " + e.note).toUtf8().constData();
    else
      LOG_INFO << "[clientload]  " << name.toUtf8().constData() << ms(e.start).toUtf8().constData()
               << ms(e.end).toUtf8().constData() << ms(e.end - e.start).toUtf8().constData()
               << (e.note.isEmpty() ? QString() : "| " + e.note).toUtf8().constData();
  }
  for (const QString & c : m_caches)
    LOG_INFO << "[clientload]   cache" << c.toUtf8().constData();
  LOG_INFO << "[clientload]   memory" << memory().toUtf8().constData();
  LOG_INFO << "[clientload] ===== end of summary";
  m_running = false;
}

QString core::LoadTimeline::memory()
{
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX pmc = {};
  pmc.cb = sizeof(pmc);
  if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
    return QString("private %1 MB, working set %2 MB")
      .arg(pmc.PrivateUsage / (1024.0 * 1024.0), 0, 'f', 1)
      .arg(pmc.WorkingSetSize / (1024.0 * 1024.0), 0, 'f', 1);
#endif
  return QString("memory unknown");
}
