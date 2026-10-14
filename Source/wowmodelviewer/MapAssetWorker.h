/*
 * MapAssetWorker.h
 *
 * THE MAP ASSET WORKER (docs/world-viewer/04b-asset-serving.md): one thread that reads the map files the Unity player
 * asks for (getMapAsset: WDT, root ADT, tex0 ADT, textures) from the loaded client's storage, so that no storage read,
 * decode or content-key check runs on the GUI thread.
 *
 * Ownership. The worker reads through its LEASE: a reference of its own to the client's CASC storage
 * (CASCFolder::shareStorage, taken on the GUI thread, which holds its own), with the client epoch and storage generation
 * it was taken for. Only the worker releases it, on its own thread, between reads, so no close by anyone else can free
 * the storage while a read uses it. Every file handle is opened, read and closed inside one read, on this thread.
 * CascLib's lookups and reads are made for several readers with handles of their own (section B of the document).
 *
 * Confinement. The worker touches nothing of the GUI's: no catalogue, no GAMEDIRECTORY, no epoch global, no wx or Qt
 * object. It is handed plain values (a Job) and hands plain values back (a Result), through one mutex. Its one call out
 * is the result notifier (setResultNotifier): the IPC server's, which only posts the server a wake (a wxThreadEvent,
 * wxEvtHandler::QueueEvent being made for other threads); the server then takes results in a poll, on the GUI thread,
 * and checks them there again before anything is sent.
 *
 * Order. Jobs and lease changes are carried out one at a time, in the order they were given; results come back in
 * that order. Bounds are the caller's (UnityIpcServer: at most MAP_ASSET_READS_IN_FLIGHT jobs not yet taken back).
 *
 * Life. The thread starts with the first job or lease. stop() cancels what waits, lets the read in progress finish (a
 * read is whole: CascLib is never asked for part of a file, section B), releases the lease and joins; every job still
 * gets its Result ("stopped"), so the caller's accounting closes.
 */

#ifndef MAPASSETWORKER_H
#define MAPASSETWORKER_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include <QByteArray>
#include <QElapsedTimer>
#include <QString>

#include "CASCFolder.h"
#include "WorldMapCatalog.h"

class MapAssetWorker
{
public:
  // What the worker reads under: one reference to the loaded client's storage, taken on the GUI thread, and the client
  // epoch and storage generation it was taken for.
  struct Lease
  {
    CASCFolder::Storage storage;
    int epoch = 0;
    quint64 generation = 0;
  };

  struct Job
  {
    quint64 ticket = 0;           // given by submit()
    int fileDataID = 0;
    qint64 maxBytes = 0;          // the read's limit (a larger file is not read: the inspection's TooLarge)
    int epoch = 0;                // the client epoch and storage generation the read was decided under: read only
    quint64 generation = 0;       // when the lease is that storage's
    QString role;                 // for the test hooks and the record
    std::shared_ptr<std::atomic<bool>> cancelled;   // set by the GUI thread; a cancelled job waiting is not read
    // TEST HOOKS (the -terraintile harness only)
    bool testHold = false;        // hold with the file open until testReleaseHold() (or stop(), or 60 s)
    bool testHoldBeforeOpen = false; // ... hold before the read begins instead: no file open, the lease's reference alone
    bool testCorrupt = false;     // change one byte read before the MD5 is compared with the content key
    int testDelayMs = 0;          // wait that long before the read (a slow read)
    quint64 testHoldSerial = 0;   // (set by submit(): the release count a hold waits to see change)
  };

  struct Result
  {
    enum class Outcome
    {
      Read,            // the inspection ran (inspection says what it found: available, not in storage, unreadable ...)
      Cancelled,       // cancelled before its read began: nothing read
      StorageChanged,  // the lease is another client's or storage's than the job's: nothing read
      NoStorage,       // the worker holds no storage: nothing read
      Stopped,         // the worker stopped before its read began: nothing read
      Failed,          // the read threw (out of memory): nothing handed back
    };
    quint64 ticket = 0;
    Outcome outcome = Outcome::Stopped;
    QString detail;               // why it was not read (every outcome but Read)
    wow::WdtInspection inspection;
    QByteArray bytes;             // the file, when the inspection found it Available
    qint64 maxBytes = 0;          // the job's limit, handed back for the caller's accounting
    QString role;
    double waitMs = -1.0;         // from submit() to the job's start on the worker
    double workMs = -1.0;         // the job on the worker (a test hold or delay included)
  };

  // The default reads the lease's storage (WorldMapCatalog::inspectInStorage); a test passes made-up reads.
  using Reader = std::function<Result(const Job & job, const Lease & lease, const CASCFolder::ReadHooks * hooks)>;
  explicit MapAssetWorker(Reader reader = Reader());
  ~MapAssetWorker();

  static QString outcomeName(Result::Outcome outcome);

  // GUI thread. grantLease: the worker reads under it from the next job on (the previous one released first, by the
  // worker); dropLease: it releases the one it holds and reads nothing until the next grant. In order with the jobs.
  void grantLease(Lease lease);
  void dropLease();
  // A job, after everything given before it; its ticket. 0 (nothing taken) once stopped.
  quint64 submit(Job job);
  // The next result, in the order the jobs were given. False when none is ready.
  bool takeResult(Result & out);
  // Stop for good: see the header comment. Idempotent; results already made stay for takeResult.
  void stop();
  bool stopped() const;

  // TESTS. Wait up to ms for every job given so far to have its result ready; wait for one ticket's result and take it
  // (out of order: the in-process checks' own reads).
  bool waitIdle(int ms);
  bool waitResult(quint64 ticket, int ms, Result & out);
  // TEST HOOKS: a read held now (Job::testHold), its ticket, and its release (it lets go the hold of every job submitted
  // before it, whether that job has reached its hold yet or not).
  bool testHolding() const;
  quint64 testHeldTicket() const;
  void testReleaseHold();
  // Wakes the worker's waits (a test delay) to look at the jobs' cancel flags again: the caller has just set some.
  void wake();
  // A result is ready to be taken.
  bool resultReady() const;

  // THE WAKE. Called on the worker thread each time it has made a result, under the worker's lock: it must be quick and
  // safe from any thread, and must not call the worker. Set and cleared on the GUI thread; once setResultNotifier()
  // returns, the previous notifier is not running and will not be called again.
  using Notifier = std::function<void()>;
  void setResultNotifier(Notifier notifier);
  bool hasResultNotifier() const;
  // Results made so far (a count); a wait until more than seen were made (true), or ms passed (false).
  quint64 resultsMade() const;
  bool waitResultsAfter(quint64 seen, int ms);

  // Accounting, at any time.
  struct Stats
  {
    long long submitted = 0;
    long long read = 0;            // Outcome::Read
    long long cancelled = 0;
    long long storageChanged = 0;
    long long noStorage = 0;
    long long stopped = 0;
    long long failed = 0;
    long long leasesGranted = 0;   // taken over by the worker
    long long leasesReleased = 0;  // released by the worker (CascCloseStorage on its reference)
    int jobsWaitingMax = 0;        // jobs given and not yet started, the most at once
    long long heldBytes = 0;       // bytes of results made and not taken back, now
    long long heldBytesMax = 0;    // ... the most at once
    double busyMs = 0.0;           // time spent on jobs
  };
  Stats stats() const;
  // Jobs given whose result has not been taken back: waiting, being read, or ready.
  size_t outstanding() const;
  bool holdsLease() const;

private:
  struct Item
  {
    enum class Kind { Job, Grant, Drop } kind = Kind::Job;
    Job job;
    Lease lease;
    qint64 submittedNs = 0;
  };
  void ensureThread();   // under m_mutex
  void run();
  Result readJob(const Job & job);
  void releaseLease();   // worker thread

  Reader m_reader;
  mutable std::mutex m_mutex;
  std::condition_variable m_changed;
  std::deque<Item> m_items;
  std::deque<Result> m_results;
  std::thread m_thread;
  bool m_stopping = false;
  bool m_threadStarted = false;
  bool m_busy = false;                // a job is being read now
  quint64 m_nextTicket = 0;
  size_t m_outstanding = 0;           // jobs given, result not taken
  quint64 m_resultsMade = 0;          // results made, ever
  Notifier m_notifier;                // called under m_mutex
  QElapsedTimer m_clock;
  Stats m_stats;
  // Worker thread only (its own lease); m_leaseHeld mirrors it for holdsLease().
  Lease m_lease;
  bool m_leaseHeld = false;
  // TEST HOOKS (under m_mutex)
  bool m_testHolding = false;
  quint64 m_testHeldTicket = 0;
  quint64 m_testReleaseSerial = 0;   // testReleaseHold() calls so far
};

#endif // MAPASSETWORKER_H
