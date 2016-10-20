#ifndef DBFILE_H
#define DBFILE_H

#include <cassert>
#include <map>
#include <string>
#include <vector>

#include <QString>

#include "CASCFile.h"

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
  ~DBFile();

	// Open database. It must be openened before it can be used.
	bool open();
  bool close();

	// TODO: Add a close function?

	// Iteration over database
	class Iterator;
	class Record
	{
	public:
		Record& operator= (const Record& r)
		{
            file = r.file;
			offset = r.offset;
			return *this;
		}
		float getFloat(size_t field) const
		{
			assert(field < file.fieldCount);
			return *reinterpret_cast<float*>(offset+field*4);
		}
		unsigned int getUInt(size_t field) const
		{
			assert(field < file.fieldCount);
			return *reinterpret_cast<unsigned int*>(offset+(field*4));
		}
		int getInt(size_t field) const
		{
			assert(field < file.fieldCount);
			return *reinterpret_cast<int*>(offset+field*4);
		}
		unsigned char getByte(size_t ofs) const
		{
			assert(ofs < file.recordSize);
			return *reinterpret_cast<unsigned char*>(offset+ofs);
		}


		std::string getStdString(size_t field) const
		{
		  assert(field < file.fieldCount);
		  size_t stringOffset = getUInt(field);
		  if (stringOffset >= file.stringSize)
		    stringOffset = 0;
		  assert(stringOffset < file.stringSize);

		  return std::string(reinterpret_cast<char*>(file.stringTable + stringOffset));
		}

		std::vector<std::string> get(const std::map<int, std::pair<QString, QString> > & structure) const;


	private:
  DBFile &file;
		unsigned char *offset;
    Record(DBFile &file, unsigned char *offset) : file(file), offset(offset) {}

    friend class DBFile;
		friend class Iterator;
	};

	/* Iterator that iterates over records */
	class Iterator
	{
	public:
  Iterator(DBFile &file, unsigned char *offset) :
			record(file, offset) {}
		/// Advance (prefix only)
		Iterator & operator++() { 
			record.offset += record.file.recordSize;
			return *this; 
		}	
		/// Return address of current instance
		Record const & operator*() const { return record; }
		const Record* operator->() const {
			return &record;
		}
		/// Comparison
		bool operator==(const Iterator &b) const
		{
			return record.offset == b.record.offset;
		}
		bool operator!=(const Iterator &b) const
		{
			return record.offset != b.record.offset;
		}
	private:
		Record record;
	};

	/// Get begin iterator over records
	Iterator begin();
	/// Get begin iterator over records
	Iterator end();

	/// Trivial
	size_t getRecordCount() const { return recordCount; }

private:
	size_t recordSize;
	size_t recordCount;
	size_t fieldCount;
	size_t stringSize;
	unsigned char *data;
	unsigned char *stringTable;

  DBFile(const DBFile &);
  void operator=(const DBFile &);
};

#endif
