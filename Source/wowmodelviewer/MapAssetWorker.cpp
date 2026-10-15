/*
 * MapAssetWorker.cpp
 *
 * See MapAssetWorker.h and docs/world-viewer/04b-asset-serving.md.
 */

#include "MapAssetWorker.h"

#include <chrono>
#include <new>

#include "logger/Logger.h"

namespace
{
  // How long a test hold waits at most before it lets the read go on by itself (a harness that never releases it must
  // not hang the worker for good).
  const int TEST_HOLD_LIMIT_MS = 60000;
}

MapAssetWorker::MapAssetWorker(Reader reader) : m_reader(std::move(reader))
{
  m_clock.start();
}

MapAssetWorker::~MapAssetWorker()
{
  stop();
}

QString MapAssetWorker::outcomeName(Result::Outcome outcome)
{
  switch (outcome)
  {
    case Result::Outcome::Read: return "read";
    case Result::Outcome::Cancelled: return "cancelled";
    case Result::Outcome::StorageChanged: return "storage changed";
    case Result::Outcome::NoStorage: return "no storage";
    case Result::Outcome::Stopped: return "stopped";
    case Result::Outcome::Failed: return "failed";
  }
  return "?";
}

void MapAssetWorker::ensureThread()
{
  // Started on first use, never in the constructor: the worker may log, and the logger's outputs are all added by then
  // (docs/world-viewer/04b-asset-serving.md B.1).
  if (m_threadStarted || m_stopping)
    return;
  m_threadStarted = true;
  m_thread = std::thread(&MapAssetWorker::run, this);
}

void MapAssetWorker::grantLease(Lease lease)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_stopping)
  {
    // Nobody will take it over: released here, the reference was never used.
    CASCFolder::releaseStorage(lease.storage);
    return;
  }
  Item item;
  item.kind = Item::Kind::Grant;
  item.lease = lease;
  m_items.push_back(item);
  ensureThread();
  m_changed.notify_all();
}

void MapAssetWorker::dropLease()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_stopping || !m_threadStarted)
    return;   // stop() releases it; a worker never started holds none
  Item item;
  item.kind = Item::Kind::Drop;
  m_items.push_back(item);
  m_changed.notify_all();
}

quint64 MapAssetWorker::submit(Job job)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_stopping)
    return 0;
  job.ticket = ++m_nextTicket;
  job.testHoldSerial = m_testReleaseSerial;   // a release from now on lets this job's hold go, even one given before it
  Item item;
  item.kind = Item::Kind::Job;
  item.job = job;
  item.submittedNs = m_clock.nsecsElapsed();
  m_items.push_back(item);
  m_outstanding++;
  m_stats.submitted++;
  int waiting = 0;
  for (const Item & i : m_items)
    waiting += i.kind == Item::Kind::Job ? 1 : 0;
  if (waiting > m_stats.jobsWaitingMax)
    m_stats.jobsWaitingMax = waiting;
  ensureThread();
  m_changed.notify_all();
  return job.ticket;
}

bool MapAssetWorker::takeResult(Result & out)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_results.empty())
    return false;
  out = std::move(m_results.front());
  m_results.pop_front();
  m_outstanding--;
  m_stats.heldBytes -= out.bytes.size();
  return true;
}

bool MapAssetWorker::waitResult(quint64 ticket, int ms, Result & out)
{
  std::unique_lock<std::mutex> lock(m_mutex);
  const auto has = [this, ticket]() {
    for (const Result & r : m_results)
      if (r.ticket == ticket)
        return true;
    return false;
  };
  if (!m_changed.wait_for(lock, std::chrono::milliseconds(ms), has))
    return false;
  for (auto it = m_results.begin(); it != m_results.end(); ++it)
    if (it->ticket == ticket)
    {
      out = std::move(*it);
      m_results.erase(it);
      m_outstanding--;
      m_stats.heldBytes -= out.bytes.size();
      return true;
    }
  return false;
}

bool MapAssetWorker::waitIdle(int ms)
{
  std::unique_lock<std::mutex> lock(m_mutex);
  return m_changed.wait_for(lock, std::chrono::milliseconds(ms), [this]() {
    for (const Item & i : m_items)
      if (i.kind == Item::Kind::Job)
        return false;
    return !m_busy;
  });
}

void MapAssetWorker::stop()
{
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopping && !m_thread.joinable())
      return;
    m_stopping = true;   // a test hold lets go too
    m_changed.notify_all();
  }
  if (m_thread.joinable())
    m_thread.join();
  // Nothing more runs on the worker: what it never took up gets its result here.
  std::lock_guard<std::mutex> lock(m_mutex);
  while (!m_items.empty())
  {
    Item item = m_items.front();
    m_items.pop_front();
    if (item.kind == Item::Kind::Grant)
      CASCFolder::releaseStorage(item.lease.storage);
    else if (item.kind == Item::Kind::Job)
    {
      Result r;
      r.ticket = item.job.ticket;
      r.outcome = Result::Outcome::Stopped;
      r.detail = "the map asset worker stopped before reading it";
      r.maxBytes = item.job.maxBytes;
      r.role = item.job.role;
      m_results.push_back(r);
      m_stats.stopped++;
      m_resultsMade++;
    }
  }
  m_changed.notify_all();
}

void MapAssetWorker::setResultNotifier(Notifier notifier)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_notifier = std::move(notifier);
}

bool MapAssetWorker::hasResultNotifier() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return (bool)m_notifier;
}

quint64 MapAssetWorker::resultsMade() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_resultsMade;
}

bool MapAssetWorker::waitResultsAfter(quint64 seen, int ms)
{
  std::unique_lock<std::mutex> lock(m_mutex);
  return m_changed.wait_for(lock, std::chrono::milliseconds(ms), [this, seen]() { return m_resultsMade > seen; });
}

bool MapAssetWorker::stopped() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_stopping;
}

bool MapAssetWorker::testHolding() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_testHolding;
}

quint64 MapAssetWorker::testHeldTicket() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_testHolding ? m_testHeldTicket : 0;
}

void MapAssetWorker::testReleaseHold()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_testReleaseSerial++;
  m_changed.notify_all();
}

void MapAssetWorker::wake()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_changed.notify_all();
}

bool MapAssetWorker::resultReady() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return !m_results.empty();
}

MapAssetWorker::Stats MapAssetWorker::stats() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_stats;
}

size_t MapAssetWorker::outstanding() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_outstanding;
}

bool MapAssetWorker::holdsLease() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_leaseHeld;
}

void MapAssetWorker::releaseLease()
{
  if (!m_lease.storage.open())
    return;
  // The worker's own reference: when the GUI has already let go of that storage (a client replaced), this is its last
  // one, and CascLib tears it down here, on the worker.
  CASCFolder::releaseStorage(m_lease.storage);
  m_lease = Lease();
  std::lock_guard<std::mutex> lock(m_mutex);
  m_leaseHeld = false;
  m_stats.leasesReleased++;
}

void MapAssetWorker::run()
{
  for (;;)
  {
    Item item;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_changed.wait(lock, [this]() { return m_stopping || !m_items.empty(); });
      if (m_stopping)
        break;   // stop() answers what is left
      item = m_items.front();
      m_items.pop_front();
      if (item.kind == Item::Kind::Job)
        m_busy = true;
    }
    if (item.kind == Item::Kind::Grant)
    {
      releaseLease();
      m_lease = item.lease;
      std::lock_guard<std::mutex> lock(m_mutex);
      m_leaseHeld = m_lease.storage.open();
      m_stats.leasesGranted++;
      continue;
    }
    if (item.kind == Item::Kind::Drop)
    {
      releaseLease();
      continue;
    }
    const qint64 startNs = m_clock.nsecsElapsed();
    Result r = readJob(item.job);
    const qint64 endNs = m_clock.nsecsElapsed();
    r.ticket = item.job.ticket;
    r.maxBytes = item.job.maxBytes;
    r.role = item.job.role;
    r.waitMs = (startNs - item.submittedNs) / 1e6;
    r.workMs = (endNs - startNs) / 1e6;
    std::lock_guard<std::mutex> lock(m_mutex);
    switch (r.outcome)
    {
      case Result::Outcome::Read: m_stats.read++; break;
      case Result::Outcome::Cancelled: m_stats.cancelled++; break;
      case Result::Outcome::StorageChanged: m_stats.storageChanged++; break;
      case Result::Outcome::NoStorage: m_stats.noStorage++; break;
      case Result::Outcome::Stopped: m_stats.stopped++; break;
      case Result::Outcome::Failed: m_stats.failed++; break;
    }
    m_stats.busyMs += r.workMs;
    m_stats.heldBytes += r.bytes.size();
    if (m_stats.heldBytes > m_stats.heldBytesMax)
      m_stats.heldBytesMax = m_stats.heldBytes;
    m_results.push_back(std::move(r));
    m_busy = false;
    m_resultsMade++;
    if (m_notifier)
      m_notifier();
    m_changed.notify_all();
  }
  // The lease goes with the thread, released here.
  releaseLease();
}

MapAssetWorker::Result MapAssetWorker::readJob(const Job & job)
{
  Result r;
  const auto notRead = [&r](Result::Outcome outcome, const QString & detail) {
    r.outcome = outcome;
    r.detail = detail;
    return r;
  };
  if (job.cancelled && job.cancelled->load())
    return notRead(Result::Outcome::Cancelled, "cancelled before its read began");
  // (A made-up reader -- a test's -- stands in for the whole read, the storage included.)
  if (!m_reader && !m_lease.storage.open())
    return notRead(Result::Outcome::NoStorage, "the map asset worker holds no storage of the client");
  if (!m_reader && (job.epoch != m_lease.epoch || job.generation != m_lease.generation))
    return notRead(Result::Outcome::StorageChanged,
                   QString("the client's storage changed while the request waited (decided under epoch %1, storage %2; the "
                           "worker holds epoch %3, storage %4)")
                     .arg(job.epoch).arg(job.generation).arg(m_lease.epoch).arg(m_lease.generation));
  if (job.testDelayMs > 0)
  {
    // A slow read, made deterministic: the wait ends early on stop() or the job's cancel.
    std::unique_lock<std::mutex> lock(m_mutex);
    m_changed.wait_for(lock, std::chrono::milliseconds(job.testDelayMs), [this, &job]() {
      return m_stopping || (job.cancelled && job.cancelled->load());
    });
    if (job.cancelled && job.cancelled->load())
      return notRead(Result::Outcome::Cancelled, "cancelled before its read began");
  }
  const auto hold = [this, &job]() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_testHolding = true;
    m_testHeldTicket = job.ticket;
    m_changed.notify_all();
    // Let go by a release given since the job was submitted (one given before the hold began counts), or stop().
    m_changed.wait_for(lock, std::chrono::milliseconds(TEST_HOLD_LIMIT_MS),
                       [this, &job]() { return m_stopping || m_testReleaseSerial != job.testHoldSerial; });
    m_testHolding = false;
    m_testHeldTicket = 0;
  };
  // Held before the read begins: no file open, the storage kept by the lease's reference alone.
  if (job.testHold && job.testHoldBeforeOpen)
    hold();
  CASCFolder::ReadHooks hooks;
  const bool hooked = (job.testHold && !job.testHoldBeforeOpen) || job.testCorrupt;
  if (job.testHold && !job.testHoldBeforeOpen)
    hooks.afterOpen = hold;
  if (job.testCorrupt)
    hooks.beforeVerify = [](QByteArray & bytes) {
      if (!bytes.isEmpty())
        bytes[0] = (char)(bytes[0] ^ 0x5a);
    };
  try
  {
    if (m_reader)
      r = m_reader(job, m_lease, hooked ? &hooks : nullptr);
    else
    {
      wow::WdtReference ref;
      if (job.fileDataID <= 0 && !job.path.isEmpty())
      {
        ref.kind = wow::WdtReference::Kind::Path;
        ref.path = job.path;
      }
      else
      {
        ref.kind = wow::WdtReference::Kind::FileDataId;
        ref.fileDataId = job.fileDataID;
      }
      r.inspection = wow::WorldMapCatalog::inspectInStorage(m_lease.storage, ref, &r.bytes, false, job.maxBytes,
                                                            hooked ? &hooks : nullptr);
      r.outcome = Result::Outcome::Read;
    }
  }
  catch (const std::bad_alloc &)
  {
    // An exception must not leave the thread (it would end the process): the request is answered unreadable.
    r = Result();
    return notRead(Result::Outcome::Failed, "the read ran out of memory");
  }
  catch (...)
  {
    r = Result();
    return notRead(Result::Outcome::Failed, "the read failed with an exception");
  }
  return r;
}
