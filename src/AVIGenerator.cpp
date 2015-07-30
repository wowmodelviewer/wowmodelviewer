// AVIGenerator.cpp: implementation of the CAVIGenerator class.
//
//////////////////////////////////////////////////////////////////////

#include "AVIGenerator.h"

// Temp openGL headers for testing to confirm that it works
#include "OpenGLHeaders.h"


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CAVIGenerator::CAVIGenerator()
{
	m_pAVIFile = NULL;
	m_pStream = NULL;
	m_pStreamCompressed = NULL;
	m_pGetFrame = NULL;
	memset(&m_bih,0,sizeof(BITMAPINFOHEADER));

	// Default file name.
	m_sFile = "untitled.avi";

	// Default frame rate, only matters if writing to AVI
	 m_dwRate = 30;
}

CAVIGenerator::~CAVIGenerator()
{
	// Just checking that all allocated ressources have been released
	assert(m_pStream==NULL);
	assert(m_pStreamCompressed==NULL);
	assert(m_pAVIFile==NULL);
}

void CAVIGenerator::SetBitmapHeader(BITMAPINFOHEADER lpbih)
{
	// checking that bitmap size are multiple of 4
	assert(lpbih.biWidth%4==0);
	assert(lpbih.biHeight%4==0);

	// copying bitmap info structure.
	// corrected thanks to Lori Gardi
	memcpy(&m_bih, &lpbih, sizeof(BITMAPINFOHEADER));
}

HRESULT CAVIGenerator::InitEngineForWrite(HWND parent)
{
	AVISTREAMINFO strHdr; // information for a single stream 
	AVICOMPRESSOPTIONS opts;
	AVICOMPRESSOPTIONS FAR * aopts[1] = {&opts};
	
	HRESULT hr;
	
	LOG_INFO << "Initating AVI class object for writing.";

	// Step 0 : Let's make sure we are running on 1.1 
	DWORD wVer = HIWORD(VideoForWindowsVersion());
	if (wVer < 0x010a) {
		 // oops, we are too old, blow out of here 
		LOG_ERROR << "Version of Video for Windows is too old. Come on, join the 21th century!";
		return S_FALSE;
	}

	// Step 1 : initialize AVI engine
	AVIFileInit();

	// Step 2 : Open the movie file for writing....
	hr = AVIFileOpen(&m_pAVIFile,			// Address to contain the new file interface pointer
					(LPCSTR)m_sFile,		// Null-terminated string containing the name of the file to open
					OF_WRITE | OF_CREATE,	// Access mode to use when opening the file. 
					NULL);					// use handler determined from file extension.
											// Name your file .avi -> very important

	if (hr != AVIERR_OK) {
		LOG_ERROR << "AVI Engine failed to initialize. Check filename" << m_sFile.c_str();
		// Check it succeded.
		switch(hr) {
		case AVIERR_BADFORMAT: 
			LOG_ERROR << "The file couldn't be read, indicating a corrupt file or an unrecognized format.";
			break;
		case AVIERR_MEMORY:		
			LOG_ERROR << "The file could not be opened because of insufficient memory.";
			break;
		case AVIERR_FILEREAD:
			LOG_ERROR << "A disk error occurred while reading the file.";
			break;
		case AVIERR_FILEOPEN:		
			LOG_ERROR << "A disk error occurred while opening the file.";
			break;
		case REGDB_E_CLASSNOTREG:		
			LOG_ERROR << "According to the registry, the type of file specified in AVIFileOpen does not have a handler to process it";
			break;
		}

		return hr;
	}

	// Fill in the header for the video stream....
	memset(&strHdr, 0, sizeof(strHdr));
	strHdr.fccType                = streamtypeVIDEO;	// video stream type
	strHdr.fccHandler             = 0;
	strHdr.dwScale                = 1;					// should be one for video
	strHdr.dwRate                 = m_dwRate;		    // fps
	strHdr.dwSuggestedBufferSize  = m_bih.biSizeImage;	// Recommended buffer size, in bytes, for the stream.
	SetRect(&strHdr.rcFrame, 0, 0,		    // rectangle for stream
	    (int) m_bih.biWidth,
	    (int) m_bih.biHeight);

	// Step 3 : Create the stream;
	hr = AVIFileCreateStream(m_pAVIFile,		// file pointer
							&m_pStream,		    // returned stream pointer
							&strHdr);			// stream header

	// Check it succeded.
	if (hr != AVIERR_OK) {
		LOG_ERROR << "Stream creation failed. Check Bitmap info.";
		if (hr==AVIERR_READONLY) {
			LOG_ERROR << "Read only file.";
		}
		return hr;
	}


	// Step 4: Get codec and infos about codec
	memset(&opts, 0, sizeof(opts));
	// Poping codec dialog
	if (!AVISaveOptions(parent, ICMF_CHOOSE_KEYFRAME|ICMF_CHOOSE_DATARATE, 1, &m_pStream, (LPAVICOMPRESSOPTIONS FAR *) &aopts))	{
		AVISaveOptionsFree(1,(LPAVICOMPRESSOPTIONS FAR *) &aopts);
		return S_FALSE;
	}

	// Step 5:  Create a compressed stream using codec options.
	hr = AVIMakeCompressedStream(&m_pStreamCompressed, m_pStream, &opts, NULL);

	if (hr != AVIERR_OK) {
		LOG_ERROR << "AVI Compressed Stream creation failed.";
		
		switch(hr) {
		case AVIERR_NOCOMPRESSOR:
			LOG_ERROR << "A suitable compressor cannot be found.";
			break;
		case AVIERR_MEMORY:
			LOG_ERROR << "There is not enough memory to complete the operation.";
			break; 
		case AVIERR_UNSUPPORTED:
			LOG_ERROR << "Compression is not supported for this type of data. This error might be returned if you try to compress data that is not audio or video.";
			break;
		}

		return hr;
	}


	// releasing memory allocated by AVISaveOptionFree
	hr=AVISaveOptionsFree(1,(LPAVICOMPRESSOPTIONS FAR *) &aopts);
	if (hr!=AVIERR_OK) {
		LOG_ERROR << "Problem releasing memory";
		return hr;
	}

	// Step 6 : sets the format of a stream at the specified position
	hr = AVIStreamSetFormat(m_pStreamCompressed, 
							0,			// position
							&m_bih,	    // stream format
							sizeof(m_bih)); // format size

	if (hr != AVIERR_OK) {
		LOG_ERROR << "Compressed Stream format setting failed.";
		return hr;
	}

	// Step 6 : Initialize step counter
	m_iLastFrame = 0;

	return hr;
}

void CAVIGenerator::InitEngineForRead()
{
	LOG_INFO << "Initiating AVI class object for reading.";

	// make sure we are running on 1.1 or newer
	DWORD wVer = HIWORD(VideoForWindowsVersion());
	if (wVer < 0x010a) {
		LOG_ERROR << "Version of Video for Windows is too old. Come on, join the 21th century!";
		return;
	}

	// Opens The AVIFile Library
	AVIFileInit();

	// Opens The AVI Stream
	if (AVIStreamOpenFromFile(&m_pStream, m_sFile, streamtypeVIDEO, 0, OF_READ, NULL) !=0) {
		// An Error Occurred Opening The Stream
		LOG_ERROR << "Failed To Open The AVI Stream.";
		ReleaseEngine();
		return;
	}

	AVIStreamInfo(m_pStream, &m_pStreamInfo, sizeof(AVISTREAMINFO));		// Reads Information About The Stream Into psi
	m_iWidth = m_pStreamInfo.rcFrame.right - m_pStreamInfo.rcFrame.left;	// Width Is Right Side Of Frame Minus Left
	m_iHeight = m_pStreamInfo.rcFrame.bottom - m_pStreamInfo.rcFrame.top;	// Height Is Bottom Of Frame Minus Top

	m_iFirstFrame = AVIStreamStart(m_pStream);
	m_iCurFrame = m_iFirstFrame;
	m_iLastFrame = AVIStreamLength(m_pStream);							// The Last Frame Of The Stream
	m_iMSPerFrame = AVIStreamSampleToTime(m_pStream, m_iLastFrame) / m_iLastFrame;	// Calculate Rough Milliseconds Per Frame

	/*
	m_bih.biSize = sizeof (BITMAPINFOHEADER);					// Size Of The BitmapInfoHeader
	m_bih.biPlanes = 1;											// Bitplanes	
	m_bih.biBitCount = 24;										// Bits Format We Want (24 Bit, 3 Bytes)
	m_bih.biWidth = 256;										// Width We Want (256 Pixels)
	m_bih.biHeight = 256;										// Height We Want (256 Pixels)
	m_bih.biCompression = BI_RGB;								// Requested Mode = RGB
	*/

	//m_hBitmap = CreateDIBSection(hdc, (BITMAPINFO*)(&m_bih), DIB_RGB_COLORS, (void**)(&data), NULL, NULL);
	//SelectObject(hdc, hBitmap);								// Select hBitmap Into Our Device Context (hdc)

	m_pGetFrame = AVIStreamGetFrameOpen(m_pStream, NULL);		// Create The PGETFRAME	Using Our Request Mode
	if (m_pGetFrame == NULL) {
		// An Error Occurred Opening The Frame
		LOG_ERROR << "Failed To Open The AVI Frame.";
		ReleaseEngine();
	}
}

// Properly Closes The Avi File
void CAVIGenerator::ReleaseEngine()
{
	if (m_pGetFrame) {
		AVIStreamGetFrameClose(m_pGetFrame);						// Deallocates The GetFrame Resources
		m_pGetFrame = NULL;
	}

	if (m_pStream) {
		AVIStreamRelease(m_pStream);
		m_pStream = NULL;
	}

	if (m_pStreamCompressed) {
		AVIStreamRelease(m_pStreamCompressed);
		m_pStreamCompressed = NULL;
	}

	if (m_pAVIFile)	{
		AVIFileRelease(m_pAVIFile);
		m_pAVIFile=NULL;
	}

	// Close engine
	AVIFileExit();
}

HRESULT CAVIGenerator::AddFrame(BYTE *bmBits)
{
	HRESULT hr;

	// compress bitmap
	hr = AVIStreamWrite(m_pStreamCompressed,	// stream pointer
		m_iLastFrame,						// time of this frame
		1,						// number to write
		bmBits,					// image buffer
		m_bih.biSizeImage,		// size of this frame
		AVIIF_KEYFRAME,			// flags....
		NULL,
		NULL);

	// updating frame counter
	m_iLastFrame++;

	return hr;
}

// Grabs the next frame from the stream
void CAVIGenerator::GetFrame()									
{

	BYTE *pDIB = (BYTE*) AVIStreamGetFrame(m_pGetFrame, m_iCurFrame);
	//ASSERT(pDIB!=NULL);
	if (!pDIB)
		return;

	//Creates a full-color (no palette) DIB from a pointer to a full-color memory DIB
	//get the BitmapInfoHeader
	BITMAPINFOHEADER bih;
	RtlMoveMemory(&bih.biSize, pDIB, sizeof(BITMAPINFOHEADER));

	//now get the bitmap bits
	if (bih.biSizeImage < 1)
	{
		return;
	}

	BYTE *pData = new BYTE[bih.biSizeImage];
	RtlMoveMemory(pData, pDIB + sizeof(BITMAPINFOHEADER), bih.biSizeImage);
	
	//flipIt(data);												// Swap The Red And Blue Bytes (GL Compatability)

	// Update The Texture
	//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, bih.biWidth, bih.biHeight, GL_RGB8, GL_UNSIGNED_BYTE, pData);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bih.biWidth, bih.biHeight, 0, GL_BGR, GL_UNSIGNED_BYTE, pData);
	delete[] pData;

	m_iCurFrame++;
	if (m_iCurFrame >= m_iLastFrame)
		m_iCurFrame = m_iFirstFrame;
}
