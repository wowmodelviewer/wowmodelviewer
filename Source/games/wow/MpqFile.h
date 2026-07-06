/*
 * MpqFile.h
 *
 * GameFile backed by a StormLib MPQ file handle, for legacy (Vanilla/TBC/WotLK/Cataclysm)
 * clients. Mirrors CASCFile exactly, but the handle is opened through the active file provider
 * (WoWFolder -> MpqFileProvider) and size/read/close use StormLib instead of CascLib. Old MPQ
 * clients address files BY NAME only (no FileDataID), so this is always a name-based file.
 *
 * Internal to the wow module.
 */

#ifndef _MPQFILE_H_
#define _MPQFILE_H_

#include "GameFile.h"

namespace wow
{
  class MpqFile : public GameFile
  {
    public:
      explicit MpqFile(QString path);
      ~MpqFile() override;

    protected:
      bool openFile() override;
      bool isAlreadyOpened() override;
      bool getFileSize(unsigned long long & s) override;
      unsigned long readFile() override;
      void doPostOpenOperation() override;
      bool doPostCloseOperation() override;

    private:
      void * m_handle; // StormLib file handle (HANDLE); opened via the provider
  };
}

#endif /* _MPQFILE_H_ */
