#include "mpq.h"

#include "globalvars.h"
#include "modelviewer.h"
#include "util.h"

#include <string>
#include <vector>

#include <wx/log.h>
#include <wx/file.h>


using namespace std;

typedef vector< pair< wxString, HANDLE* > > ArchiveSet;
static ArchiveSet gOpenArchives;

MPQArchive::MPQArchive(wxString filename) : ok(false)
{
	wxLogMessage(wxT("Opening %s %s"), filename.Mid(gamePath.Len()).c_str(), isPartialMPQ(filename) ? "(Partial)" : "");
	g_modelViewer->SetStatusText(wxT("Initiating "+filename+wxT(" Archive")));
#ifndef _MINGW
	if (!SFileOpenArchive(filename.fn_str(), 0, MPQ_OPEN_FORCE_MPQ_V1|MPQ_OPEN_READ_ONLY, &mpq_a )) {
#else
	if (!SFileOpenArchive(filename.char_str(), 0, MPQ_OPEN_FORCE_MPQ_V1|MPQ_OPEN_READ_ONLY, &mpq_a )) {
#endif
		int nError = GetLastError();
		wxLogMessage(wxT("Error opening archive %s, error #: 0x%X"), filename.Mid(gamePath.Len()).c_str(), nError);
		return;
	}

	
	// do patch, but skip cache\ directory
	if (!(filename.BeforeLast(SLASH).Lower().Contains(wxT("cache")) && 
		filename.AfterLast(SLASH).Lower().StartsWith(wxT("patch"))) &&
		!isPartialMPQ(filename)) { // skip the PTCH files atrchives
		// do patch
		for(ssize_t j=mpqArchives.GetCount()-1; j>=0; j--) {
			if (!mpqArchives[j].AfterLast(SLASH).StartsWith(wxT("wow-update-")))
				continue;
			if (mpqArchives[j].AfterLast(SLASH).Len() == strlen("wow-update-xxxxx.mpq")) {
#ifndef _MINGW
				SFileOpenPatchArchive(mpq_a, mpqArchives[j].fn_str(), "base", 0);
				SFileOpenPatchArchive(mpq_a, mpqArchives[j].fn_str(), langName.fn_str(), 0);
#else
				SFileOpenPatchArchive(mpq_a, mpqArchives[j].char_str(), "base", 0);
				SFileOpenPatchArchive(mpq_a, mpqArchives[j].char_str(), langName.char_str(), 0);
#endif
				// too many for ptr client, just comment it
				// wxLogMessage(wxT("Appending base & %s patch %s"), langName.c_str(), mpqArchives[j].Mid(gamePath.Len()).c_str());
			} else if (mpqArchives[j].BeforeLast(SLASH) == filename.BeforeLast(SLASH)) { // same directory only
#ifndef _MINGW
				SFileOpenPatchArchive(mpq_a, mpqArchives[j].fn_str(), "", 0);
#else
				SFileOpenPatchArchive(mpq_a, mpqArchives[j].char_str(), "", 0);
#endif
				// wxLogMessage(wxT("Appending patch %s"), mpqArchives[j].Mid(gamePath.Len()).c_str());
			}
		}
	}

	ok = true;
	gOpenArchives.push_back( make_pair( filename, &mpq_a ) );
}

MPQArchive::~MPQArchive()
{
	/*
	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		mpq_archive &mpq_a = **i;
		
		free(mpq_a.header);
	}
	*/
	//gOpenArchives.erase(gOpenArchives.begin(), gOpenArchives.end());
}

bool MPQArchive::isPartialMPQ(wxString filename)
{
	if (filename.AfterLast(SLASH).StartsWith(wxT("wow-update-")))
		return true;
	return false;
}

void MPQArchive::close()
{
	if (ok == false)
		return;
	SFileCloseArchive(mpq_a);
	for(ArchiveSet::iterator it=gOpenArchives.begin(); it!=gOpenArchives.end();++it)
	{
		HANDLE &mpq_b = *it->second;
		if (&mpq_b == &mpq_a) {
			gOpenArchives.erase(it);
			//delete (*it);
			return;
		}
	}
	
}

bool MPQFile::isPartialMPQ(wxString filename)
{
	if (filename.AfterLast(SLASH).StartsWith(wxT("wow-update-")))
		return true;
	return false;
}

void
MPQFile::openFile(wxString filename)
{
	eof = false;
	buffer = 0;
	pointer = 0;
	size = 0;
	if( useLocalFiles ) {
		wxString fn1 = wxGetCwd()+SLASH+wxT("Import")+SLASH;
		wxString fn2 = fn1;
		wxString fn3 = gamePath;
		fn1.Append(filename);
		fn2.Append(filename.AfterLast(SLASH));
		fn3.Append(filename);

		wxString fns[] = { fn1, fn2, fn3 };
		for(size_t i=0; i<WXSIZEOF(fns); i++) {
			wxString fn = fns[i];
			if (wxFile::Exists(fn)) {
				// success
				wxFile file;
				// if successfully opened
				if (file.Open(fn, wxFile::read)) {
					size = file.Length();
					if (size > 0) {
						buffer = new unsigned char[size];
						// if successfully read data
						if (file.Read(buffer, size) > 0) {
							eof = false;
							file.Close();
							return;
						} else {
							wxDELETEA(buffer);
							eof = true;
							size = 0;
						}
					}
					file.Close();
				}
			}
		}
	}

	// zhCN alternate file mode
	if (bAlternate && !filename.IsEmpty() && !filename.Lower().StartsWith(wxT("alternate"))) {
		wxString alterName = wxString("alternate")+SLASH+filename;

		for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end(); ++i)
		{
			HANDLE &mpq_a = *i->second;

			HANDLE fh;
#ifndef _MINGW
			if( !SFileOpenFileEx( mpq_a, alterName.fn_str(), SFILE_OPEN_PATCHED_FILE, &fh ) )
#else
			if( !SFileOpenFileEx( mpq_a, alterName.char_str(), SFILE_OPEN_PATCHED_FILE, &fh ) )
#endif
				continue;

			// Found!
			DWORD filesize = SFileGetFileSize( fh );
			size = filesize;

			// HACK: in patch.mpq some files don't want to open and give 1 for filesize
			if (size<=1) {
				eof = true;
				buffer = 0;
				return;
			}

			buffer = new unsigned char[size];
			SFileReadFile( fh, buffer, (DWORD)size );
			SFileCloseFile( fh );

			return;
		}
	}

	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end(); ++i)
	{
		HANDLE &mpq_a = *i->second;

		HANDLE fh;
#ifndef _MINGW
		if( !SFileOpenFileEx( mpq_a, filename.fn_str(), SFILE_OPEN_PATCHED_FILE, &fh ) )
#else
		if( !SFileOpenFileEx( mpq_a, filename.char_str(), SFILE_OPEN_PATCHED_FILE, &fh ) )
#endif
			continue;

		// Found!
		DWORD filesize = SFileGetFileSize( fh );
		size = filesize;

		// HACK: in patch.mpq some files don't want to open and give 1 for filesize
		if (size<=1) {
			eof = true;
			buffer = 0;
			return;
		}

		buffer = new unsigned char[size];
		SFileReadFile( fh, buffer, (DWORD)size );
		SFileCloseFile( fh );

		return;
	}

	eof = true;
	buffer = 0;
}

MPQFile::MPQFile(wxString filename):
	eof(false),
	buffer(0),
	pointer(0),
	size(0)
{
	openFile(filename);
}

MPQFile::~MPQFile()
{
	close();
}

bool MPQFile::exists(wxString filename)
{
	if( useLocalFiles ) {
		wxString fn1 = wxGetCwd()+SLASH+wxT("Import")+SLASH;
		wxString fn2 = fn1;
		wxString fn3 = gamePath;
		fn1.Append(filename);
		fn2.Append(filename.AfterLast(SLASH));
		fn3.Append(filename);

		wxString fns[] = { fn1, fn2, fn3 };
		for(size_t i=0; i<WXSIZEOF(fns); i++) {
			wxString fn = fns[i];
			if (wxFile::Exists(fn))
				return true;
		}
	}

	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		HANDLE &mpq_a = *i->second;
#ifndef _MINGW
		if( SFileHasFile( mpq_a, filename.fn_str() ) )
#else
		if( SFileHasFile( mpq_a, filename.char_str() ) )
#endif
			return true;
	}

	return false;
}

void MPQFile::save(wxString filename)
{
	wxFile f;
	f.Open(filename, wxFile::write);
	f.Write(buffer, size);
	f.Close();
}

size_t MPQFile::read(void* dest, size_t bytes)
{
	if (eof) 
		return 0;

	size_t rpos = pointer + bytes;
	if (rpos > size) {
		bytes = size - pointer;
		eof = true;
	}

	memcpy(dest, &(buffer[pointer]), bytes);

	pointer = rpos;

	return bytes;
}

bool MPQFile::isEof()
{
    return eof;
}

void MPQFile::seek(ssize_t offset) {
	pointer = offset;
	eof = (pointer >= size);
}

void MPQFile::seekRelative(ssize_t offset)
{
	pointer += offset;
	eof = (pointer >= size);
}

void MPQFile::close()
{
	wxDELETEA(buffer);
	eof = true;
}

size_t MPQFile::getSize()
{
	return size;
}

int MPQFile::getSize(wxString filename)
{
	if( useLocalFiles ) {
		wxString fn1 = wxGetCwd()+SLASH+wxT("Import")+SLASH;
		wxString fn2 = fn1;
		wxString fn3 = gamePath;
		fn1.Append(filename);
		fn2.Append(filename.AfterLast(SLASH));
		fn3.Append(filename);

		wxString fns[] = { fn1, fn2, fn3 };
		for(size_t i=0; i<WXSIZEOF(fns); i++) {
			wxString fn = fns[i];
			if (wxFile::Exists(fn)) {
				wxFile file(fn);
				return file.Length();
			}
		}
	}

	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		HANDLE &mpq_a = *i->second;
		HANDLE fh;
#ifndef _MINGW
		if( !SFileOpenFileEx( mpq_a, filename.fn_str(), SFILE_OPEN_PATCHED_FILE, &fh ) )
#else
		if( !SFileOpenFileEx( mpq_a, filename.char_str(), SFILE_OPEN_PATCHED_FILE, &fh ) )
#endif
			continue;

		DWORD filesize = SFileGetFileSize( fh );
		SFileCloseFile( fh );
		return filesize;
	}

	return 0;
}

wxString MPQFile::getArchive(wxString filename)
{
	if( useLocalFiles ) {
		wxString fn1 = wxGetCwd()+SLASH+wxT("Import")+SLASH;
		wxString fn2 = fn1;
		wxString fn3 = gamePath;
		fn1.Append(filename);
		fn2.Append(filename.AfterLast(SLASH));
		fn3.Append(filename);

		wxString fns[] = { fn1, fn2, fn3 };
		for(size_t i=0; i<WXSIZEOF(fns); i++) {
			wxString fn = fns[i];
			if (wxFile::Exists(fn)) {
				return fn;
			}
		}
	}

	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		HANDLE &mpq_a = *i->second;
		HANDLE fh;
#ifndef _MINGW
		if( !SFileOpenFileEx( mpq_a, filename.fn_str(), SFILE_OPEN_PATCHED_FILE, &fh ) )
#else
		if( !SFileOpenFileEx( mpq_a, filename.char_str(), SFILE_OPEN_PATCHED_FILE, &fh ) )
#endif
			continue;

		return i->first;
	}

	return wxT("unknown");
}

size_t MPQFile::getPos()
{
	return pointer;
}

unsigned char* MPQFile::getBuffer()
{
	return buffer;
}

unsigned char* MPQFile::getPointer()
{
	return buffer + pointer;
}


#include <wx/tokenzr.h>

void getFileLists(std::set<FileTreeItem> &dest, bool filterfunc(wxString))
{
	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		HANDLE &mpq_a = *i->second;
		bool isPartial = false;
		if (i->first.AfterLast(SLASH).StartsWith(wxT("wow-update-base")))
			isPartial = false;
		else if (i->first.AfterLast(SLASH).StartsWith(wxT("wow-update-")+langName))
			isPartial = false;
		else if (i->first.AfterLast(SLASH).StartsWith(wxT("wow-update-")))
			isPartial = true;

		HANDLE fh;
		if( SFileOpenFileEx( mpq_a, "(listfile)", 0, &fh ) )
		{
			// Found!
			DWORD filesize = SFileGetFileSize( fh );
			size_t size = filesize;

			wxString temp(i->first);
			temp.MakeLower();
			int col = 0; // Black

			if ((temp.Find(wxT("wow-update-")) > -1) || (temp.Find(wxT("patch.mpq")) > -1))
				col = 1; // Blue
			else if (temp.Find(wxT("cache")) > -1 || temp.Find(wxT("patch-2.mpq")) > -1)
				col = 2; // Red
			else if(temp.Find(wxT("expansion1.mpq")) > -1 || temp.Find(wxT("expansion.mpq")) > -1)
				col = 3; // Outlands Purple
			else if (temp.Find(wxT("expansion2.mpq")) > -1 || temp.Find(wxT("lichking.mpq")) > -1)
				col = 4; // Frozen Blue
			else if (temp.Find(wxT("expansion3.mpq")) > -1)
				col = 5; // Destruction Orange
			else if (temp.Find(wxT("expansion4.mpq")) > -1 || temp.Find(wxT("patch-3.mpq")) > -1)
				col = 6; // Bamboo Green
			else if (temp.Find(wxT("alternate.mpq")) > -1)
				col = 7; // Cyan

			if (size > 0 ) {
				unsigned char *buffer = new unsigned char[size];
				SFileReadFile( fh, buffer, (DWORD)size );
				unsigned char *p = buffer, *end = buffer + size;

				while (p < end) { // if p = end here, no need to go into next loop !
					unsigned char *q=p;
					do {
						if (*q=='\r' || *q=='\n') // carriage return or new line
							break;
					} while (q++<=end);

					wxString line(reinterpret_cast<char *>(p), wxConvUTF8, q-p);
					if (line.Length()==0) 
						break;
					p = q;
					if (*p == '\r')
						p++;
					if (*p == '\n')
						p++;
					//line = line.Trim(); // delete \r\n
					if (isPartial) {
						if (line.Lower().StartsWith(wxT("base\\"))) // strip "base\\"
							line = line.Mid(5);
						else if (line.StartsWith(langName)) // strip "enus\\"
							line = line.Mid(5);
						else
							continue;
					}

					if (filterfunc(wxString(line.mb_str(), wxConvUTF8))) {
						// This is just to help cleanup Duplicates
						// Ideally I should tokenise the string and clean it up automatically
						FileTreeItem tmp;

						tmp.fileName = line;
						line.MakeLower();
						line[0] = toupper(line.GetChar(0));
						int ret = line.Find('\\');
						if (ret>-1)
							line[ret+1] = toupper(line.GetChar(ret+1));

						tmp.displayName = line;
						tmp.color = col;
						dest.insert(tmp);
					}
				}

				wxDELETEA(buffer);
				p = NULL;
				end = NULL;
			}

			SFileCloseFile( fh );
		}
	}
}
