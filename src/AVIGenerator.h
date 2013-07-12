// AVIGenerator.h: interface for the CAVIGenerator class.
//
// A class to easily create AVI
//
// Original code: Example code in WriteAvi.c of MSDN
// 
// Author: Jonathan de Halleux. dehalleux@auto.ucl.ac.be
//
// Modified: by John (Darjk) Steele, jsteele05@gmail.com on 10/07/06, 
//			for use with the WoW Model Viewer.
//			  Cut out excess code (made a "lite" version) and took advantage of wxWidgets 
//			  which is used throughout the WoW Model Viewer project.
//
//////////////////////////////////////////////////////////////////////

#ifndef AVIGENERATOR_H
#define AVIGENERATOR_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// needed headers
#include <comdef.h>
#include <memory.h>
#include <tchar.h>
#include <string.h>

#pragma warning(disable : 4311)
#include <vfw.h>
#pragma warning(default : 4311)

#include <assert.h>

#pragma message("     Adding library: vfw32.lib" ) 
#pragma comment(lib, "vfw32.lib")

/*! \brief A simple class to create AVI video stream.

\par Usage

  Step 1 : Declare an CAVIGenerator object
  Step 2 : Set Bitmap by calling SetBitmapHeader functions + other parameters
  Step 3 : Initialize engine by calling InitEngine
  Step 4 : Send each frames to engine with function AddFrame
  Step 5 : Close engine by calling ReleaseEngine

\par Update history:

	- {\bf 22-10-2002} Minor changes in constructors.

\author : Jonathan de Halleux, dehalleux@auto.ucl.ac.be (2001)
*/
class CAVIGenerator  
{
public:
	//! \name Constructors and destructors
	//! Default constructor 
	CAVIGenerator();

	//! Inplace constructor with BITMAPINFOHEADER
	//CAVIGenerator(LPCTSTR _sFileName, LPBITMAPINFOHEADER lpbih, DWORD dwRate);
	~CAVIGenerator();

	//! \name  AVI engine function
	//! \brief  Initialize engine and choose codex
	//Some asserts are made to check that bitmap has been properly initialized
	HRESULT InitEngineForWrite(HWND parent = NULL);
	void InitEngineForRead();

	//! \brief Adds a frame to the movie. 
	//The data pointed by bmBits has to be compatible with the bitmap description of the movie.
	HRESULT AddFrame(BYTE* bmBits);

	// Get RGB image data from specified frame.
	void GetFrame();	

	//! Release ressources allocated for movie and close file.
	void ReleaseEngine();

	//! \name Setters and getters
	//! Sets bitmap info as in lpbih
	void SetBitmapHeader(BITMAPINFOHEADER lpbih);

	//! returns a pointer to bitmap info
	LPBITMAPINFOHEADER GetBitmapHeader() {return &m_bih;};

	//! sets the name of the ouput file (should be .avi)
	void SetFileName(LPCTSTR sFileName) { m_sFile = sFileName;}

	//! Sets FrameRate (should equal or greater than one)
	void SetRate(DWORD dwRate) { m_dwRate=dwRate;};

	int GetWidth() {return m_iWidth;}
	int GetHeight() {return m_iHeight;}

private:
	//! name of output file
	//_bstr_t m_sFile;
	LPCSTR m_sFile;

	//! Frame rate 
	DWORD m_dwRate;	

	BITMAPINFOHEADER m_bih;		// structure contains information for a single stream
	char *m_pData;				// Pointer To Texture Data

	//BITMAPINFOHEADER	bmih;			// Header Information For DrawDibDraw Decoding
	int	m_iWidth;						// Video Width
	int m_iHeight;						// Video Height	

	long m_iCurFrame;		// frame counter (current frame)
	long m_iFirstFrame;		// First frame of the stream
	long m_iLastFrame;		// Last Frame Of The Stream
	int m_iMSPerFrame;		// Will Hold The Rough Milliseconds Per Frame

	PAVIFILE m_pAVIFile;				// file interface pointer

	AVISTREAMINFO m_pStreamInfo;		// Pointer To A Structure Containing Stream Info
	PGETFRAME m_pGetFrame;				// Pointer To A GetFrame Object

	PAVISTREAM m_pStream;				// Handle To An Open Stream
	PAVISTREAM m_pStreamCompressed;		// Handle To An Open Compressed Stream
};

#endif // !defined(AFX_AVIGENERATOR_H
