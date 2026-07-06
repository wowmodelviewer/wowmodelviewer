/*
 * CascFileProvider.h
 *
 * IFileProvider backed by the existing CASCFolder / CascLib. This is the modern (default)
 * provider: it forwards straight to CASCFolder, so routing file opens through it leaves the
 * modern client's behaviour byte-for-byte identical -- it is purely a seam that names the
 * storage backend and lets an MPQ provider slot in later.
 *
 * Internal to the wow module (only WoWFolder constructs it); no DLL export needed.
 */

#ifndef _CASCFILEPROVIDER_H_
#define _CASCFILEPROVIDER_H_

#include "IFileProvider.h"

class CASCFolder;

namespace wow
{
  class CascFileProvider : public core::IFileProvider
  {
    public:
      // Does not own the CASCFolder; it is owned by the WoWFolder that creates this provider.
      explicit CascFileProvider(CASCFolder * casc) : m_casc(casc) {}
      ~CascFileProvider() override {}

      const char * name() const override { return "CASC"; }
      core::StorageType storageType() const override { return core::StorageType::CASC; }

      // CASC is addressed by FileDataID. It cannot open a file by name directly on modern
      // roots (no filename hashes), so WoWFolder resolves name -> id before calling openById.
      bool supportsFileDataId() const override { return true; }
      bool supportsNameLookup() const override { return false; }

      bool openById(int fileDataId, void ** handle) override;
      bool openByName(const std::string & name, void ** handle) override;
      bool closeFile(void * handle) override;

      bool isReady() const override { return m_casc != nullptr; }

    private:
      CASCFolder * m_casc;
  };
}

#endif /* _CASCFILEPROVIDER_H_ */
