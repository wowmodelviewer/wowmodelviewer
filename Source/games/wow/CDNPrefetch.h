/*
 * CDNPrefetch.h
 *
 * Cold-start accelerator for online (CDN) mode. Before CascLib can open an online storage it needs
 * the build and CDN configs, the index of every CDN archive (1,401 files, 125 MB for 12.1.0) and
 * the ENCODING manifest (187 MB). CascLib fetches them itself, but one after the other over a
 * single plain-HTTP connection: the indexes alone took 33 s that way, bound by round trips rather
 * than bandwidth. prefetchForOpen() fetches the same files over HTTPS with several requests in
 * flight, verifies each one against its name and writes it into CascLib's cache layout, where the
 * open that follows finds it. It is purely an accelerator: whatever it could not fetch, CascLib
 * still fetches on its own.
 *
 * Internal to the wow module (only CASCFolder calls it); no DLL export needed.
 */

#ifndef _CDNPREFETCH_H_
#define _CDNPREFETCH_H_

#include <functional>

#include <QString>

namespace wow
{
  namespace cdn
  {
    struct PrefetchResult
    {
      bool ok = false;             // the configs, every archive index and ENCODING are in the cache
      bool cancelled = false;      // progress() returned false
      qint64 bytesDownloaded = 0;  // everything received, including answers that were rejected
      int filesDownloaded = 0;     // verified and written
      int filesFailed = 0;         // no host delivered them; CascLib fetches them on its own
      QString error;               // empty when ok
    };

    // root: the folder holding "versions" and "cdns" (<cache>/<product>). region: which row to use.
    // progress(done, total) is in bytes and is called on the calling thread; returning false cancels.
    //
    // Blocks until done. The caller may be a worker thread without an event loop of its own: the
    // network runs in a local QEventLoop on the calling thread. Only missing files are fetched, so
    // with a warm cache this returns at once without a single request, and progress() is never
    // called. progress() comes about ten times a second, also while nothing arrives: it is how a
    // cancel gets in. While the configs are being fetched done stays 0; after that done <= total,
    // done / total never decreases, and unless cancelled the last call has done == total. Nothing
    // here is fatal: a file that fails is left to CascLib, and a partial or unverified file never
    // gets its final name.
    PrefetchResult prefetchForOpen(const QString & root, const QString & region,
                                   const std::function<bool(qint64 done, qint64 total)> & progress);
  }
}

#endif /* _CDNPREFETCH_H_ */
