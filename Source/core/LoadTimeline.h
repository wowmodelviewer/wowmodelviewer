/*
 * LoadTimeline.h
 *
 * THE CLIENT LOAD'S TIMELINE. Opening a World of Warcraft client -- from the click on its card to the moment the
 * viewer can be used -- is timed stage by stage with the performance counter (QElapsedTimer) and written to the log:
 *
 *   [clientload] storage                start   412.30 ms  end  1655.82 ms  duration  1243.52 ms
 *   [clientload] cache database HIT (12.1.0.69933|wow|games/wow/12.0/|schema17)
 *
 * and, when the load is over, as one summary that lists every stage, every cache's outcome (HIT, MISS or REBUILD) and
 * the process's memory. Stages nest (a stage begun inside another is listed under it). One load at a time, on the GUI
 * thread: the viewer refuses a second load while one runs.
 */

#ifndef _LOADTIMELINE_H_
#define _LOADTIMELINE_H_

#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <vector>

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _LOADTIMELINE_API_ __declspec(dllexport)
#    else
#        define _LOADTIMELINE_API_ __declspec(dllimport)
#    endif
#else
#    define _LOADTIMELINE_API_
#endif

namespace core
{
  class _LOADTIMELINE_API_ LoadTimeline
  {
  public:
    static LoadTimeline & instance();

    // A load starts: the clock restarts and the last load's stages are forgotten. what names the client.
    void start(const QString & what);
    // Starts a load only when none is running (a load not started from the chooser: headless, File menu legacy...).
    void startIfIdle(const QString & what);
    bool running() const { return m_running; }
    // Milliseconds since start(), to the microsecond.
    double nowMs() const;

    void begin(const QString & stage);
    void end(const QString & stage, const QString & note = QString());
    // A cache's outcome in this load: HIT, MISS or REBUILD, and why.
    void cache(const QString & name, const QString & outcome, const QString & detail = QString());
    // A moment, with no duration ("interactive").
    void mark(const QString & what, const QString & note = QString());
    // The load is over (interactive, or failed): logs the summary and stops the clock.
    void finish(const QString & outcome);

    // The process's private bytes and working set, in MB, as "private N MB, working set N MB".
    static QString memory();

    // A stage for the length of a scope.
    class Stage
    {
    public:
      explicit Stage(const QString & name) : m_name(name) { LoadTimeline::instance().begin(m_name); }
      ~Stage() { LoadTimeline::instance().end(m_name, m_note); }
      void note(const QString & n) { m_note = n; }
    private:
      QString m_name;
      QString m_note;
    };

  private:
    LoadTimeline() = default;

    struct Entry
    {
      QString name;
      double start = 0, end = -1;
      int depth = 0;
      QString note;
      bool mark = false;
    };
    QElapsedTimer m_clock;
    bool m_running = false;
    QString m_what;
    std::vector<Entry> m_entries;
    std::vector<size_t> m_open;   // the stages begun and not ended, innermost last
    QStringList m_caches;
  };
}

#endif /* _LOADTIMELINE_H_ */
