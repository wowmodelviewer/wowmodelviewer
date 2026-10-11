// casc_extract -- read-only probe for the world-format audit (docs/world-viewer/00-audit.md).
//
// Copies files out of a locally installed WoW client's CASC storage by FileDataID, for the Python parsers in this
// folder. It never writes to the game folder and is not part of the viewer or its installer.
//
//   casc_extract <Data folder> <product> <output folder> <keys.csv or -> <id list>
//
// <product> is the .build.info product code (wow, wowt, wow_classic, wow_classic_era, wow_classic_beta).
// The id list has one FileDataID per line, optionally followed by a byte limit (only that many bytes are kept,
// for headers). A line "n <path>" opens a file by its name instead (saved as name_<n>.bin, listed as name:<path>);
// a line "s <id>" only records the file's status and size (nothing written).
// Lines starting with # are ignored. Each file goes to <output>/<id>.bin, and <output>/manifest.tsv
// gets one line per id:  id <TAB> status <TAB> bytes <TAB> detail
// where status is OK, NOT_IN_ROOT (the client's root does not list it), NOT_LOCAL (listed, but its data is not on
// this computer), ENCRYPTED_PARTIAL (some blocks are under a key nobody has: written with those blocks as zeros),
// ENCRYPTED (no key, nothing readable) or READ_ERROR (anything else; detail has the Windows error number).
// The listed/local decision for FileDataIDs comes from one enumeration of the storage (counts in <output>/index.txt).
// <output>/timing.txt gets one line per run: milliseconds to open the storage, to enumerate it, and to read the list
// (files and bytes read), so read throughput can be told apart from the start-up cost.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#define CASCLIB_NO_AUTO_LINK_LIBRARY
#include "CascLib.h"

namespace
{
  std::wstring widen(const char * s)
  {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n > 1)
      MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return w;
  }

  const char * statusOf(DWORD error)
  {
    switch (error)
    {
      case ERROR_FILE_NOT_FOUND: return "NOT_IN_ROOT";
      case ERROR_FILE_OFFLINE: return "NOT_LOCAL";
      case ERROR_FILE_ENCRYPTED: return "ENCRYPTED";
      default: return "READ_ERROR";
    }
  }

  // "<key name hex>;<key hex>" lines, as the viewer's extraEncryptionKeys.csv has them.
  int addKeys(HANDLE storage, const char * path)
  {
    std::ifstream in(path);
    std::string line;
    int added = 0;
    while (std::getline(in, line))
    {
      const size_t sep = line.find(';');
      if (sep == std::string::npos)
        continue;
      const unsigned long long name = std::strtoull(line.substr(0, sep).c_str(), nullptr, 16);
      std::string key = line.substr(sep + 1);
      while (!key.empty() && (key.back() == '\r' || key.back() == ' '))
        key.pop_back();
      if (CascAddStringEncryptionKey(storage, name, key.c_str()))
        added++;
    }
    return added;
  }

  double millisecondsSince(const LARGE_INTEGER & start)
  {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return 1000.0 * static_cast<double>(now.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart);
  }
}

int main(int argc, char ** argv)
{
  if (argc != 6)
  {
    std::fprintf(stderr, "usage: casc_extract <Data folder> <product> <output folder> <keys.csv or -> <id list>\n");
    return 2;
  }
  const std::wstring storagePath = widen(argv[1]) + L"*" + widen(argv[2]);
  const std::string outDir = argv[3];
  CreateDirectoryA(outDir.c_str(), nullptr);

  LARGE_INTEGER started;
  QueryPerformanceCounter(&started);
  CASC_OPEN_STORAGE_ARGS args = {sizeof(CASC_OPEN_STORAGE_ARGS)};
  args.dwLocaleMask = CASC_LOCALE_ENUS;
  HANDLE storage = nullptr;
  if (!CascOpenStorageEx(storagePath.c_str(), &args, false, &storage))
  {
    std::fprintf(stderr, "cannot open storage %ls: error %lu\n", storagePath.c_str(), GetLastError());
    return 1;
  }
  if (std::string(argv[4]) != "-")
    std::fprintf(stderr, "encryption keys added: %d\n", addKeys(storage, argv[4]));
  const double openMs = millisecondsSince(started);

  // Which FileDataIDs the root lists, and which of them have a copy on this computer -- as the viewer's
  // CASCFolder::buildPresentIdIndex decides it (bFileAvailable on any locale entry). CascLib opens a listed file that
  // is not on this computer as an empty file, so this, not the open, is what tells NOT_LOCAL apart.
  std::vector<bool> listed, present;
  size_t listedCount = 0, presentCount = 0;
  {
    CASC_FIND_DATA fd;
    HANDLE find = CascFindFirstFile(storage, "*", &fd, nullptr);
    if (find && find != INVALID_HANDLE_VALUE)
    {
      do
      {
        if (fd.dwFileDataId == CASC_INVALID_ID)
          continue;
        const size_t id = fd.dwFileDataId;
        if (id >= listed.size())
        {
          listed.resize(id + 1 + id / 2);
          present.resize(id + 1 + id / 2);
        }
        if (!listed[id])
        {
          listed[id] = true;
          listedCount++;
        }
        if (fd.bFileAvailable && !present[id])
        {
          present[id] = true;
          presentCount++;
        }
      } while (CascFindNextFile(find, &fd));
      CascFindClose(find);
    }
  }
  std::ofstream(outDir + "\\index.txt", std::ios::app)
    << argv[2] << ": " << listedCount << " FileDataIDs listed by the root, " << presentCount << " on this computer, "
    << (listedCount - presentCount) << " listed but not on this computer\n";
  const double indexMs = millisecondsSince(started) - openMs;
  LARGE_INTEGER readStarted;
  QueryPerformanceCounter(&readStarted);
  unsigned long long bytesRead = 0;
  auto isListed = [&](unsigned long id) { return id < listed.size() && listed[id]; };
  auto isPresent = [&](unsigned long id) { return id < present.size() && present[id]; };

  std::ifstream list(argv[5]);
  std::ofstream manifest(outDir + "\\manifest.tsv", std::ios::app);
  std::string line;
  int ok = 0, failed = 0;
  while (std::getline(list, line))
  {
    if (line.empty() || line[0] == '#')
      continue;
    unsigned long id = 0;
    unsigned long long limit = 0;
    std::string name;
    // "s <id>": status only -- classified, opened and its first byte read, nothing written (availability census).
    const bool statusOnly = line.compare(0, 2, "s ") == 0;
    if (statusOnly)
      line = line.substr(2);
    if (line.compare(0, 2, "n ") == 0)
    {
      // "n <path>": opened by its name, as a client whose root lists names finds it. The file is saved as
      // name_<n>.bin (<n> = its line in the list) and the manifest's first column is "name:<path>".
      name = line.substr(2);
      while (!name.empty() && (name.back() == '\r' || name.back() == ' '))
        name.pop_back();
    }
    else
    {
      std::istringstream(line) >> id >> limit;
      if (!id)
        continue;
    }
    static int named = 0;
    const std::string key = name.empty() ? std::to_string(id) : "name:" + name;
    const std::string outName = name.empty() ? std::to_string(id) : "name_" + std::to_string(++named);
    if (name.empty() && !listed.empty())
    {
      if (!isListed(id))
      {
        manifest << key << "\tNOT_IN_ROOT\t0\tnot listed by the root\n";
        failed++;
        continue;
      }
      if (!isPresent(id))
      {
        manifest << key << "\tNOT_LOCAL\t0\tlisted by the root, no copy on this computer\n";
        failed++;
        continue;
      }
    }
    HANDLE file = nullptr;
    const bool opened = name.empty()
      ? CascOpenFile(storage, CASC_FILE_DATA_ID(id), CASC_LOCALE_ENUS, CASC_OPEN_BY_FILEID, &file)
      : CascOpenFile(storage, name.c_str(), CASC_LOCALE_ENUS, CASC_OPEN_BY_NAME, &file);
    if (!opened)
    {
      const DWORD error = GetLastError();
      manifest << key << '\t' << statusOf(error) << "\t0\topen error " << error << '\n';
      failed++;
      continue;
    }
    ULONGLONG size = 0;
    CascGetFileSize64(file, &size);
    if (statusOnly)
    {
      char probe = 0;
      DWORD got = 0;
      const bool readable = size == 0 || (CascReadFile(file, &probe, 1, &got) && got == 1);
      const DWORD error = readable ? ERROR_SUCCESS : GetLastError();
      CascCloseFile(file);
      manifest << key << '\t' << (readable ? "OK" : statusOf(error)) << '\t' << size << "\tstatus only\n";
      (readable ? ok : failed)++;
      continue;
    }
    const ULONGLONG want = limit && limit < size ? limit : size;
    std::vector<char> data(static_cast<size_t>(want));
    DWORD read = 0, total = 0;
    bool good = true;
    while (total < want)
    {
      if (!CascReadFile(file, data.data() + total, static_cast<DWORD>(want - total), &read) || !read)
      {
        good = false;
        break;
      }
      total += read;
    }
    DWORD error = good ? ERROR_SUCCESS : GetLastError();
    CascCloseFile(file);
    bool partial = false;
    if (!good && error == ERROR_FILE_ENCRYPTED &&
        (name.empty() ? CascOpenFile(storage, CASC_FILE_DATA_ID(id), CASC_LOCALE_ENUS, CASC_OPEN_BY_FILEID | CASC_OVERCOME_ENCRYPTED, &file)
                      : CascOpenFile(storage, name.c_str(), CASC_LOCALE_ENUS, CASC_OPEN_BY_NAME | CASC_OVERCOME_ENCRYPTED, &file)))
    {
      // Blocks under a key nobody has are read as zeros: kept, and marked, so a parser knows which parts are real.
      total = 0;
      good = true;
      while (total < want)
      {
        if (!CascReadFile(file, data.data() + total, static_cast<DWORD>(want - total), &read) || !read)
        {
          good = false;
          break;
        }
        total += read;
      }
      error = good ? ERROR_SUCCESS : GetLastError();
      partial = good;
      CascCloseFile(file);
    }
    if (!good)
    {
      manifest << key << '\t' << statusOf(error) << '\t' << total << "\tread error " << error << " of " << size << '\n';
      failed++;
      continue;
    }
    bytesRead += total;
    std::ofstream out(outDir + "\\" + outName + ".bin", std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(total));
    manifest << key << (partial ? "\tENCRYPTED_PARTIAL\t" : "\tOK\t") << total << '\t'
             << (want < size ? "first bytes of " + std::to_string(size) : std::string("whole"))
             << (partial ? " (blocks without a key read as zeros)" : "") << '\n';
    ok++;
  }
  const double readMs = millisecondsSince(readStarted);
  CascCloseStorage(storage);
  char timing[256];
  std::snprintf(timing, sizeof(timing), "%s: open %.0f ms, index %.0f ms, read %.0f ms (%d files, %llu bytes)\n", argv[2],
                openMs, indexMs, readMs, ok, bytesRead);
  std::ofstream(outDir + "\\timing.txt", std::ios::app) << timing;
  std::fprintf(stderr, "%d read, %d not read\n%s", ok, failed, timing);
  return 0;
}
