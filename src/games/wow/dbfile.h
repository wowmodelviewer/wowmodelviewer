#ifndef DBFILE_H
#define DBFILE_H

#include <cassert>
#include <map>
#include <string>
#include <vector>

#include <QString>

#include "CASCFile.h"
#include "GameDatabase.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _DBFILE_API_ __declspec(dllexport)
#    else
#        define _DBFILE_API_ __declspec(dllimport)
#    endif
#else
#    define _DBFILE_API_
#endif


class _DBFILE_API_ DBFile : public CASCFile
{
public:
  explicit DBFile(const QString & file);
  virtual ~DBFile();

	// Open database. It must be openened before it can be used.
	bool open();

  // to be implemented in inherited classes to perform specific reading at open time
  virtual bool doSpecializedOpen() = 0;

  bool close();

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
		
      std::vector<std::string> get(const GameDatabase::tableStructure & structure) const
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

  float getFloat(unsigned char * recordOffset, size_t field) const
  {
    return *reinterpret_cast<float*>(recordOffset + field * 4);
  }
  unsigned int getUInt(unsigned char * recordOffset, size_t field) const
  {
    return *reinterpret_cast<unsigned int*>(recordOffset + (field * 4));
  }
  int getInt(unsigned char * recordOffset, size_t field) const
  {
    return *reinterpret_cast<int*>(recordOffset + field * 4);
  }
  unsigned char getByte(unsigned char * recordOffset, size_t ofs) const
  {
    return *reinterpret_cast<unsigned char*>(recordOffset + ofs);
  }


  std::string getStdString(unsigned char * recordOffset, size_t field) const
  {
    size_t stringOffset = getUInt(recordOffset, field);
    if (stringOffset >= stringSize)
      stringOffset = 0;

    return std::string(reinterpret_cast<char*>(stringTable + stringOffset));
  }

  // to be implemented in inherited classes to get actual record values (specified by recordOffset), following "structure" format
  virtual std::vector<std::string> get(unsigned int recordIndex, const GameDatabase::tableStructure & structure) const = 0;

protected:
	size_t recordSize;
	size_t recordCount;
	size_t fieldCount;
	size_t stringSize;
	unsigned char *data;
	unsigned char *stringTable;

  private:
  DBFile(const DBFile &);
  void operator=(const DBFile &);
};

#endif
