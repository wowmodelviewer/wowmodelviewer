/*
 * IFileProvider.h
 *
 * Abstraction over the storage backend that actually turns a file reference into an open
 * handle. Today there is exactly one real backend (CASC); this interface exists so an MPQ
 * backend for old clients can be added later without the rest of the code knowing which
 * archive format it is talking to.
 *
 * The handle type is deliberately opaque (void*), matching the existing GameFolder::openFile
 * surface: for CASC it is a CascLib HANDLE, for a future MPQ backend it would be a StormLib
 * handle. Callers pass it straight back to the backend that produced it.
 *
 * Pure interface, header-only: it has no out-of-line symbols, so it needs no DLL export and
 * no .cpp. Concrete providers live in the game module that owns their archive library.
 */

#ifndef _IFILEPROVIDER_H_
#define _IFILEPROVIDER_H_

#include <string>

#include "ClientProfile.h" // core::StorageType

namespace core
{
  class IFileProvider
  {
    public:
      virtual ~IFileProvider() {}

      // Short identifier for logging, e.g. "CASC" or "MPQ".
      virtual const char * name() const = 0;

      // Which storage backend this provider talks to.
      virtual StorageType storageType() const = 0;

      // Capability queries: which addressing modes this backend natively supports.
      virtual bool supportsFileDataId() const = 0;
      virtual bool supportsNameLookup() const = 0;

      // Open a file by numeric FileDataID. Returns true and fills *handle on success.
      // Providers that do not support id lookup return false.
      virtual bool openById(int fileDataId, void ** handle) = 0;

      // Open a file by name/path. Returns true and fills *handle on success.
      // Providers that do not support name lookup return false.
      virtual bool openByName(const std::string & name, void ** handle) = 0;

      // Close a handle previously returned by openById/openByName. Returns true on success.
      virtual bool closeFile(void * handle) = 0;

      // True if a file with this name/path exists in the backend. Used to populate the file tree
      // on demand for name-addressed storage (MPQ). Providers without name lookup return false.
      virtual bool hasFile(const std::string & name) = 0;

      // True once the backend has been opened and is ready to serve files.
      virtual bool isReady() const = 0;
  };
}

#endif /* _IFILEPROVIDER_H_ */
