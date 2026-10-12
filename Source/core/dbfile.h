#ifndef DBFILE_H
#define DBFILE_H

#include <map>
#include <string>
#include <vector>

#include "GameDatabase.h"

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _DBFILE_API_ __declspec(dllexport)
#    else
#        define _DBFILE_API_ __declspec(dllimport)
#    endif
#else
#    define _DBFILE_API_
#endif


class _DBFILE_API_ DBFile
{
public:
  explicit DBFile();
  virtual ~DBFile() {};

  virtual bool open() = 0;

  virtual bool close() = 0;

  // Iteration over database
  class Iterator
  {
    public:
    Iterator(DBFile &f, unsigned int index) :
      file(f), recordIndex(index) {}
      
      /// Advance (prefix only)
      Iterator & operator++() 
      { 
        recordIndex++;
        return *this; 
      }  
    
      std::vector<std::string> get(const core::TableStructure * structure) const
      {
        return file.get(recordIndex, structure);
      }
  
      /// Comparison
      bool operator==(const Iterator &b) const
      {
        return recordIndex == b.recordIndex;
      }
    
      bool operator!=(const Iterator &b) const
      {
        return recordIndex != b.recordIndex;
      }
    
    private:
      DBFile &file;
      unsigned int recordIndex;
  };

  /// Get begin iterator over records
  Iterator begin();
  /// Get begin iterator over records
  Iterator end();

  /// Trivial
  size_t getRecordCount() const { return recordCount; }
  // Records the file holds in sections encrypted with a key this computer does not have: the storage reads them as
  // zeros, so they are left out of the records above (never read as data). 0 for formats without sections.
  size_t getLockedRecordCount() const { return lockedRecordCount; }
  // The names of the keys those sections are encrypted with (a key this computer gains later makes them readable).
  const std::vector<unsigned long long> & getLockedKeyNames() const { return lockedKeyNames; }

  // to be implemented in inherited classes to get actual record values (specified by recordOffset), following "structure" format
  virtual std::vector<std::string> get(unsigned int recordIndex, const core::TableStructure * structure) const = 0;

protected:
  size_t recordSize;
  size_t recordCount;
  size_t fieldCount;
  size_t stringSize;
  size_t lockedRecordCount = 0;
  std::vector<unsigned long long> lockedKeyNames;
  unsigned char *data;
  unsigned char *stringTable;

  private:
  DBFile(const DBFile &);
  void operator=(const DBFile &);
};

#endif
