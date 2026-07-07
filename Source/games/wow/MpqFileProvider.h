/*
 * MpqFileProvider.h
 *
 * Real MPQ (MoPaQ) file provider for legacy WoW clients (Vanilla 1.x .. Cataclysm 4.x), backed
 * by StormLib. Opens the Data\*.MPQ (+ locale) archive chain in override order (base archives
 * first, patches last, locale patches highest) and serves files BY NAME. Legacy clients have no
 * FileDataID, so openById always fails; name lookup probes the chain highest-priority first so
 * patch archives correctly override base archives.
 *
 * Internal to the wow module. StormLib is included only in the .cpp so windows.h/HANDLE do not
 * leak into translation units that also pull in CASCFolder.h.
 */

#ifndef _MPQFILEPROVIDER_H_
#define _MPQFILEPROVIDER_H_

#include <string>
#include <vector>

#include <QString>

#include "IFileProvider.h"

namespace wow
{
  class MpqFileProvider : public core::IFileProvider
  {
    public:
      MpqFileProvider();
      ~MpqFileProvider() override;

      // Open the legacy archive chain. dataRoot may be the WoW install folder or its Data
      // subfolder (auto-resolved). locale may be empty to auto-detect from the Data subfolders.
      // Returns the number of archives opened; logs the full ordered list, locale and result.
      int init(const QString & dataRoot, const QString & locale);

      const char * name() const override { return "MPQ"; }
      core::StorageType storageType() const override { return core::StorageType::MPQ; }
      bool supportsFileDataId() const override { return false; }
      bool supportsNameLookup() const override { return true; }

      bool openById(int fileDataId, void ** handle) override;   // always false (no FileDataID)
      bool openByName(const std::string & name, void ** handle) override;
      bool closeFile(void * handle) override;
      bool hasFile(const std::string & name) override;
      bool isReady() const override { return !m_archives.empty(); }

      QString detectedLocale() const { return m_locale; }
      // "common.MPQ; patch.MPQ; ..." in load order -- for logging / the POC report.
      QString archiveListString() const;

      // Enumerate every named file across the archive chain (via each archive's internal
      // "(listfile)"), de-duplicated and normalised to lowercase '/'. Used to populate the file
      // browser tree in the GUI (the CASC path builds its tree from the community listfile; MPQ
      // clients carry their own). Does not change how individual files are opened.
      void listAllFiles(std::vector<QString> & out) const;

    private:
      struct Archive
      {
        QString name;   // base file name, e.g. "patch-3.MPQ"
        void *  handle; // StormLib archive HANDLE
      };

      // Ordered low->high priority (base first, patches last, locale patches highest). openByName
      // probes back-to-front so higher-priority archives win.
      std::vector<Archive> m_archives;
      QString m_dataFolder;
      QString m_locale;

      // Resolve the Data folder + locale and return archive file paths in load order.
      std::vector<QString> buildArchiveList(const QString & dataRoot, const QString & locale);
      static QString mpqInternalPath(const std::string & name); // '/' -> '\'
  };
}

#endif /* _MPQFILEPROVIDER_H_ */
