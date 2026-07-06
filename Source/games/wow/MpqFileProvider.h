/*
 * MpqFileProvider.h
 *
 * PLACEHOLDER provider for classic MoPaQ (.MPQ) archives used by old clients
 * (Vanilla 1.x .. Cataclysm 4.x). MPQ reading is NOT implemented yet -- integrating a MoPaQ
 * library (e.g. StormLib) is a later milestone. This class exists so the versioned
 * file-provider abstraction is complete and so selecting an old client fails loudly and
 * clearly (a logged "not implemented" rather than a silent CASC failure) instead of
 * pretending to work.
 *
 * Internal to the wow module; no DLL export needed.
 */

#ifndef _MPQFILEPROVIDER_H_
#define _MPQFILEPROVIDER_H_

#include "IFileProvider.h"

namespace wow
{
  class MpqFileProvider : public core::IFileProvider
  {
    public:
      MpqFileProvider() {}
      ~MpqFileProvider() override {}

      const char * name() const override { return "MPQ"; }
      core::StorageType storageType() const override { return core::StorageType::MPQ; }

      // MPQ archives are addressed by file name/path, not by FileDataID.
      bool supportsFileDataId() const override { return false; }
      bool supportsNameLookup() const override { return true; }

      // Not implemented yet -- both log once and return false.
      bool openById(int fileDataId, void ** handle) override;
      bool openByName(const std::string & name, void ** handle) override;
      bool closeFile(void * handle) override; // no-op: nothing is ever opened

      // Never ready until a MoPaQ backend is integrated.
      bool isReady() const override { return false; }

    private:
      void warnOnce() const;
  };
}

#endif /* _MPQFILEPROVIDER_H_ */
