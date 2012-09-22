#include "mpq_libmpq.h"

#include <wx/log.h>
#include <wx/file.h>

#include <vector>
#include <algorithm>

#include "util.h"

typedef std::vector<mpq_archive*> ArchiveSet;
static ArchiveSet gOpenArchives;

MPQArchive::MPQArchive(wxString filename) : ok(false)
{
	int result = libmpq_archive_open(&mpq_a, (unsigned char*)filename.c_str());

	wxLogMessage(_T("Opening %s"), filename.c_str());
	if(result) {
		wxLogMessage(_T("Error opening archive %s"), filename.c_str());
		return;
	}

	ok = true;
	gOpenArchives.push_back(&mpq_a);
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

void MPQArchive::close()
{
	if (ok == false)
		return;
	libmpq_archive_close(&mpq_a);
	for(ArchiveSet::iterator it=gOpenArchives.begin(); it!=gOpenArchives.end();++it)
	{
		mpq_archive &mpq_b = **it;
		if (&mpq_b == &mpq_a) {
			gOpenArchives.erase(it);
			//delete (*it);
			return;
		}
	}
	
}

void
MPQFile::openFile(const char* filename)
{
	eof = false;
	buffer = 0;
	pointer = 0;
	size = 0;
	if( useLocalFiles ) {
		wxString fn1 = wxGetCwd()+SLASH+_T("Import")+SLASH;
		wxString fn2 = fn1;
		wxString fn3 = gamePath;
		fn1.Append(wxString(filename, wxConvUTF8));
		fn2.Append(wxString(filename, wxConvUTF8).AfterLast(SLASH));
		fn3.Append(wxString(filename, wxConvUTF8));

		wxString fns[] = { fn1, fn2, fn3 };
		for(unsigned int i=0; i<WXSIZEOF(fns); i++) {
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

	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end(); ++i)
	{
		mpq_archive &mpq_a = **i;
		int fileno = libmpq_file_number(&mpq_a, filename);
		if(fileno == LIBMPQ_EFILE_NOT_FOUND)
			continue;

		// Found!
		size = libmpq_file_info(&mpq_a, LIBMPQ_FILE_UNCOMPRESSED_SIZE, fileno);

		// HACK: in patch.mpq some files don't want to open and give 1 for filesize
		if (size<=1) {
			eof = true;
			buffer = 0;
			return;
		}

		buffer = new unsigned char[size];
 		libmpq_file_getdata(&mpq_a, fileno, buffer);
		return;
	}

	eof = true;
	buffer = 0;
}

MPQFile::MPQFile(const char* filename):
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

bool MPQFile::exists(const char* filename)
{
	if( useLocalFiles ) {
		wxString fn1 = wxGetCwd()+SLASH+_T("Import")+SLASH;
		wxString fn2 = fn1;
		wxString fn3 = gamePath;
		fn1.Append(wxString(filename, wxConvUTF8));
		fn2.Append(wxString(filename, wxConvUTF8).AfterLast(SLASH));
		fn3.Append(wxString(filename, wxConvUTF8));

		wxString fns[] = { fn1, fn2, fn3 };
		for(unsigned int i=0; i<WXSIZEOF(fns); i++) {
			wxString fn = fns[i];
			if (wxFile::Exists(fn))
				return true;
		}
	}

	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		mpq_archive &mpq_a = **i;
		int fileno = libmpq_file_number(&mpq_a, filename);
		if (fileno != LIBMPQ_EFILE_NOT_FOUND) 
			return true;
	}

	return false;
}

void MPQFile::save(const char* filename)
{
	wxFile f;
	f.Open(wxString(filename, wxConvUTF8), wxFile::write);
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

void MPQFile::seek(int offset)
{
	pointer = offset;
	eof = (pointer >= size);
}

void MPQFile::seekRelative(int offset)
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

int MPQFile::getSize(const char* filename)
{
	if( useLocalFiles ) {
		wxString fn1 = wxGetCwd()+SLASH+_T("Import")+SLASH;
		wxString fn2 = fn1;
		wxString fn3 = gamePath;
		fn1.Append(wxString(filename, wxConvUTF8));
		fn2.Append(wxString(filename, wxConvUTF8).AfterLast(SLASH));
		fn3.Append(wxString(filename, wxConvUTF8));

		wxString fns[] = { fn1, fn2, fn3 };
		for(unsigned int i=0; i<WXSIZEOF(fns); i++) {
			wxString fn = fns[i];
			if (wxFile::Exists(fn)) {
				wxFile file(fn);
				return file.Length();
			}
		}
	}

	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		mpq_archive &mpq_a = **i;
		int fileno = libmpq_file_number(&mpq_a, filename);
		if (fileno != LIBMPQ_EFILE_NOT_FOUND)
			return libmpq_file_info(&mpq_a, LIBMPQ_FILE_UNCOMPRESSED_SIZE, fileno);
	}

	return 0;
}

wxString MPQFile::getArchive(wxString filename)
{
	if( useLocalFiles ) {
		wxString fn1 = wxGetCwd()+SLASH+_T("Import")+SLASH;
		wxString fn2 = fn1;
		wxString fn3 = gamePath;
		fn1.Append(wxString(filename, wxConvUTF8));
		fn2.Append(wxString(filename, wxConvUTF8).AfterLast(SLASH));
		fn3.Append(wxString(filename, wxConvUTF8));

		wxString fns[] = { fn1, fn2, fn3 };
		for(unsigned int i=0; i<WXSIZEOF(fns); i++) {
			wxString fn = fns[i];
			if (wxFile::Exists(fn)) {
				return fn;
			}
		}
	}

	for(ArchiveSet::iterator i=gOpenArchives.begin(); i!=gOpenArchives.end();++i)
	{
		mpq_archive &mpq_a = **i;
		int fileno = libmpq_file_number(&mpq_a, filename);
		if (fileno != LIBMPQ_EFILE_NOT_FOUND) {
			return wxString(mpq_a.filename, wxConvUTF8);
		}
	}

	return _T("unknown");
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
		mpq_archive &mpq_a = **i;
		int fileno = libmpq_file_number(&mpq_a, "(listfile)");
		
		if(fileno != LIBMPQ_EFILE_NOT_FOUND) {
			// Found!
			size_t size = libmpq_file_info(&mpq_a, LIBMPQ_FILE_UNCOMPRESSED_SIZE, fileno);
			//int retVal = libmpq_file_info(&mpq_a, LIBMPQ_FILE_COMPRESSION_TYPE, fileno);
			// If retVal is 512, its compressed, if its 0 then its uncompressed

			int col = 0; // Black
			if (strstr(mpq_a.filename, "patch.MPQ"))
				col = 1; // Blue
			else if (strstr(mpq_a.filename, "patch-2.MPQ"))
				col = 2; // Red
			else if (strstr(mpq_a.filename, "patch-3.MPQ"))
				col = 3; // Green
			else if (strstr(mpq_a.filename, "expansion.MPQ"))
				col = 4; // Outlands Purple
			else if (strstr(mpq_a.filename, "expansion2.MPQ") || strstr(mpq_a.filename, "lichking.MPQ"))
				col = 5; // Frozen Blue
			else if (strstr(mpq_a.filename, "expansion3.MPQ"))
				col = 6; // Destruction Orange
			else if (strstr(mpq_a.filename, "patch-4.MPQ"))
				col = 7; // Cyan

			// TODO: Add handling for uncompressed files.
			// err.. it seems uncompressed files no longer cause crashes?
			if (size > 0 /*&& retVal != 0*/) {
				unsigned char *buffer = new unsigned char[size];
				libmpq_file_getdata(&mpq_a, fileno, buffer);
				unsigned char *p = buffer, *end = buffer + size;

				while (p <= end) {
					unsigned char *q=p;
					do {
						if (*q==13) 
							break;
					} while (q++<=end);
                    wxString line(reinterpret_cast<char *>(p), q-p);
					if (line.length()==0) 
						break;
					//p += line.length();
					p = q + 2;
					//line.erase(line.length()-2, 2); // delete \r\n

					if (filterfunc(line)) {
						// This is just to help cleanup Duplicates
						// Ideally I should tokenise the string and clean it up automatically
                        FileTreeItem tmp;
                        tmp.fileName = line;
						std::transform(line.begin(), line.end(), line.begin(), ::tolower);
						line[0] = toupper(line[0]);
						int ret = line.find('\\');
						if (ret>-1)
							line[ret+1] = toupper(line.at(ret+1));
                        tmp.displayName = line;
						tmp.color = col;
						dest.insert(tmp);
					}
				}
				wxDELETEA(buffer);
			}
		}
	}
}

