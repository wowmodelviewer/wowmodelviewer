#ifndef MPQ_H
#define MPQ_H

//#include "stormlib/src/StormLib.h"

#include "GameFile.h"

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


class MPQFile : public GameFile
{
	// disable copying
	MPQFile(const MPQFile &);
	void operator=(const MPQFile &);

public:
	MPQFile(): GameFile() {}
	MPQFile(wxString filename);	// filenames are not case sensitive
	void openFile(std::string filename);
	~MPQFile();

	static bool exists(wxString filename);
//	static int getSize(wxString filename); // Used to do a quick check to see if a file is corrupted
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

