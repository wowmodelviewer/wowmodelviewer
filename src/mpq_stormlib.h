#ifndef MPQ_H
#define MPQ_H

#include "stormlib/src/StormLib.h"

// C++ files
#include <string>
#include <set>
#include <vector>
#include <wx/wx.h>

struct FileTreeItem {
    wxString displayName;
	wxString fileName;
	int color;

	/// Comparison
	bool operator<(const FileTreeItem &i) const {
		return displayName < i.displayName;
	}

	bool operator>(const FileTreeItem &i) const {
		return displayName < i.displayName;
	}
};


class MPQArchive
{
	//MPQHANDLE handle;
	HANDLE mpq_a;
	bool ok;
public:
	MPQArchive(wxString filename);
	~MPQArchive();
	bool isPartialMPQ(wxString filename);

	void close();
};


class MPQFile
{
	//MPQHANDLE handle;
	bool eof;
	unsigned char *buffer;
	size_t pointer, size;

	// disable copying
	MPQFile(const MPQFile &f) {}
	void operator=(const MPQFile &f) {}

public:
	MPQFile():eof(false),buffer(0),pointer(0),size(0) {}
	MPQFile(wxString filename);	// filenames are not case sensitive
	void openFile(wxString filename);
	~MPQFile();
	size_t read(void* dest, size_t bytes);
	size_t getSize();
	size_t getPos();
	unsigned char* getBuffer();
	unsigned char* getPointer();
	bool isEof();
	void seek(ssize_t offset);
	void seekRelative(ssize_t offset);
	void close();
	void save(wxString filename);

	static bool exists(wxString filename);
	static int getSize(wxString filename); // Used to do a quick check to see if a file is corrupted
	static wxString getArchive(wxString filename);
	bool isPartialMPQ(wxString filename);
};

inline void flipcc(char *fcc)
{
	char t;
	t=fcc[0];
	fcc[0]=fcc[3];
	fcc[3]=t;
	t=fcc[1];
	fcc[1]=fcc[2];
	fcc[2]=t;
}

inline bool defaultFilterFunc(wxString) { return true; }
void getFileLists(std::set<FileTreeItem> &dest, bool filterfunc(wxString) = defaultFilterFunc);


#endif

