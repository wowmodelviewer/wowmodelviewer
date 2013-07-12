#include "modelexport_lwo.h"
#include "wx/txtstrm.h"

//---------------------------------------------
// --== Master Object Writing Function ==--
//---------------------------------------------

// Write Lightwave Object data to a file.
size_t WriteLWObject(wxString filename, LWObject Object) {
	g_modelViewer->SetStatusText(wxT("Writing Lightwave Object..."));
	/* LightWave object files use the IFF syntax described in the EA-IFF85 document. Data is stored in a collection of chunks. 
	Each chunk begins with a 4-byte chunk ID and the size of the chunk in bytes, and this is followed by the chunk contents.

	LWO Model Format, as layed out in official LWO2 files. (I Hex-edited to find most of this information from files I made/saved in Lightwave. -Kjasi)

	FORM	// Format Declaration
	LWO2	// Declares this is the Lightwave Object 2 file format. LWOB is the first format. Doesn't have a lot of the cool stuff LWO2 has...

	TAGS	// Used for various Strings
		Sketch Color Names
		Part Names
		Surface Names
	LAYR		// Specifies the start of a new layer. Probably best to only be on one...
		PNTS		// Points listing & Block Section
			BBOX		// Bounding Box. It's optional, but will probably help.
			VMPA		// Vertex Map Parameters, Always Preceeds a VMAP & VMAD. 4bytes: Size (2 * 4 bytes).
						// UV Sub Type: 0-Linear, 1-Subpatched, 2-Linear Corners, 3-Linear Edges, 4-Across Discontinuous Edges.
						// Sketch Color: 0-12; 6-Default Gray
				VMAP		// Vector Map Section. Always Preceeds the following:
					SPOT	// Aboslute Morph Maps. Used only while modeling. Ignore.
					TXUV	// Defines UV Vector Map. Best not to use these unless the data has no Discontinuous UVs.
					PICK	// Point Selection Sets (2 bytes, then Set Name, then data. (Don't know what kind)
					MORF	// Relative Morph Maps. These are used for non-boned mesh animation.
					RGB		// Point Color Map, no Alpha. Note the space at end of the group!
					RGBA	// Same as above, but with an alpha channel.
					WGHT	// Weight Map. Used to give bones limited areas to effect, or can be used for point-by-point maps for various surfacing tricks.

		POLS		// Declares Polygon section. Next 4 bytes = Number of Polys
			FACE		// The actual Polygons. The maximum number of vertices is 1023 per poly!
				PTAG		// The Poly Tags for this Poly. These usually reference items in the TAGS group.
					COLR	// The Sketch Color Name
					PART	// The Part Name
					SURF	// The Surface Name
				VMPA		// Discontinuous Vertex Map Parameters (See the one in the Points section for details)
					VMAD		// Discontinuous Vector Map Section. Best if used only for UV Maps. Difference between VMAP & VMAD: VMAPs are connected to points, while VMADs are connected to Polys.
						APSL	// Adaptive Pixel Subdivision Level. Only needed for sub-patched models, so just ignore it for our outputs.
						TXUV	// Defines UV Vector Map
						NORM	// Define's a poly's Normals
			PTCH	// Cat-mull Clarke Patches. Don't need this, but it mirror's FACE's sub-chunks.
			SUBD	// Subdivision Patches. Same as above.
			MBAL	// Metaballs. Don't bother...
			BONE	// Line segments representing the object's skeleton. These are converted to bones for deformation during setup/rigging.

	CLIP (for each Image)
		STIL	// 2 bytes, size of string, followed by image name.extention
		FLAG	// Flags. 2 bytes, size of chunk. Not sure what the flag values are.
		AMOD	// 2 bytes: What's changed, 2 bytes: value. 2-Alphas: 0-Enabled, 1-Disabled, 2-Alpha Only. AMOD is omitted if value is 0.
		XREF	// Calls an instance of a CLIP. We'll avoid this for now.

	SURF	// Starts the surface's data. Not sure about the 4 bytes after it...
		// Until BLOK, this just sets the default values
		COLR	// Color
		LUMI	// Luminosity
		DIFF	// Diffusion
		SPEC	// Specularity
		REFL	// Reflections
		TRAN	// Transparancy
		TRNL	// Translucency
		RIND	// Refractive Index
		BUMP	// Bump Amount
		GLOS	// Glossiness
		VCOL	// 2 bytes, size | 1.0f (4bytes) | Zero (2 bytes) | Map Type | MapName
			RGB		// Vertex Color Map Type without Alpha
			RGBA	// Vertex Color Map Type with an Alpha
		GVAL	// Glow
		SHRP	// Diffuse Sharpness
		SMAN	// Smoothing Amount
		RFOP	// Reflection Options: 0-Backdrop Only (default), 1-Raytracing + Backdrop, 2 - Spherical Map, 3 - Raytracing + Spherical Map
		TROP	// Same as RFOP, but for Refraction.
		SIDE	// Is it Double-Sided?
		NVSK	// Exclude from VStack
		NORM	// Specifies the Normal Map's Name

		CMNT // Surface Comment. 2bytes: Size. Simple Text line for this surface. Make sure it doesn't end on an odd byte! VERS must be 931 or 950!
		VERS // Version Compatibility mode, including what it's compatible with. 2 bytes (int16, value 4), 4 bytes (int32, value is 850 for LW8.5 Compatability, 931 for LW9.3.1, and 950 for Default)

		BLOK	// First Blok. Bloks hold Surface texture information!
			IMAP	// Declares that this surface texture is an image map.
				CHAN COLR	// Declares that the image map will be applied to the color channel. (Color has a Texture!)
					OPAC	// Opacity of Layer
					ENAB	// Is the layer enabled?
					NEGA	// Is it inverted?
					TMAP	// Texture Map details
						CNTR	// Position
						SIZE	// Scale
						ROTA	// Rotation
						FALL	// Falloff
						OREF	// Object Reference
						CSYS	// Coordinate System: 0-Object's Coordinates, 1-World's Coordinates

						// Image Maps
						PROJ	// Image Projection Mode: 0-Planar (references AXIS), 1-Cylindrical, 2-Spherical, 3-Cubic, 4-Front Projection, 5-UV (IDed in VMAP chunk)
						AXIS	// The axis the image uses: 0-X, 1-Y, or 2-Z;
						IMAG	// The image to use: Use CLIP Index
						WRAP	// Wrapping Mode: 0-Reset, 1-Repeat, 2-Mirror, 3-Edge
						WRPW	// Wrap Count Width (Used for Cylindrical & Spherical projections)
						WRPH	// Wrap Count Height
						VMAP	// Name of the UV Map to use, should PROJ be set to 5!
						AAST	// Antialiasing Strength
						PIXB	// Pixel Blending

		// Node Information
		// We can probably skip this for now. Later, it would be cool to mess with it, but for now, it'll be automatically generated once the file is opened in LW.

		NODS	// Node Block & Size
			NROT
			NLOC
			NZOM
			NSTA	// Activate Nodes
			NVER
			NNDS
			NSRV
				Surface
			NTAG
			NRNM
				Surface
			NNME
				Surface
			NCRD
			NMOD
			NDTA
			NPRW
			NCOM
			NCON

	*/

	// Check to see if we have any data to generate this file.
	if ((Object.Layers.size() < 1) || (Object.Layers[0].Points.size() < 1)){
		wxMessageBox(wxT("No Layer Data found.\nUnable to write object file."),wxT("Error"));
		wxLogMessage(wxT("Error: No Layer Data. Unable to write object file."));
		return EXPORT_ERROR_NO_DATA;
	}


	// Open Model File
	wxFileName filef = wxString(filename, wxConvUTF8);
	wxString file = wxString(filename, wxConvUTF8);

	if (filef.IsOk() == false){
		return EXPORT_ERROR_BAD_FILENAME;
	};

	// Check if file exists, and ask about overwriting if it does.
	bool overwrite = false;		// Don't overwrite by default.
	if (filef.FileExists() == true){
		wxLogMessage(wxT("File %s already exists. Asking if we should overwrite..."),file.c_str());
		wxMessageDialog *ovr = new wxMessageDialog(NULL,wxString::Format(wxT("File \"%s\" already exists.\nDo you wish to overwrite the file?"),file.AfterLast(SLASH).c_str()),wxT("Overwrite Confirmation"),wxYES_NO | wxNO_DEFAULT | wxCANCEL | wxICON_QUESTION | wxSTAY_ON_TOP);
		size_t result = ovr->ShowModal();
		if (result == wxID_YES){
			overwrite = true;
			wxLogMessage(wxT("User has chosen to overwrite the file. Continuing..."));
		}else{
			overwrite = false;
			wxLogMessage(wxT("User has chosen not to overwrite the file. Aborting..."));
		}
	}else{
		// If the file doesn't exist, then continue with the writing.
		overwrite = true;
	}
	if (overwrite == false){
		return EXPORT_ERROR_NO_OVERWRITE;
	}

	wxFFileOutputStream f(file, wxT("w+b"));
	if (!f.IsOk()) {
		wxMessageBox(wxString::Format(wxT("Unable to access Lightwave Object file.\nCould not export model %s."), file.AfterLast(SLASH).c_str()), wxT("Error"));
		wxLogMessage(wxT("Error: Unable to open Lightwave Object file '%s'. Could not export model."), file.c_str());
		g_modelViewer->SetStatusText(wxT("Unable to open Lightwave Object file. Could not export model."));
		return EXPORT_ERROR_FILE_ACCESS;
	}

   	// -----------------------------------------
	// Initial Variables
	// -----------------------------------------

	// File Length
	unsigned int fileLen = 0;

	// Other Declares
	int off_T;
	uint16 zero = 0;

	// Needed Numbers
	size_t TagCount = Object.PartNames.size() + Object.Surfaces.size();

	// ===================================================
	// FORM		// Format Declaration
	//
	// Always exempt from the length of the file!
	// ===================================================
	f.Write("FORM", 4);
	f.Write(reinterpret_cast<char *>(&fileLen), 4);

	// ===================================================
	// LWO2
	//
	// Declares this is the Lightwave Object 2 file format.
	// LWO2 is the modern format, LWOB is the first format. LWOB doesn't have a lot of the capabilities of LWO2, which we need for exporting.
	// ===================================================
	f.Write("LWO2", 4);
	fileLen += 4;

	// ===================================================
	// TAGS
	//
	// Used for various Strings. Known string types, in order:
	//		Sketch Color Names
	//		Part Names
	//		Surface Names
	// ===================================================

	if (TagCount > 0) {
		g_modelViewer->SetStatusText(wxT("LWO Export: Writing Tags..."));
		f.Write("TAGS", 4);
		uint32 tagsSize = 0;
		f.Write(reinterpret_cast<char *>(&tagsSize), 4);
		fileLen += 8;

		// Parts
		for (size_t i=0; i<Object.PartNames.size(); i++){
			wxString PartName = Object.PartNames[i];

			PartName.Append(wxT('\0'));
			if (fmod((float)PartName.Len(), 2.0f) > 0)
				PartName.Append(wxT('\0'));
			f.Write(PartName.data(), PartName.Len());
			tagsSize += (uint32)PartName.Len();
		}
		// Surfaces
		for (size_t i=0; i<Object.Surfaces.size(); i++){
			wxString SurfName = Object.Surfaces[i].Name;

			SurfName.Append(wxT('\0'));
			if (fmod((float)SurfName.Len(), 2.0f) > 0)
				SurfName.Append(wxT('\0'));
			f.Write(SurfName.data(), SurfName.Len());
			tagsSize += (uint32)SurfName.Len();
		}

		// Correct TAGS Length
		off_T = -4-tagsSize;
		f.SeekO(off_T, wxFromCurrent);
		u32 = MSB4<uint32>(tagsSize);
		f.Write(reinterpret_cast<char *>(&u32), 4);
		f.SeekO(0, wxFromEnd);

		fileLen += tagsSize;
	}

	// -------------------------------------------------
	// Generate our Layers
	//
	// Point, Poly & Vertex Map data will be nested in
	// our layers.
	// -------------------------------------------------
	for (size_t l=0;l<Object.Layers.size();l++){
		g_modelViewer->SetStatusText(wxString::Format(wxT("LWO Export: Writing Layer %i data..."), l));
		LWLayer cLyr = Object.Layers[l];
		// Define a Layer & It's data
		if (cLyr.Name.length() > 0)
			cLyr.Name.Append(wxT('\0'));
		if (fmod((float)cLyr.Name.length(), 2.0f) > 0)
			cLyr.Name.Append(wxT('\0'));
		uint16 LayerNameSize = (uint16)cLyr.Name.length();
		uint32 LayerSize = 16+LayerNameSize;
		if ((cLyr.ParentLayer)&&(cLyr.ParentLayer>-1))
			LayerSize += 2;
		f.Write("LAYR", 4);
		u32 = MSB4<uint32>(LayerSize);
		f.Write(reinterpret_cast<char *>(&u32), 4);
		fileLen += 8;

		// Layer Number
		u16 = MSB2((uint16)l);
		f.Write(reinterpret_cast<char *>(&u16), 2);
		// Flags
		f.Write(reinterpret_cast<char *>(&zero), 2);
		// Pivot
		f.Write(reinterpret_cast<char *>(&zero), 4);
		f.Write(reinterpret_cast<char *>(&zero), 4);
		f.Write(reinterpret_cast<char *>(&zero), 4);
		// Name
		if (LayerNameSize>0){
			f.Write(cLyr.Name, LayerNameSize);
		}
		// Parent
		if ((cLyr.ParentLayer)&&(cLyr.ParentLayer>-1)){
			int pLyr = MSB2(cLyr.ParentLayer);
			f.Write(reinterpret_cast<char *>(&pLyr), 2);
		}
		fileLen += LayerSize;

		// -------------------------------------------------
		// Points Chunk
		//
		// There will be new Point Chunk for every Layer, so if we go
		// beyond 1 Layer, this should be nested.
		// -------------------------------------------------

		g_modelViewer->SetStatusText(wxT("LWO Export: Writing Points..."));
		uint32 pointsSize = (uint32)cLyr.Points.size()*12;
		f.Write("PNTS", 4);
		u32 = MSB4<uint32>(pointsSize);
		f.Write(reinterpret_cast<char *>(&u32), 4);
		fileLen += 8 + pointsSize;	// Corrects the filesize...

		// Writes the point data
		for (size_t i=0; i<(uint32)cLyr.Points.size(); i++) {
			Vec3D Points = cLyr.Points[i].PointData;
			MakeModelFaceForwards(Points,true);		// Face the model Forwards
			Points *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
			Vec3D vert;
			vert.x = MSB4<float>(Points.x);
			vert.y = MSB4<float>(Points.y);
			vert.z = MSB4<float>(Points.z);

			f.Write(reinterpret_cast<char *>(&vert.x), 4);
			f.Write(reinterpret_cast<char *>(&vert.y), 4);
			f.Write(reinterpret_cast<char *>(&vert.z), 4);
		}

		// --== Bounding Box Info ==--
		// Skipping for now.


		// --== Vertex Mapping ==--
		// UV, Weights, Vertex Color Maps, etc.
		g_modelViewer->SetStatusText(wxT("LWO Export: Writing Vertex Maps..."));
		// ===================================================
		//VMPA		// Vertex Map Parameters, Always Preceeds a VMAP & VMAD. 4bytes: Size, then Num Vars (2) * 4 bytes.
					// UV Sub Type: 0-Linear, 1-Subpatched, 2-Linear Corners, 3-Linear Edges, 4-Across Discontinuous Edges.
					// Sketch Color: 0-12; 6-Default Gray
		// ===================================================
		f.Write("VMPA", 4);
		u32 = MSB4<uint32>(8);	// We got 2 Paramaters, * 4 Bytes.
		f.Write(reinterpret_cast<char *>(&u32), 4);
		u32 = 4;				// Across Discontinuous Edges UV Sub Type
		f.Write(reinterpret_cast<char *>(&u32), 4);
		u32 = MSB4<uint32>(6);	// Default Gray
		f.Write(reinterpret_cast<char *>(&u32), 4);
		fileLen += 16;

		// ===================================================
		// Point UV Data (VMAP)
		// ===================================================
		uint32 vmapSize = 0;
		f.Write("VMAP", 4);
		u32 = MSB4<uint32>(vmapSize);
		f.Write(reinterpret_cast<char *>(&u32), 4);
		fileLen += 8;
		f.Write("TXUV", 4);
		u16 = MSB2(2);
		f.Write(reinterpret_cast<char *>(&u16), 2);
		f.Write(wxT("Texture"), 7);
		f.Write(reinterpret_cast<char *>(&zero), 1);
		vmapSize += 14;

		for (size_t i=0; i<(uint32)cLyr.Points.size(); i++) {
			LW_WriteVX(f,i,vmapSize);

			Vec2D vert;
			vert.x = MSB4<float>(cLyr.Points[i].UVData.x);
			vert.y = MSB4<float>(1-cLyr.Points[i].UVData.y);
			f.Write(reinterpret_cast<char *>(&vert.x), 4);
			f.Write(reinterpret_cast<char *>(&vert.y), 4);
			vmapSize += 8;
		}
		fileLen += vmapSize;
		off_T = -4-vmapSize;
		f.SeekO(off_T, wxFromCurrent);
		u32 = MSB4<uint32>(vmapSize);
		f.Write(reinterpret_cast<char *>(&u32), 4);
		f.SeekO(0, wxFromEnd);

		// ===================================================
		// Vertex Colors Map
		// ===================================================
		if (cLyr.HasVectorColors == true){
			wxLogMessage(wxT("Has Vector Colors"));

			f.Write("VMPA", 4);
			u32 = MSB4<uint32>(8);	// We got 2 Paramaters, * 4 Bytes.
			f.Write(reinterpret_cast<char *>(&u32), 4);
			u32 = 4;				// Across Discontinuous Edges UV Sub Type
			f.Write(reinterpret_cast<char *>(&u32), 4);
			u32 = MSB4<uint32>(6);	// Default Gray
			f.Write(reinterpret_cast<char *>(&u32), 4);
			fileLen += 16;

			uint32 vmapSize = 0;
			f.Write("VMAP", 4);
			u32 = MSB4<uint32>(vmapSize);
			f.Write(reinterpret_cast<char *>(&u32), 4);
			fileLen += 8;
			f.Write("RGBA", 4);
			u16 = MSB2(4);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f.Write("Colors", 6);
			f.Write(reinterpret_cast<char *>(&zero), 2);
			vmapSize += 14;

			for (size_t i=0;i<(uint32)cLyr.Points.size();i++) {
				float rf,gf,bf,af;
				rf = (float)(cLyr.Points[i].VertexColors.r/255.0f);
				gf = (float)(cLyr.Points[i].VertexColors.g/255.0f);
				bf = (float)(cLyr.Points[i].VertexColors.b/255.0f);
				af = (float)((255.0f-cLyr.Points[i].VertexColors.a)/255.0f);
				//wxLogMessage(wxT("Point: %i - R:%f(%i), G:%f(%i), B:%f(%i), A:%f(%i)"),i,rf,cLyr.Points[i].VertexColors.r,gf,cLyr.Points[i].VertexColors.g,bf,cLyr.Points[i].VertexColors.b,af,cLyr.Points[i].VertexColors.a);

				LW_WriteVX(f,i,vmapSize);

				Vec4D vert;
				vert.w = MSB4<float>(rf);
				vert.x = MSB4<float>(gf);
				vert.y = MSB4<float>(bf);
				vert.z = MSB4<float>(af);
				f.Write(reinterpret_cast<char *>(&vert.w), 4);
				f.Write(reinterpret_cast<char *>(&vert.x), 4);
				f.Write(reinterpret_cast<char *>(&vert.y), 4);
				f.Write(reinterpret_cast<char *>(&vert.z), 4);
				vmapSize += 16;
			}
			fileLen += vmapSize;
			off_T = -4-vmapSize;
			f.SeekO(off_T, wxFromCurrent);
			u32 = MSB4<uint32>(vmapSize);
			f.Write(reinterpret_cast<char *>(&u32), 4);
			f.SeekO(0, wxFromEnd);
		}

		// ===================================================
		// Weight Maps
		// ===================================================
		if (cLyr.Weights.size() > 0){
			wxLogMessage(wxT("Has Weight Maps"));
			for (size_t w=0;w<cLyr.Weights.size();w++){
				f.Write("VMPA", 4);
				u32 = MSB4<uint32>(8);	// We got 2 Paramaters, * 4 Bytes.
				f.Write(reinterpret_cast<char *>(&u32), 4);
				u32 = 4;				// Across Discontinuous Edges UV Sub Type
				f.Write(reinterpret_cast<char *>(&u32), 4);
				u32 = MSB4<uint32>(6);	// Default Gray
				f.Write(reinterpret_cast<char *>(&u32), 4);
				fileLen += 16;

				uint32 vmapSize = 0;
				f.Write("VMAP", 4);
				u32 = MSB4<uint32>(vmapSize);
				f.Write(reinterpret_cast<char *>(&u32), 4);
				fileLen += 8;
				f.Write("WGHT", 4);
				u16 = MSB2(1);		// Always 1
				f.Write(reinterpret_cast<char *>(&u16), 2);
				vmapSize += 6;

				// Weightmap Name, /0 Terminated, evened out.
				wxString name = cLyr.Weights[w].WeightMapName;
				name.Append(wxT('\0'));
				if (fmod((float)name.Len(), 2.0f) > 0)
					name.Append(wxT('\0'));
				f.Write(name, name.Len());
				vmapSize += (uint32)name.Len();

				for (size_t p=0;p<cLyr.Weights[w].PData.size();p++){
					// Point number (VX Format)
					size_t pID = cLyr.Weights[w].PData[p].PointID;
					LW_WriteVX(f,pID,vmapSize);

					// Float value of weight map
					float value = MSB4<float>(cLyr.Weights[w].PData[p].Value);
					f.Write(reinterpret_cast<char *>(&value), 4);
					vmapSize += 4;
				}

				fileLen += vmapSize;
				off_T = -4-vmapSize;
				f.SeekO(off_T, wxFromCurrent);
				u32 = MSB4<uint32>(vmapSize);
				f.Write(reinterpret_cast<char *>(&u32), 4);
				f.SeekO(0, wxFromEnd);
			}
		}

		// --== Polygons ==--
		if (cLyr.Polys.size() > 0){
			g_modelViewer->SetStatusText(wxT("LWO Export: Writing Polygons..."));
			// -------------------------------------------------
			// Polygon Chunk
			// -------------------------------------------------
			f.Write("POLS", 4);
			uint32 polySize = 4;
			u32 = MSB4<uint32>(polySize);
			f.Write(reinterpret_cast<char *>(&u32), 4);
			fileLen += 8; // FACE is handled in the PolySize
			f.Write("FACE", 4);

			for (size_t x=0;x<cLyr.Polys.size();x++){
				PolyChunk PolyData = cLyr.Polys[x].PolyData;
				uint16 nverts = MSB2(PolyData.numVerts);
				polySize += 2;
				f.Write(reinterpret_cast<char *>(&nverts),2);
				for (size_t y=0;y<3;y++){
					LW_WriteVX(f,PolyData.indice[y],polySize);
				}
			}

			off_T = -4-polySize;
			f.SeekO(off_T, wxFromCurrent);
			u32 = MSB4<uint32>(polySize);
			f.Write(reinterpret_cast<char *>(&u32), 4);
			f.SeekO(0, wxFromEnd);
			fileLen += polySize;

			// The PTAG chunk associates tags with polygons. In this case, it identifies which part or surface is assigned to each polygon. 
			// The first number in each pair is a 0-based index into the most recent POLS chunk, and the second is a 0-based 
			// index into the TAGS chunk.

			// NOTE: Every PTAG type needs a seperate PTAG call!

			// -------------------------------------------------
			// Parts PolyTag
			// -------------------------------------------------
			if (Object.PartNames.size() > 0){
				f.Write(wxT("PTAG"), 4);
				uint32 ptagSize = 4;
				u32 = MSB4<uint32>(ptagSize);
				f.Write(reinterpret_cast<char *>(&u32), 4);
				fileLen += 8;
				f.Write(wxT("PART"), 4);

				for (size_t x=0;x<cLyr.Polys.size();x++){
					LWPoly Poly = cLyr.Polys[x];
					LW_WriteVX(f,x,ptagSize);

					u16 = MSB2((uint16)Poly.PartTagID);
					f.Write(reinterpret_cast<char *>(&u16), 2);
					ptagSize += 2;
				}
				fileLen += ptagSize;

				off_T = -4-ptagSize;
				f.SeekO(off_T, wxFromCurrent);
				u32 = MSB4<uint32>(ptagSize);
				f.Write(reinterpret_cast<char *>(&u32), 4);
				f.SeekO(0, wxFromEnd);
			}

			// -------------------------------------------------
			// Surface PolyTag
			// -------------------------------------------------
			if (Object.Surfaces.size() > 0){
				f.Write(wxT("PTAG"), 4);
				uint32 ptagSize = 4;
				u32 = MSB4<uint32>(ptagSize);
				f.Write(reinterpret_cast<char *>(&u32), 4);
				fileLen += 8;
				f.Write(wxT("SURF"), 4);

				for (size_t x=0;x<cLyr.Polys.size();x++){
					LWPoly Poly = cLyr.Polys[x];
					LW_WriteVX(f,x,ptagSize);

					u16 = MSB2((uint16)Poly.SurfTagID);
					f.Write(reinterpret_cast<char *>(&u16), 2);
					ptagSize += 2;
				}
				fileLen += ptagSize;

				off_T = -4-ptagSize;
				f.SeekO(off_T, wxFromCurrent);
				u32 = MSB4<uint32>(ptagSize);
				f.Write(reinterpret_cast<char *>(&u32), 4);
				f.SeekO(0, wxFromEnd);
			}

			// --== Poly-Based Vertex Mapping ==--
			if (cLyr.Polys[0].NormalMapName != wxEmptyString){
				g_modelViewer->SetStatusText(wxT("LWO Export: Writing Poly-Based Vertex Maps..."));
				// ===================================================
				//VMPA		// Vertex Map Parameters, Always Preceeds a VMAP & VMAD. 4bytes: Size, then Num Vars (2) * 4 bytes.
							// UV Sub Type: 0-Linear, 1-Subpatched, 2-Linear Corners, 3-Linear Edges, 4-Across Discontinuous Edges.
							// Sketch Color: 0-12; 6-Default Gray
				// ===================================================
				f.Write("VMPA", 4);
				u32 = MSB4<uint32>(8);	// We got 2 Paramaters, * 4 Bytes.
				f.Write(reinterpret_cast<char *>(&u32), 4);
				u32 = 0;				// Linear SubType
				f.Write(reinterpret_cast<char *>(&u32), 4);
				u32 = MSB4<uint32>(6);	// Default Gray
				f.Write(reinterpret_cast<char *>(&u32), 4);
				fileLen += 16;

				uint32 vmadSize = 0;
				f.Write("VMAD", 4);
				u32 = MSB4<uint32>(vmadSize);
				f.Write(reinterpret_cast<char *>(&u32), 4);
				fileLen += 8;

				f.Write("NORM", 4);
				u16 = MSB2(3);
				f.Write(reinterpret_cast<char *>(&u16), 2);
				vmadSize += 6;
				wxString NormMapName = cLyr.Polys[0].NormalMapName;
				NormMapName += wxT('\0');
				if (fmod((float)NormMapName.length(), 2.0f) > 0)
					NormMapName.Append(wxT('\0'));
				f.Write(NormMapName.data(), NormMapName.length());
				vmadSize += (uint32)NormMapName.length();

				for (size_t x=0;x<cLyr.Polys.size();x++){
					PolyNormal cNorm = cLyr.Polys[x].Normals;
					for (size_t n=0;n<3;n++){
						LW_WriteVX(f,cNorm.indice[n],vmadSize);
						LW_WriteVX(f,cNorm.polygon,vmadSize);

						f32 = MSB4<float>(cNorm.direction[n].x);
						f.Write(reinterpret_cast<char *>(&f32), 4);
						f32 = MSB4<float>(cNorm.direction[n].y);
						f.Write(reinterpret_cast<char *>(&f32), 4);
						f32 = MSB4<float>(cNorm.direction[n].z);
						f.Write(reinterpret_cast<char *>(&f32), 4);
						vmadSize += 12;
					}
				}
				fileLen += vmadSize;

				off_T = -4-vmadSize;
				f.SeekO(off_T, wxFromCurrent);
				u32 = MSB4<uint32>(vmadSize);
				f.Write(reinterpret_cast<char *>(&u32), 4);
				f.SeekO(0, wxFromEnd);
			}
		}
	}

	// --== Clips (Images) ==--
	if (Object.Images.size() > 0){
		g_modelViewer->SetStatusText(wxT("LWO Export: Writing Image file data..."));
		for (size_t x=0;x<Object.Images.size();x++){
			LWClip cImg = Object.Images[x];

			int clipSize = 0;
			f.Write("CLIP", 4);
			u32 = MSB4<uint32>(clipSize);
			f.Write(reinterpret_cast<char *>(&u32), 4);
			fileLen += 8;

			u32 = MSB4<uint32>((uint32)cImg.TagID);
			f.Write(reinterpret_cast<char *>(&u32), 4);
			f.Write("STIL", 4);
			clipSize += 8;

			wxString ImgName = wxEmptyString;
			wxString ImgPath = wxEmptyString;
			if (modelExport_LW_PreserveDir == true){
				ImgName += wxT("Images") + SLASH;
			}
			if (modelExport_PreserveDir == true){
				ImgPath = cImg.Source.Append(SLASH);
			}
			ImgName += wxString(ImgPath + cImg.Filename + wxString(wxT(".tga")));
			ImgName += wxT('\0');
			ImgName.Replace(wxT("\\"),wxT("/"));

			if (fmod((float)ImgName.length(), 2.0f) > 0)
					ImgName.Append(wxT('\0'));

			u16 = MSB2((unsigned short)ImgName.length());
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f.Write(ImgName.data(), ImgName.length());
			clipSize += (2+(int)ImgName.length());

			// update the chunks length
			off_T = -4-clipSize;
			f.SeekO(off_T, wxFromCurrent);
			u32 = MSB4<uint32>(clipSize);
			f.Write(reinterpret_cast<char *>(&u32), 4);
			f.SeekO(0, wxFromEnd);

			fileLen += clipSize;
		}
	}

	// --== Surfaces ==--
	if (Object.Surfaces.size() > 0){
		g_modelViewer->SetStatusText(wxT("LWO Export: Writing Surface/Material data..."));
		for (size_t x=0;x<Object.Surfaces.size();x++){
			LWSurface cSurf = Object.Surfaces[x];
			int off_T;

			uint32 surfaceDefSize = 0;
			f.Write(wxT("SURF"), 4);
			u32 = MSB4<uint32>(surfaceDefSize);
			f.Write(reinterpret_cast<char *>(&u32), 4);
			fileLen += 8;

			wxString surfName = cSurf.Name;
			surfName.Append(wxT('\0'));
			if (fmod((float)surfName.length(), 2.0f) > 0)
				surfName.Append(wxT('\0'));

			surfName.Append(wxT('\0')); // ""
			surfName.Append(wxT('\0')); // Evens out the Code.
			f.Write(surfName.data(), (int)surfName.length());

			surfaceDefSize += (uint32)surfName.length();

			// Surface Attributes
			// COLOUR, size 4, bytes 2
			f.Write(wxT("COLR"), 4);
			u16 = 14; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);

			// value
			f32 = MSB4<float>(cSurf.Surf_Color.x);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			f32 = MSB4<float>(cSurf.Surf_Color.y);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			f32 = MSB4<float>(cSurf.Surf_Color.z);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);

			surfaceDefSize += 20;

			// LUMI
			f.Write(wxT("LUMI"), 4);
			u16 = 6; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = cSurf.Surf_Lum;
			f32 = MSB4<float>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);

			surfaceDefSize += 12;

			// DIFF
			f.Write(wxT("DIFF"), 4);
			u16 = 6; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = cSurf.Surf_Diff;
			f32 = MSB4<float>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);

			surfaceDefSize += 12;

			// SPEC
			f.Write(wxT("SPEC"), 4);
			u16 = 6; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = cSurf.Surf_Spec;
			f32 = MSB4<float>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);

			surfaceDefSize += 12;

			// REFL
			f.Write(wxT("REFL"), 4);
			u16 = 6; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = cSurf.Surf_Reflect;
			f32 = MSB4<float>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);

			surfaceDefSize += 12;

			// TRAN
			f.Write(wxT("TRAN"), 4);
			u16 = 6; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = MSB4<float>(cSurf.Surf_Trans);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);

			surfaceDefSize += 12;

			// GLOSSINESS
			f.Write(wxT("GLOS"), 4);
			u16 = 6; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			// Value
			// Set to 20%, because that seems right.
			f32 = 0.2f;
			f32 = MSB4<float>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);
			surfaceDefSize += 12;

			if (cSurf.hasVertColors == true){
				// VCOL (Vector Colors)
				f.Write(wxT("VCOL"), 4);
				u16 = MSB2(18); // size
				uint16 zero = 0;
				f.Write(reinterpret_cast<char *>(&u16), 2);
				// Unknown Values
				f32 = MSB4<float>(1.0f);
				f.Write(reinterpret_cast<char *>(&f32), 4);
				f.Write(reinterpret_cast<char *>(&zero), 2);
				// RGBA Map Name
				f.Write(wxT("RGBA"), 4);
				f.Write(wxT("Colors"), 6);
				f.Write(reinterpret_cast<char *>(&zero), 2);
				surfaceDefSize += 24;
			}

			// SMAN (Smoothing)
			f.Write(wxT("SMAN"), 4);
			u16 = 4; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			// Smoothing is done in radiens. PI = 180 degree smoothing.
			f32 = (float)PI;
			f32 = MSB4<float>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			surfaceDefSize += 10;

			// RFOP
			f.Write(wxT("RFOP"), 4);
			u16 = 2; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 1;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);

			surfaceDefSize += 8;

			// TROP
			f.Write(wxT("TROP"), 4);
			u16 = 2; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 1;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);

			surfaceDefSize += 8;

			// SIDE
			f.Write(wxT("SIDE"), 4);
			u16 = 2; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 1;
			if (cSurf.isDoubleSided == false){
				u16 = 3;
			}
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);

			surfaceDefSize += 8;

			// NVSK (Exclude from VStack)

			// Normal Map (NORM)
			if (cSurf.NormalMapName != wxEmptyString){
				wxString NormMapName = cSurf.NormalMapName;
				NormMapName += wxT('\0');
				if (fmod((float)NormMapName.length(), 2.0f) > 0)
					NormMapName.Append(wxT('\0'));
				f.Write("NORM", 4);
				u16 = MSB2((uint16)NormMapName.length());
				f.Write(reinterpret_cast<char *>(&u16), 2);
				surfaceDefSize += 6;
				f.Write(NormMapName.data(), NormMapName.length());
				surfaceDefSize += (uint32)NormMapName.length();
			}

			// --
			// BLOK
			uint16 blokSize = 0;
			f.Write(wxT("BLOK"), 4);
			f.Write(reinterpret_cast<char *>(&blokSize), 2);
			surfaceDefSize += 6;

			// IMAP
			f.Write(wxT("IMAP"), 4);
			u16 = 50-8; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 0x80;
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 8;

			// CHAN
			f.Write(wxT("CHAN"), 4);
			u16 = 4; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f.Write(wxT("COLR"), 4);
			blokSize += 10;

			// OPAC
			f.Write(wxT("OPAC"), 4);
			u16 = 8; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = 1.0;
			f32 = MSB4<float>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 14;

			// ENAB
			f.Write(wxT("ENAB"), 4);
			u16 = 2; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 1;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 8;

			// NEGA
			f.Write(wxT("NEGA"), 4);
			u16 = 2; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 0;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 8;
			
			// AXIS
			// This is only needed for Planar images. Everything but ADTs uses UV data.
			if (cSurf.Image_Color.Axis == LW_TEXTUREAXIS_PLANAR){
				f.Write(wxT("AXIS"), 4);
				u16 = 2; // size
				u16 = MSB2(u16);
				f.Write(reinterpret_cast<char *>(&u16), 2);
				u16 = 1;
				u16 = MSB2(u16);
				f.Write(reinterpret_cast<char *>(&u16), 2);
				blokSize += 8;
			}
			// TMAP
			f.Write(wxT("TMAP"), 4);
			u16 = 98; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 6;

			// CNTR
			f.Write(wxT("CNTR"), 4);
			u16 = 14; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = 0.0;
			f.Write(reinterpret_cast<char *>(&f32), 4);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 20;

			// SIZE
			f.Write(wxT("SIZE"), 4);
			u16 = 14; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = 1.0;
			f32 = MSB4<float>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 20;

			// ROTA
			f.Write(wxT("ROTA"), 4);
			u16 = 14; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = 0.0;
			f.Write(reinterpret_cast<char *>(&f32), 4);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 20;

			// FALL
			f.Write(wxT("FALL"), 4);
			u16 = 16; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = 0.0;
			f.Write(reinterpret_cast<char *>(&f32), 4);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 22;

			// OREF
			f.Write(wxT("OREF"), 4);
			u16 = 2; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 8;

			// CSYS
			f.Write(wxT("CSYS"), 4);
			u16 = 2; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 0;
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 8;

			// end TMAP

			// PROJ
			f.Write(wxT("PROJ"), 4);
			u16 = 2; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 5;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 8;

			// AXIS
			f.Write(wxT("AXIS"), 4);
			u16 = 2; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 2;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 8;

			// IMAG
			f.Write(wxT("IMAG"), 4);
			u16 = 2; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = cSurf.Image_Color.ID;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 8;

			// WRAP
			f.Write(wxT("WRAP"), 4);
			u16 = 4; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 1;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 1;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 10;

			// WRPW
			f.Write(wxT("WRPW"), 4);
			u16 = 6; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = 1;
			f32 = MSB4<float>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 12;

			// WRPH
			f.Write(wxT("WRPH"), 4);
			u16 = 6; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = 1;
			f32 = MSB4<float>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			u16 = 0;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 12;

			// VMAP
			f.Write(wxT("VMAP"), 4);
			u16 = 8; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			wxString t = wxT("Texture");
			t.Append(wxT('\0'));
			f.Write(t.data(), t.length());
			blokSize += 14;

			// AAST
			f.Write(wxT("AAST"), 4);
			u16 = 6; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 1;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = 1;
			f32 = MSB4<float>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			blokSize += 12;

			// PIXB
			f.Write(wxT("PIXB"), 4);
			u16 = 2; // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			u16 = 1;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			blokSize += 8;

			// Fix Blok Size
			surfaceDefSize += blokSize;
			off_T = -2-blokSize;
			f.SeekO(off_T, wxFromCurrent);
			u16 = MSB2(blokSize);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f.SeekO(0, wxFromEnd);
			// ================

			// CMNT
			f.Write(wxT("CMNT"), 4);
			wxString comment = cSurf.Comment;
			comment.Append(wxT('\0'));
			if (fmod((float)comment.length(), 2.0f) > 0)
				comment.Append(wxT('\0'));
			u16 = (uint16)comment.length(); // size
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f.Write(comment.data(), comment.length());
			surfaceDefSize += 6 + (uint32)comment.length();

			f.Write(wxT("VERS"), 4);
			u16 = 4;
			u16 = MSB2(u16);
			f.Write(reinterpret_cast<char *>(&u16), 2);
			f32 = 950;	// Surface Compatability Number. 950 = Lightwave 9.5
			f32 = MSB4<int32>(f32);
			f.Write(reinterpret_cast<char *>(&f32), 4);
			surfaceDefSize += 10;
					
			// Fix Surface Size
			fileLen += surfaceDefSize;
			off_T = -4-surfaceDefSize;
			f.SeekO(off_T, wxFromCurrent);
			u32 = MSB4<uint32>(surfaceDefSize);
			f.Write(reinterpret_cast<char *>(&u32), 4);
			f.SeekO(0, wxFromEnd);
		}
	}

	g_modelViewer->SetStatusText(wxT("LWO Export: Cleaning Up..."));
	// Correct File Length
	f.SeekO(4, wxFromStart);
	u32 = MSB4<uint32>(fileLen);
	f.Write(reinterpret_cast<char *>(&u32), 4);
	f.SeekO(0, wxFromEnd);

	f.Close();

	// If we've gotten this far, then the file is good!
	g_modelViewer->SetStatusText(wxT("Lightwave Object Export Successful!"));
	return EXPORT_OKAY;
}


//---------------------------------------------
// Scene Writing Helper Functions
//---------------------------------------------

// Writes an Object's Bone to the scene file.
void WriteLWSceneBone(wxTextOutputStream &fs, LWBones BoneData)
{
	int active = 1;
	if ((BoneData.Active == false)||(BoneData.WeightMap_Name != BoneData.Name)||(BoneData.WeightMap_Name == wxEmptyString)){	// Add Or if weightmap != bone name when we can get weightmap info.
		active = 0;
	}

	fs << wxT("\nAddBone 4") << wxString::Format(wxT("%03x"), BoneData.BoneID) << wxT("0000\nBoneName ") << BoneData.Name << wxT("\n");
	fs << wxT("ShowBone 1 -1 0.376471 0.878431 0.941176\nBoneActive ") << active << wxT("\n");
	fs << wxT("BoneStrength 1\n");
	if (BoneData.WeightMap_Name != wxEmptyString){
		fs << wxT("BoneWeightMapName ") << BoneData.WeightMap_Name << wxT("\nBoneWeightMapOnly ") << (BoneData.WeightMap_Only?1:0) << wxT("\nBoneNormalization ") << (BoneData.WeightMap_Normalize?1:0) << wxT("\n");
	}
	fs << wxT("ScaleBoneStrength 1\n");
	fs << wxT("BoneRestPosition ")<<BoneData.RestPos.x<<wxT(" ")<<BoneData.RestPos.y<<wxT(" ")<<BoneData.RestPos.z<< wxT("\n");
	fs << wxT("BoneRestDirection ")<<BoneData.RestRot.x<<wxT(" ")<<BoneData.RestRot.y<<wxT(" ")<<BoneData.RestRot.z<< wxT("\n");
	fs << wxT("BoneRestLength ")<<BoneData.Length<< wxT("\n");
	fs << wxT("BoneType ") << BoneData.BoneType << wxT("\n");
	fs << wxT("BoneMotion\n");
	
	LW_WriteMotionArray(fs,BoneData.AnimData,9);

	fs << wxT("PathAlignLookAhead 0.033\nPathAlignMaxLookSteps 10\nPathAlignReliableDist 0.001\n");
	fs << wxT("ParentItem ") << BoneData.ParentType;
	if (BoneData.ParentType == LW_ITEMTYPE_BONE){
		fs << wxString::Format(wxT("%03x"),BoneData.ParentID) << wxT("0000");
	}else{	// Assume LW_ITEMTYPE_OBJECT
		fs << wxString::Format(wxT("%07x"),BoneData.ParentID);
	}
	fs << wxT("\nIKInitialState 0\n");
}

// Writes an Object or Null, including Bones, to the scene file.
void WriteLWSceneObject(wxTextOutputStream &fs, LWSceneObj Object)
{
	wxLogMessage(wxT("Writing object information for %s..."), Object.Name.AfterLast(SLASH).c_str());
	if (Object.isNull == true){
		fs << wxT("AddNullObject");
	}else{
		fs << wxT("LoadObjectLayer ") << (int)Object.LayerID;
	}
	fs << wxT(" 1") << wxString::Format(wxT("%07x"),Object.ObjectID) << wxT(" ") << Object.Name << wxT("\nChangeObject 0\n");

	if (Object.ObjectTags != wxEmptyString)
		fs << wxT("// ") << Object.ObjectTags << wxT("\n");
	fs << wxT("ShowObject 7 3\nGroup 0\nObjectMotion\n"); // -1 0.376471 0.878431 0.941176

	LW_WriteMotionArray(fs,Object.AnimData,9);

	fs << wxT("IKInitCustomFrame 0\nGoalStrength 1\nIKFKBlending 0\nIKSoftMin 0.25\nIKSoftMax 0.75\nCtrlPosItemBlend 1\nCtrlRotItemBlend 1\nCtrlScaleItemBlend 1\n\nPathAlignLookAhead 0.033\nPathAlignMaxLookSteps 10\nPathAlignReliableDist 0.001\n");

	if (Object.ParentType > LW_ITEMTYPE_NOPARENT){
		fs << wxT("ParentItem ") << Object.ParentType;
		if (Object.ParentType == LW_ITEMTYPE_BONE){
			fs << wxString::Format(wxT("%03x"),Object.ParentID) << wxT("0000");
		}else{
			fs << wxString::Format(wxT("%07x"),Object.ParentID);
		}
		fs << wxT("\n");
	}
	fs << wxT("IKInitialState 0\nSubPatchLevel 3 3\nShadowOptions 7\n\n");

	// Bones
	if (modelExport_LW_ExportBones == true){
		if (Object.Bones.size() > 0){
			wxLogMessage(wxT("Writing data for %i %s %s..."),Object.Bones.size(),(Object.Bones.size()>1?"bones":"bone"),Object.Name.AfterLast(SLASH).c_str());
			fs << wxT("BoneFalloffType 5\nFasterBones 0\n");
			for (size_t x=0;x<Object.Bones.size();x++){
				WriteLWSceneBone(fs, Object.Bones[x]);
			}
		}else{
			wxLogMessage(wxT("No Bone information to write."));
		}
		fs << wxT("\n");
	}else{
		wxLogMessage(wxT("Export Bones option off. No Bone information written."));
	}
}

// Write a Light to the Scene File
void WriteLWSceneLight(wxTextOutputStream &fs, LWLight Light) //uint32 &lcount, Vec3D LPos, uint32 Ltype, Vec3D Lcolor, float Lintensity, bool useAtten, float AttenEnd, float defRange = 2.5, wxString prefix = wxEmptyString, uint32 ParentNum = -1)
{
	fs << wxT("AddLight 2") << wxString::Format(wxT("%07x"),Light.LightID) << wxT("\n");
	//modelname[0] = toupper(modelname[0]);
	fs << wxT("LightName ") << Light.Name << wxT("\nShowLight 1 -1 0.941176 0.376471 0.941176\nLightMotion\n");
	LW_WriteMotionArray(fs,Light.AnimData,9);

	if (Light.ParentType > LW_ITEMTYPE_NOPARENT){
		fs << wxT("ParentItem ") << Light.ParentType;
		if (Light.ParentType == LW_ITEMTYPE_BONE){
			fs << wxString::Format(wxT("%03x"),Light.ParentID) << wxT("0000");
		}else{
			fs << wxString::Format(wxT("%07x"),Light.ParentID) << wxT("\n");
		}
		fs << wxT("\n");
	}

	// Light Color Reducer
	// Some lights have a color channel greater than 255. This reduces all the colors, but increases the intensity, which should keep it looking the way Blizzard intended.
	Vec3D LColor = Light.Color;
	float LIntensity = Light.Intensity;
	while ((LColor.x > 1.0f)||(LColor.y > 1.0f)||(LColor.z > 1.0f)) {
		LColor.x *= 0.99f;
		LColor.y *= 0.99f;
		LColor.z *= 0.99f;
		LIntensity /= 0.99f;
	}
	fs << wxT("LightColor ") << LColor.x << wxT(" ") << LColor.y << wxT(" ") << LColor.z << wxT("\nLightIntensity ") << LIntensity << wxT("\n");

	// Process Light type & output!
	switch (Light.LightType) {
		// Omni Lights
		case 1:
		default:
			// Default to an Omni (Point) light.
			fs << wxT("LightType ") << LW_LIGHTTYPE_POINT << wxT("\n");

			if (Light.FalloffRadius > 0.0f) {
				// Use Inverse Distance for the default Light Falloff Type. Should better simulate WoW Lights, until I can write a WoW light plugin for Lightwave...
				fs << wxT("LightFalloffType 2\nLightRange ") << Light.FalloffRadius << wxT("\n");
			}else{
				// Default to these settings, which look pretty close...
				fs << wxT("LightFalloffType 2\nLightRange 2.5\n");
			}
			fs << wxT("ShadowType 1\nShadowColor 0 0 0\n");
			WriteLWScenePlugin(fs,wxT("LightHandler"),1,wxT("PointLight"));
	}
	fs << wxT("\n");
}


//---------------------------------------------
// --== Scene Writing Function ==--
//---------------------------------------------

// Write Lightwave Scene data to a file.
// Currently incomplete.
size_t WriteLWScene(LWScene *SceneData){
	wxLogMessage(wxT("Export Lightwave Scene Function running."));
	g_modelViewer->SetStatusText(wxT("Preparing Scene Export Settings..."));

	// Fail if there is nothing to write
	if ((SceneData->Objects.size() == 0) && (SceneData->Lights.size() == 0) && (SceneData->Cameras.size() == 0)){
		wxString errormsg = wxT("No Scene Data found. Unable to write scene file.");
		wxLogMessage(errormsg);
		wxMessageBox(errormsg,wxT("Scene Export Failure"));
		return EXPORT_ERROR_NO_DATA;
	}
	// Fail if Always Write is off, and all the other options are off.
	if ((modelExport_LW_AlwaysWriteSceneFile==false) 
		&& (modelExport_LW_ExportLights == false) && (modelExport_LW_ExportDoodads == false) 
		&& (modelExport_LW_ExportCameras == false) && (modelExport_LW_ExportBones == false))
	{
		wxString errormsg = wxT("Global variables don't allow for scene generation. Unable to write scene file.");
		wxLogMessage(errormsg);
		wxMessageBox(errormsg,wxT("Scene Export Failure"));
		return EXPORT_ERROR_SETTINGS_WRONG;
	}

	// Open Scene filename
	g_modelViewer->SetStatusText(wxT("Opening Scene file for writing..."));
	wxString SceneName = SceneData->FilePath + SceneData->FileName;
	wxFFileOutputStream output(SceneName);
	if (!output.IsOk()){
		wxMessageBox(wxT("Unable to open the scene file for exporting."),wxT("Scene Export Failure"));
		wxLogMessage(wxT("Error: Unable to open file \"%s\". Could not export the scene."), SceneName.c_str());
		return EXPORT_ERROR_FILE_ACCESS;
	}
	wxTextOutputStream fs(output,wxEOL_DOS);

	wxLogMessage(wxT("Opened %s for writing..."),SceneName.c_str());
	SceneName = SceneName.AfterLast(SLASH);

	/*
	Lightwave Scene Data Order:
	
	Format Data
	Render Range Data
	Objects
		Bones
	Global Light Data
	Lights
	Cameras
	GlobalSettings
	Antialasing Settings
	Backdrop
	Fog Data
	Render Settings
	View Configuration
	Options
	Current Items

	*/
	
	g_modelViewer->SetStatusText(wxT("Writing Scene Data..."));

	// File Top
	fs << wxT("LWSC\n");
	fs << wxT("5\n\n"); // This is a version-compatibility number...
	
	// Scene Length Data
	size_t Frame_First = SceneData->FirstFrame;
	size_t Frame_Last = SceneData->LastFrame;
	if ((Frame_First == 0)&&(Frame_Last == 1)){
		Frame_First = 0;
		Frame_Last = 60;
	}
	fs << wxT("RenderRangeType 0\nFirstFrame ") << (int)Frame_First << wxT("\nLastFrame ") << (int)Frame_Last << wxT("\nFrameStep 1\nRenderRangeObject 0\nRenderRangeArbitrary ") << (int)Frame_First << wxT("-") << (int)Frame_Last << wxT("\n");
	fs << wxT("PreviewFirstFrame ") << (int)Frame_First << wxT("\nPreviewLastFrame ") << (int)Frame_Last << wxT("\nPreviewFrameStep 1\nCurrentFrame ") << (int)Frame_First << wxT("\nFramesPerSecond 30\nChangeScene 0\n\n");

	// Objects & Bones
	g_modelViewer->SetStatusText(wxT("Writing Scene Objects & Bones..."));
	size_t numObj = SceneData->Objects.size();
	if (numObj>0)
		wxLogMessage(wxT("Writing information for %i %s and their bones..."),numObj,(numObj>1?wxT("objects"):wxT("object")));
	for (size_t o=0;o<numObj;o++){
		WriteLWSceneObject(fs,SceneData->Objects[o]);
	}

	// Global Light Options
	g_modelViewer->SetStatusText(wxT("Writing Scene Light information..."));
	if (!SceneData->AmbientIntensity){
		SceneData->AmbientIntensity = 0.5f;		// Default to 50%
	}
	fs << wxT("\nAmbientColor 1 1 1\nAmbientIntensity ") << SceneData->AmbientIntensity << wxT("\nDoubleSidedAreaLights 1\nRadiosityType 2\nRadiosityInterpolated 1\nRadiosityTransparency 0\nRadiosityIntensity 1\nRadiosityTolerance 0.2928932\nRadiosityRays 64\nSecondaryBounceRays 16\nRadiosityMinPixelSpacing 4\nRadiosityMaxPixelSpacing 100\nRadiosityMultiplier 1\nRadiosityDirectionalRays 0\nRadiosityUseGradients 0\nRadiosityUseBehindTest 1\nBlurRadiosity 1\nRadiosityFlags 0\nRadiosityCacheModulus 1\nRadiosityCacheFilePath Radiosity/radiosity.cache\nPixelFilterForceMT 0\n\n");

	// Lights
	if (modelExport_LW_ExportLights == true){
		if (SceneData->Lights.size()>0){
			wxLogMessage(wxT("Writing light information..."));
			for (size_t l=0;l<SceneData->Lights.size();l++){
				WriteLWSceneLight(fs,SceneData->Lights[l]);
			}
		}else{
			wxLogMessage(wxT("No Light information to write."));
		}
	}else{
		wxLogMessage(wxT("Export Lights option off. No Light information written."));
	}

	// Cameras
	g_modelViewer->SetStatusText(wxT("Writing Scene Camera placement..."));
	if (modelExport_LW_ExportCameras == true){
		if (SceneData->Cameras.size()>0){
			wxLogMessage(wxT("Writing camera information..."));
			for (size_t c=0;c<SceneData->Cameras.size();c++){
				wxLogMessage(wxT("Writing data for Camera %02i..."),c);
				LWCamera *cam = &SceneData->Cameras[c];
				fs << wxT("AddCamera 3") << wxString::Format(wxT("%07x"),cam->CameraID) << wxT("\nCameraName Camera\nShowCamera 1 -1 0.125490 0.878431 0.125490\nCameraMotion\n");
				LW_WriteMotionArray(fs, cam->AnimData, 6);
				if (cam->ParentType > LW_ITEMTYPE_NOPARENT){
					fs << wxT("ParentItem ") << cam->ParentType << wxString::Format(wxT("%07x"),cam->ParentID) << wxT("\n");
				}
				fs << wxT("IKInitCustomFrame 0\nGoalStrength 1\nIKFKBlending 0\nIKSoftMin 0.25\nIKSoftMax 0.75\nCtrlPosItemBlend 1\nCtrlRotItemBlend 1\nCtrlScaleItemBlend 1\n\n");
				fs << wxT("HController 1\nPController 1\nPathAlignLookAhead 0.033\nPathAlignMaxLookSteps 10\nPathAlignReliableDist 0.001\n");
				fs << wxT("TargetItem 1")<<wxString::Format(wxT("%07x"),cam->TargetObjectID)<<wxT("\n");
				fs << wxT("ZoomFactor ")<<(cam->FieldOfView*3.6)<<wxT("\nZoomType 1\n");
				WriteLWScenePlugin(fs,wxT("CameraHandler"),1,wxT("Perspective"));	// Make the camera a Perspective camera
				fs << wxT("\n");
			}
		}else{
			wxLogMessage(wxT("No Camera information to write."));
		}
	}else{
		wxLogMessage(wxT("Export Cameras option off. No Camera information written."));
	}

	// Successfully wrote the scene file!
	wxLogMessage(wxT("Closing Scene File..."));
	output.Close();
	
	//wxLogMessage(wxT("Destorying output objects..."));
	//output.~wxFFileOutputStream();
	//fs.~wxTextOutputStream();

	wxLogMessage(wxT("Lightwave Scene export successful."));
	g_modelViewer->SetStatusText(wxT("Lightwave Scene export successful."));
	return EXPORT_OKAY;
}

//---------------------------------------------
// --== Object Info Gathering Functions ==--
//---------------------------------------------

// Gather M2 Data
LWObject GatherM2forLWO(Attachment *att, Model *m, bool init, wxString fn, LWScene &scene, bool announce){
	g_modelViewer->SetStatusText(wxT("Gathering M2 Data for Lightwave Exporter..."));
	LWObject Object;
	if (!m){
		return Object;
		Object.~LWObject();
	}

	wxString filename = fn;
	wxString scfilename = fn.BeforeLast('.') + wxT(".lws");

	if (modelExport_LW_PreserveDir == true){
		wxString Path, Name;

		// Object
		Path << filename.BeforeLast(SLASH);
		Name << filename.AfterLast(SLASH);
		MakeDirs(Path,wxT("Objects"));
		filename.Empty();
		filename << Path << SLASH << wxT("Objects") << SLASH << Name;
	}
	if (m->modelType != MT_CHAR){
		if (modelExport_PreserveDir == true){
			wxString Path1, Path2, Name;

			// Objects
			Path1 << filename.BeforeLast(SLASH);
			Name << filename.AfterLast(SLASH);
			Path2 << m->name.BeforeLast(SLASH);
			MakeDirs(Path1,Path2);
			filename.Empty();
			filename << Path1 << SLASH << Path2 << SLASH << Name;
		}
	}

	LWSceneObj SceneObj(filename, LW_ObjCount.GetPlus(), 0, LW_ITEMTYPE_NOPARENT);

	Object.SourceType = wxT("M2");
	if (announce == true)
		LogExportData(wxT("LWO"),m->modelname,filename);

	size_t SurfCounter = 0;
	ssize_t cAnim = 0;
	size_t cFrame = 0;
	bool vertMsg = false;

	if ((m->animated == true) && (init == false)){
		cAnim = m->currentAnim;
		cFrame = m->animManager->GetFrame();
	}

	// Main Object
	LWLayer Layer;
	Layer.Name = m->name.AfterLast(MPQ_SLASH).BeforeLast('.');
	Layer.ParentLayer = -1;
	std::vector<wxString> surfnamearray;

	/*
	Brilliant idea! Place all the models into a vector list, then parse the list. Should reduce code, specifically remove duplicate code for attached objects!
	Just have to find a way to deal with any positioning problems...

	struct ListModel{
		Model *m,
		size_t ID;
		size_t ParentID;
	}

	std::vector<ListModel> mlist;
	ListModel a;
	a.m = m;
	a.ID = 1;
	a.ParentID = 0;
	mlist.push_back(a);
	size_t counter = 1;
	if (att != NULL){
		g_modelViewer->SetStatusText(wxT("Processing Attached Model Files..."));
		// Have yet to find an att->model, so skip it until we do.
		//if (att->model){
		//}
		for (size_t c=0; c<att->children.size(); c++) {
			Attachment *att2 = att->children[c];
			for (size_t j=0; j<att2->children.size(); j++) {
				Model *mAttChild = static_cast<Model*>(att2->children[j]->model);
				counter++;

				if (mAttChild){
					ListModel ac;
					ac.m = mAttChild;
					ac.ID = counter;
					ac.ParentID = a.ID;
					mlist.push_back(mAttChild);
				}
			}
		}
	}

	// Parse the model list...

	*/

	// Bounding Box for the Layer
	/*if (m->bounds[0]){
		Layer.BoundingBox1 = m->bounds[0];
		Layer.BoundingBox2 = m->bounds[1];
	}*/

	wxLogMessage(wxT("M2 Texture List:"));
	for (size_t x=0;x<m->TextureList.size();x++){
		wxLogMessage(wxT("Image %i: %s"),x,m->TextureList[x].c_str());
	}

	// Build Part Names
	// Seperated from the rest of the build for various reasons.
	g_modelViewer->SetStatusText(wxT("Building Part Names..."));
	wxLogMessage(wxT("Building Part Names..."));
	for (size_t i=0; i<m->passes.size(); i++) {
		ModelRenderPass &p = m->passes[i];
		if (p.init(m)){
			// Main Model
			int g = p.geoset;
			bool isFound = false;
			wxString partName;
			
			// Part Names
			int mesh = m->geosets[g].id / 100;
			if (m->modelType == MT_CHAR && mesh < 19 && meshes[mesh] != wxEmptyString){
				partName = wxString::Format(wxT("Geoset %03i - %s"),g,meshes[mesh].c_str());
			}else{
				partName = wxString::Format(wxT("Geoset %03i"),g);
			}
			for (size_t x=0;x<Object.PartNames.size();x++){
				if (Object.PartNames[x] == partName){
					isFound = true;
					break;
				}
			}
			if (isFound == false)
				Object.PartNames.push_back(partName);
		}
	}
	// Parts for Attached Objects
	if (att != NULL){
		if (att->model){
			wxLogMessage(wxT("Att Model found."));
		}
		for (size_t c=0; c<att->children.size(); c++) {
			Attachment *att2 = att->children[c];
			for (size_t j=0; j<att2->children.size(); j++) {
				Model *mAttChild = static_cast<Model*>(att2->children[j]->model);

				if (mAttChild){
					wxLogMessage(wxT("AttChild Model found."));
					for (size_t i=0; i<mAttChild->passes.size(); i++) {
						ModelRenderPass &p = mAttChild->passes[i];

						if (p.init(mAttChild)) {
							bool isFound = false;
							wxString partName;

							int thisslot = att2->children[j]->slot;
							if (thisslot < 15 && slots[thisslot]!=wxEmptyString){
								partName = wxString::Format(wxT("Child %02i - %s"),j,slots[thisslot].c_str());
							}else{
								partName = wxString::Format(wxT("Child %02i - Slot %02i"),j,att2->children[j]->slot);
							}

							for (size_t x=0;x<Object.PartNames.size();x++){
								if (Object.PartNames[x] == partName){
									isFound = true;
									break;
								}
							}
							if (isFound == false)
								Object.PartNames.push_back(partName);
						}
					}
				}
			}
		}
	}

	// Process Passes
	wxLogMessage(wxT("Processing Model File..."));
	g_modelViewer->SetStatusText(wxT("Processing Model File..."));
	for (size_t i=0; i<m->passes.size(); i++) {
		wxLogMessage(wxT("Processing model, pass %i of %i..."),i,m->passes.size());
		ModelRenderPass &p = m->passes[i];
		if (p.init(m)){
			// Main Model
			int g = p.geoset;
			size_t partID = i;
			size_t *Vert2Point = new size_t[p.vertexEnd];
			float Surf_Diff = 1.0f;
			float Surf_Lum = 0.0f;
			bool doublesided = (p.cull?true:false);

			wxString partName, matName;
			
			// Part Names
			int mesh = m->geosets[g].id / 100;
			if (m->modelType == MT_CHAR && mesh < 19 && meshes[mesh] != wxEmptyString){
				partName = wxString::Format(wxT("Geoset %03i - %s"),g,meshes[mesh].c_str());
			}else{
				partName = wxString::Format(wxT("Geoset %03i"),g);
			}
			for (size_t x=0;x<Object.PartNames.size();x++){
				if (Object.PartNames[x] == partName){
					partID = x;
					break;
				}
			}

			// Surface Name
			matName = m->TextureList[p.tex].AfterLast(SLASH).BeforeLast(wxT('.'));
			if (matName.Len() == 0)
				matName = wxString::Format(wxT("Material_%03i"), p.tex);
/*
			if (p.useSpec == true){
				matName = matName + wxT("_Spec");
			}

			if (p.trans == true){
				matName = matName + wxT("_Trans");
			}
*/
			// If Luminous...
			if (p.unlit == true) {
				wxLogMessage(wxT("Surface is Luminous..."));
				Surf_Diff = 0.0f;
				Surf_Lum = 1.0f;
				// Add Lum, just in case there's a non-luminous surface with the same name.
				matName = matName + wxT("_Lum");
			}

			// If Doublesided
			if (doublesided == false) {
				wxLogMessage(wxT("Surface is Double-sided..."));
				matName = matName + wxT("_Dbl");
			}

			wxLogMessage(wxT("Processing Texture & image names..."));

			// Add Images to Model
			LWClip ClipImage;
			// Image Filename
			wxString Texture = m->TextureList[p.tex];
			wxString TexturePath = Texture.BeforeLast(MPQ_SLASH);
			wxString texName = Texture.AfterLast(MPQ_SLASH).BeforeLast(wxT('.'));

			if (m->modelType == MT_CHAR){
				wxString charname = filename.AfterLast(SLASH).BeforeLast(wxT('.')) + wxT("_");
				if (texName.Find(MPQ_SLASH) != wxNOT_FOUND){
					texName = charname + Texture.AfterLast(MPQ_SLASH).BeforeLast(wxT('.'));
					TexturePath = wxT("");
				}
				if (texName.Find(wxT("Body")) != wxNOT_FOUND){
					texName = charname + wxT("Body");
				}
			}
			//wxLogMessage(wxT("Texture: %s\n\tTexPath: %s\n\ttexName: %s"),Texture,TexturePath,texName);

			// Image Data
			ClipImage.Filename = texName;
			ClipImage.Source = TexturePath;
			ClipImage.TagID = (uint32)Object.PartNames.size() + SurfCounter;
			Object.Images.push_back(ClipImage);
			LWSurf_Image SurfImage_Color(ClipImage.TagID, LW_TEXTUREAXIS_UV, 0, 0);

			// ExportName = Base Path + Texture name without paths.
			wxString ExportName = wxString(fn, wxConvUTF8).BeforeLast(SLASH) + SLASH + texName;
			//wxLogMessage(wxT("PrePath ExportName: %s, fn Path: %s"),ExportName,wxString(fn, wxConvUTF8).BeforeLast(SLASH));
			if (modelExport_LW_PreserveDir == true){
				wxString Path, Name;

				Path << wxString(fn, wxConvUTF8).BeforeLast(SLASH);
				Name << ExportName.AfterLast(SLASH);

				MakeDirs(Path,wxT("Images"));

				ExportName.Empty();
				ExportName << Path << SLASH<<wxT("Images")<<SLASH << Name;
			}
			if ((modelExport_PreserveDir == true)&&(TexturePath.Len() > 0)){
				wxString Path1, Path2, Name;
				Path1 << ExportName.BeforeLast(SLASH);
				Name << texName;
				Path2 << TexturePath;

				MakeDirs(Path1,Path2);

				ExportName.Empty();
				ExportName << Path1 << SLASH << Path2 << SLASH << Name;
			}
			ExportName << wxT(".tga");
			//wxLogMessage(wxT("Image ExportName: %s"),ExportName);
			SaveTexture(ExportName);
			//SaveTexture2(ClipImage.Filename,ClipImage.Source,wxString(wxT("LWO")),wxString(wxT("tga")));

			wxLogMessage(wxT("Building Surface..."));
			LWSurface Surface(matName,m->TextureList[p.tex],SurfImage_Color,LWSurf_Image(),LWSurf_Image(),Vec3D(1,1,1),Surf_Diff,Surf_Lum,doublesided);

			// Points
			wxLogMessage(wxT("Processing Vertex point data..."));
			for (size_t v=p.vertexStart; v<p.vertexEnd; v++) {
				// --== Point Data ==--
				LWPoint Point;
				uint32 pointnum = (uint32)Layer.Points.size();

				// Points
				//wxLogMessage(wxT("Processing Points..."));
				Vec3D vert;
				if ((m->animated == true) && (init == false) && (m->vertices)) {
					if (vertMsg == false){
						wxLogMessage(wxT("Using Verticies"));
						vertMsg = true;
					}
					vert = m->vertices[v];
				} else {
					if (vertMsg == false){
						wxLogMessage(wxT("Using Original Verticies"));
						vertMsg = true;
					}
					vert = m->origVertices[v].pos;
				}
				vert = Vec3D(vert.x,vert.y,-vert.z);	// Fixes X flipped verts

				Point.PointData = vert;
				Point.UVData = m->origVertices[v].texcoords;	// UV Data
				
				// Vertex Colors
				//wxLogMessage(wxT("Checking for Vertex Colors..."));	
				/*
				if (m->colors[p.color].color.uses(cAnim) == true){
					wxLogMessage(wxT("Processing Vertex Colors..."));	
					LWVertexColor vc;
					vc.a = 1.0f;
					//Currently bugs out...
					//if (m->colors[p.color].opacity.uses(cAnim) == true)
					//	vc.a = m->colors[p.color].opacity.getValue(cAnim,cFrame);
					vc.r = 1.0f;
					vc.g = 1.0f;
					vc.b = 1.0f;
					Point.VertexColors = vc;
				}
				*/
				
				//wxLogMessage(wxT("Recording Point data..."));
				Vert2Point[v] = pointnum;
				Layer.Points.push_back(Point);
			}

			// Polys
			wxLogMessage(wxT("Processing Polygon data..."));
			for (size_t k=0; k<p.indexCount; k+=3) {
				// --== Polygon Data ==--	
				LWPoly Poly;
				Poly.PolyData.numVerts = 3;
				for (ssize_t x=0;x<3;x++){
					// Modify to Flip Polys
					int mod = 0;
					if (x == 1){
						mod = 1;
					}else if (x == 2){
						mod = -1;
					}

					// Polygon Indice
					size_t a = p.indexStart + k + x + mod;
					size_t b = m->IndiceToVerts[a];
					Poly.PolyData.indice[x] = Vert2Point[b];

					// Normal Indice
					/*
					Poly.Normals.indice[x] = Vert2Point[b];
					Poly.Normals.direction[x].x = m->origVertices[b].normal.x;
					Poly.Normals.direction[x].y = m->origVertices[b].normal.z;
					Poly.Normals.direction[x].z = -(m->origVertices[b].normal.y);
					*/
				}
				Poly.PartTagID = partID;
				Poly.SurfTagID = (uint32)Object.PartNames.size() + SurfCounter;
				//Poly.Normals.polygon = (uint32)Layer.Polys.size();
				wxString NormName = wxEmptyString; //Layer.Name + wxString(wxT("_NormalMap"));
				Poly.NormalMapName = NormName;
				Surface.NormalMapName = NormName;
				Layer.Polys.push_back(Poly);
			}
			Object.Surfaces.push_back(Surface);
			SurfCounter++;
		}
	}
	
	// Functions that should only be called if there are bones...
	if (m->header.nBones > 0) {
		// Build Bone Data for Weight Maps
		wxLogMessage(wxT("Processing Bone data..."));
		for (size_t x=0;x<m->header.nBones;x++){
			LWWeightMap wMap;

			// Weight Map Name = Bone Name
			wxString bone_name = wxString::Format(wxT("Bone_%03i"), x);
			for (size_t j=0; j<BONE_MAX; ++j) {
				if (m->keyBoneLookup[j] == (int16)x) {
					bone_name = Bone_Names[j];
					break;
				}
			}

			wMap.WeightMapName = bone_name;
			wMap.BoneID = x;
			Layer.Weights.push_back(wMap);
		};

		// Fill Weightmap Data
		wxLogMessage(wxT("Processing Bone Limits..."));
		size_t numv = m->header.nVertices;
		LWWeightMap wMap;
		for (size_t i=0; i<numv; i++) {
			ModelVertex& vertex = m->origVertices[i];
			for (size_t j=0;j<4; j++) {
				if ((vertex.bones[j] == 0) || (vertex.weights[j] == 0))
					continue;

				LWWeightInfo a;
				a.Value = (float)(vertex.weights[j] / 255.0);
				a.PointID = i;
				Layer.Weights[vertex.bones[j]].PData.push_back(a);
			}
		}
	}

	// --== Attachments ==--
	if (att != NULL){
		g_modelViewer->SetStatusText(wxT("Processing Attached Model Files..."));
		wxLogMessage(wxT("Processing Attached Model Files..."));
		/* Have yet to find an att->model, so skip it until we do.
		if (att->model){
		} */
		for (size_t c=0; c<att->children.size(); c++) {
			Attachment *att2 = att->children[c];
			for (size_t j=0; j<att2->children.size(); j++) {
				Model *mAttChild = static_cast<Model*>(att2->children[j]->model);

				if (mAttChild){
					int boneID = -1;
					Model *mParent = NULL;

					wxLogMessage(wxT("Attached Child Model: %s"),mAttChild->name.c_str());
					wxLogMessage(wxT("Texture List:"));
					for (size_t x=0;x<mAttChild->TextureList.size();x++){
						wxLogMessage(wxT("Image %i: %s"),x,mAttChild->TextureList[x].c_str());
					}

					if (att2->parent) {
						mParent = static_cast<Model*>(att2->children[j]->parent->model);
						if (mParent)
							boneID = mParent->attLookup[att2->children[j]->id];
					}

					// Model Movement, Roation & Scale Data
					Vec3D mPos(0,0,0);
					Quaternion mRot(Vec4D(0,0,0,0));
					Vec3D mScale(1,1,1);

					Vec3D Seraph(1,1,1);
					Quaternion Niobe(Vec4D(0,0,0,0));

					if (boneID>-1) {
						Bone cbone = mParent->bones[mParent->atts[boneID].bone];
						Matrix mat = cbone.mat;
						Matrix rmat = cbone.mrot;

						if (init == true){
							mPos = mParent->atts[boneID].pos;
							mScale = cbone.scale.getValue(ANIMATION_HANDSCLOSED,0);
							mRot = cbone.rot.getValue(ANIMATION_HANDSCLOSED,0);
						}else{
							// Rotations aren't working correctly... Not sure why.
							rmat.quaternionRotate(cbone.rot.getValue(cAnim,cFrame));
							mat.scale(cbone.scale.getValue(cAnim,cFrame));
							mat.translation(cbone.transPivot);

							mPos.x = mat.m[0][3];
							mPos.y = mat.m[1][3];
							mPos.z = mat.m[2][3];

							mScale = cbone.scale.getValue(cAnim,(uint32)cFrame);
							mRot = rmat.GetQuaternion();
						}
						if (mScale.x == 0 && mScale.y == 0 && mScale.z == 0){
							mScale = Vec3D(1,1,1);
						}
					}
					/*
					wxLogMessage(wxT("mRot: X: %f, Y: %f, Z: %f, W: %f"),mRot.x,mRot.y,mRot.z,mRot.w);
					wxLogMessage(wxT("mPos: X: %f, Y: %f, Z: %f"),mPos.x,mPos.y,mPos.z);
					wxLogMessage(wxT("mScale: X: %f, Y: %f, Z: %f"),mScale.x,mScale.y,mScale.z);
					*/
					for (size_t i=0; i<mAttChild->passes.size(); i++) {
						ModelRenderPass &p = mAttChild->passes[i];

						if (p.init(mAttChild)) {
							size_t partID = i;
							size_t *Vert2Point = new size_t[p.vertexEnd];
							float Surf_Diff = 1.0f;
							float Surf_Lum = 0.0f;
							wxString partName, matName;
							bool doublesided = (p.cull?true:false);
							
							size_t thisslot = att2->children[j]->slot;

							// Part Names
							if (thisslot < WXSIZEOF(slots) && slots[thisslot]!=wxEmptyString){
								partName = wxString::Format(wxT("Child %02i - %s"),j,slots[thisslot].c_str());
							}else{
								partName = wxString::Format(wxT("Child %02i - Slot %02i"),j,att2->children[j]->slot);
							}
							for (size_t x=0;x<Object.PartNames.size();x++){
								if (Object.PartNames[x] == partName){
									partID = x;
									break;
								}
							}

							// Surface Name
							matName = mAttChild->TextureList[p.tex].AfterLast(MPQ_SLASH).BeforeLast(wxT('.'));
							if (thisslot < WXSIZEOF(slots) && slots[thisslot]!=wxEmptyString){
								if (matName != wxEmptyString){
									matName = wxString::Format(wxT("%s - %s"),slots[thisslot].c_str(),matName.c_str());
								}else {
									matName = wxString::Format(wxT("%s - Material %02i"),slots[thisslot].c_str(),p.tex);
								}
							}


							if (matName.Len() == 0)
								matName = wxString::Format(wxT("Child %02i - Material %03i"), j, p.tex);

							// If Luminous...
							if (p.unlit == true) {
								Surf_Diff = 0.0f;
								Surf_Lum = 1.0f;
								// Add Lum, just in case there's a non-luminous surface with the same name.
								matName = matName + wxT("_Lum");
							}

							// If Doublesided
							if (doublesided == false) {
								matName = matName + wxT("_Dbl");
							}

							// Add Images to Model
							LWClip ClipImage;
							// Image Filename
							wxString Texture = mAttChild->TextureList[p.tex];
							wxString TexturePath = Texture.BeforeLast(MPQ_SLASH);
							wxString texName = Texture.AfterLast(MPQ_SLASH).BeforeLast(wxT('.'));

							//wxLogMessage("Texture: %s\n\tTexurePath: %s\n\ttexName: %s",Texture,TexturePath,texName);

							// Image Data
							ClipImage.Filename = texName;
							ClipImage.Source = TexturePath;
							ClipImage.TagID = (uint32)Object.PartNames.size() + SurfCounter;
							Object.Images.push_back(ClipImage);
							LWSurf_Image SurfImage_Color(ClipImage.TagID, LW_TEXTUREAXIS_UV, 0, 0);

							wxString ExportName = wxString(fn, wxConvUTF8).BeforeLast(SLASH) + SLASH + texName;
							if (modelExport_LW_PreserveDir == true){
								wxString Path, Name;

								Path << wxString(fn, wxConvUTF8).BeforeLast(SLASH);
								Name << ExportName.AfterLast(SLASH);

								MakeDirs(Path,wxT("Images"));

								ExportName.Empty();
								ExportName << Path << SLASH<<wxT("Images")<<SLASH << Name;
							}
							if (modelExport_PreserveDir == true){
								wxString Path1, Path2, Name;
								Path1 << ExportName.BeforeLast(SLASH);
								Name << texName.AfterLast(SLASH);
								Path2 << TexturePath;

								MakeDirs(Path1,Path2);

								ExportName.Empty();
								ExportName << Path1 << SLASH << Path2 << SLASH << Name;
							}
							ExportName <<  wxT(".tga");

							SaveTexture(ExportName);

							LWSurface Surface(matName,Texture.BeforeLast('.'),SurfImage_Color,LWSurf_Image(),LWSurf_Image(),Vec3D(1,1,1),Surf_Diff,Surf_Lum,doublesided);
							Object.Surfaces.push_back(Surface);

							// Points
							for (size_t v=p.vertexStart; v<p.vertexEnd; v++) {
								// --== Point Data ==--
								LWPoint Point;
								uint32 pointnum = (uint32)Layer.Points.size();

								// Points
								Vec3D vert;
								if ((init == false)&&(mAttChild->vertices)) {
									vert = mAttChild->vertices[v];
								} else {
									vert = mAttChild->origVertices[v].pos;
								}
								Matrix Neo;
								
								Neo.translation(vert);							// Set Original Position
								Neo.quaternionRotate(Niobe);					// Set Original Rotation
								Neo.scale(Seraph);								// Set Original Scale

								Neo *= Matrix::newTranslation(mPos);			// Apply New Position
								Neo *= Matrix::newQuatRotate(mRot);				// Apply New Rotation
								Neo *= Matrix::newScale(mScale);				// Apply New Scale

								Vec3D mVert = Neo * vert;
								mVert = Vec3D(mVert.x,mVert.y,-mVert.z);		// Fixes X flipped verts

								Point.PointData = mVert;
								Point.UVData = mAttChild->origVertices[v].texcoords;	// UV Data
								// Weight Data NYI

								Vert2Point[v] = pointnum;
								Layer.Points.push_back(Point);
							}

							// Polys
							for (size_t k=0; k<p.indexCount; k+=3) {
								// --== Polygon Data ==--	
								LWPoly Poly;
								Poly.PolyData.numVerts = 3;
								for (ssize_t x=0;x<3;x++){
									// Modify to Flip Polys
									int mod = 0;
									if (x == 1){
										mod = 1;
									}else if (x == 2){
										mod = -1;
									}

									// Polygon Indice
									size_t a = p.indexStart + k + x + mod;
									size_t b = mAttChild->IndiceToVerts[a];
									Poly.PolyData.indice[x] = Vert2Point[b];

									// Normal Indice
									/*Poly.Normals.indice[x] = Vert2Point[b];
									Poly.Normals.direction[x].x = mAttChild->normals[b].x;
									Poly.Normals.direction[x].y = mAttChild->normals[b].z;
									Poly.Normals.direction[x].z = -mAttChild->normals[b].y;*/
								}
								Poly.PartTagID = partID;
								Poly.SurfTagID = (uint32)Object.PartNames.size() + SurfCounter;
								//Poly.Normals.polygon = (uint32)Layer.Polys.size();
								Poly.NormalMapName = wxEmptyString; //Layer.Name + wxString(wxT("_NormalMap"));
								Layer.Polys.push_back(Poly);
							}
							SurfCounter++;
						}
					}
					
					// Fill Weightmap Data
					size_t numv = mAttChild->header.nVertices;
					LWWeightMap wMap;
					for (size_t i=0; i<numv; i++) {
						ModelVertex& vertex = mAttChild->origVertices[i];
						for (size_t j=0;j<4; j++) {
							if ((vertex.bones[j] == 0) && (vertex.weights[j] == 0))
								continue;

							LWWeightInfo a;
							a.Value = (float)(vertex.weights[j] / 255.0);
							a.PointID = i;
							Layer.Weights[vertex.bones[j]].PData.push_back(a);
						}
					}
				}
			}
		}
	}

	// Push the finished layer into the objects.
	Object.Layers.push_back(Layer);

	// --== Scene Data ==--
	g_modelViewer->SetStatusText(wxT("Gathering Scene Data..."));
	wxLogMessage(wxT("Gathering Scene Data..."));
	int modelExport_LW_ExportAnim = 0;	// Temp here until I build the interface.
	//bool animExportError = false;
	// Object Placement
	AnimationData a(animValue0,animValue0,animValue1);
	SceneObj.AnimData = a;

	if (scene.FileName != wxEmptyString) {
		// Bones
		if (m->header.nBones > 0){
			g_modelViewer->SetStatusText(wxT("Gathering Bones..."));
			wxLogMessage(wxT("Model has %i %s."),m->header.nBones,(m->hasCamera?wxT("bones"):wxT("bone")));
			for (size_t x=0;x<m->header.nBones;x++){
				LWBones lwBone((uint32)x);
				Bone *cbone = &m->bones[x];

				Vec3D Pos = cbone->pivot;	// For Animation & Init Only
				Vec3D Pos_i = Pos;			// For Non-Init Only Exporting
				if (cbone->parent > LW_ITEMTYPE_NOPARENT){
					Pos -= m->bones[cbone->parent].pivot;
					lwBone.ParentType = LW_ITEMTYPE_BONE;
					lwBone.ParentID = cbone->parent;
				}else{
					//lwBone.Active = false;		// Disable Non-Parented bones.
					lwBone.ParentType = LW_ITEMTYPE_OBJECT;
					lwBone.ParentID = LW_ObjCount.GetValue();
				}
				Pos *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
				Pos_i = Pos;
				if (init == false){
					Pos_i = (cbone->transPivot*(modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0));
					if (cbone->parent > LW_ITEMTYPE_NOPARENT){
						Pos_i -= (m->bones[cbone->parent].transPivot*(modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0));
						lwBone.Active = true;
					}
					MakeModelFaceForwards(Pos_i,true);
					Pos_i.x = -Pos_i.x;
					lwBone.RestPos = Pos_i;
				}else{
					MakeModelFaceForwards(Pos,true);
					Pos.x = -Pos.x;
					lwBone.RestPos = Pos;
					lwBone.RestRot = Vec3D();
				}
				lwBone.RestRot = Vec3D();
				AnimVec3D animPos_i = AnimVec3D(AnimVector(Pos_i.x,0),AnimVector(Pos_i.y,0),AnimVector(Pos_i.z,0));
				AnimVec3D animPos, animRot, animSca;
				if (modelExport_LW_ExportAnim > 0){
					// Position
					AnimVector PosX, PosY, PosZ;
					for (size_t i=0;i<cbone->trans.data[cAnim].size();i++){
						size_t aTime = cbone->trans.times[cAnim][i];
						float fTime = (float)aTime;
						Vec3D aPos = (cbone->pivot*(modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0)); //cbone->trans.getValue(cAnim,aTime);
						if (cbone->parent > LW_ITEMTYPE_NOPARENT)
							aPos -= (m->bones[cbone->parent].pivot*(modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0));
						//aPos.z = -aPos.z;
						//MakeModelFaceForwards(aPos,false);
						if (fTime/FRAMES_PER_SECOND < scene.FirstFrame){
							scene.FirstFrame = fTime/FRAMES_PER_SECOND;
						}
						if (fTime/FRAMES_PER_SECOND > scene.LastFrame){
							scene.LastFrame = fTime/FRAMES_PER_SECOND;
						}
						PosX.Push(aPos.x,fTime,cbone->trans.type);
						PosY.Push(aPos.y,fTime,cbone->trans.type);
						PosZ.Push(aPos.z,fTime,cbone->trans.type);
					}
					if (PosX.Size() > 0){
						animPos = AnimVec3D(PosX,PosY,PosZ);
					}else{
						animPos = AnimVec3D(AnimVector(Pos.x,0),AnimVector(Pos.y,0),AnimVector(Pos.z,0));
					}

					// Rotation
					AnimVector RotX, RotY, RotZ;
					for (size_t i=0;i<cbone->rot.data[cAnim].size();i++){
						size_t aTime = cbone->rot.times[cAnim][i];
						float fTime = (float)aTime;
						Vec3D HPB;
						Quaternion q = cbone->rot.getValue(cAnim,aTime);
						Quaternion tq;
						tq.x = q.w; tq.y = q.x; tq.z = q.y; tq.w = q.z;
						Matrix Neo;
						QuaternionToRotationMatrix(tq, Neo);
						RotationMatrixToEulerAnglesXYZ(Neo, HPB.x, HPB.y, HPB.z);

						HPB.x = HPB.x * (180.0f / (float)PI);
						HPB.y = HPB.y * (180.0f / (float)PI);
						HPB.z = HPB.z * (180.0f / (float)PI);

						if (fTime/FRAMES_PER_SECOND < scene.FirstFrame){
							scene.FirstFrame = fTime/FRAMES_PER_SECOND;
						}
						if (fTime/FRAMES_PER_SECOND > scene.LastFrame){
							scene.LastFrame = fTime/FRAMES_PER_SECOND;
						}
						RotX.Push(HPB.x,fTime,cbone->rot.type);
						RotY.Push(HPB.y,fTime,cbone->rot.type);
						RotZ.Push(HPB.z,fTime,cbone->rot.type);
					}
					if (RotX.Size() > 0){
						animRot = AnimVec3D(RotX,RotY,RotZ);
					}else{
						animRot = animValue0;
					}

					// Scale
					AnimVector ScaX, ScaY, ScaZ;
					for (size_t i=0;i<cbone->scale.data[cAnim].size();i++){
						size_t aTime = cbone->scale.times[cAnim][i];
						float fTime = (float)(aTime/FRAMES_PER_SECOND);

						Vec3D Sc = cbone->scale.getValue(cAnim,aTime)*(modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);

						if (floor(fTime) < scene.FirstFrame){
							scene.FirstFrame = floor(fTime);
						}
						if (ceil(fTime) > scene.LastFrame){
							scene.LastFrame = ceil(fTime);
						}
						ScaX.Push(Sc.x,fTime,cbone->scale.type);
						ScaY.Push(Sc.y,fTime,cbone->scale.type);
						ScaZ.Push(Sc.z,fTime,cbone->scale.type);
					}
					if (ScaX.Size() > 0){
						animSca = AnimVec3D(ScaX,ScaY,ScaZ);
					}else{
						animSca = animValue1;
					}
				}else{
					animPos = AnimVec3D(AnimVector(Pos.x,0),AnimVector(Pos.y,0),AnimVector(Pos.z,0));
					animRot = animValue0;
					animSca = animValue1;
				}

				if (init == false)
					lwBone.AnimData = AnimationData(animPos_i,animValue0,animValue1);
				else if (modelExport_LW_ExportAnim > 0)
					lwBone.AnimData = AnimationData(animPos,animRot,animSca);
				else
					lwBone.AnimData = AnimationData(animPos,animValue0,animValue1);

				wxString bone_name = wxString::Format(wxT("Bone_%03i"), x);
				for (size_t j=0; j<BONE_MAX; ++j) {
					if (m->keyBoneLookup[j] == (uint16)x) {
						bone_name = Bone_Names[j];
						break;
					}
				}
				lwBone.Name = bone_name;
				for (size_t j=0;j<Object.Layers[0].Weights.size();j++){
					if (Object.Layers[0].Weights[j].WeightMapName == bone_name){
						lwBone.WeightMap_Name = bone_name;
						break;
					}
				}
				

				SceneObj.Bones.push_back(lwBone);
			}
		}
		scene.Objects.push_back(SceneObj);

		// Lights
		uint32 nLights = m->header.nLights;
		if (nLights>0){
			wxLogMessage(wxT("Model has %i %s."),nLights,(nLights>1?wxT("Lights"):wxT("Light")));
			g_modelViewer->SetStatusText(wxT("Gathering Lights..."));
		}
		for (size_t x=0;x<nLights;x++){
			ModelLight *l = &m->lights[x];
			LWLight Light;

			Light.LightID = LW_LightCount.GetPlus();
			Light.Name = m->name.AfterLast(MPQ_SLASH).BeforeLast('.');
			Light.Name << wxString::Format(wxT(" Light %02i"), Light.LightID);
			if (l->parent > LW_ITEMTYPE_NOPARENT){
				Light.ParentType = LW_ITEMTYPE_BONE;
				Light.ParentID = l->parent;
			}else{
				Light.ParentType = LW_ITEMTYPE_OBJECT;
				Light.ParentID = LW_ObjCount.GetValue();
			}

			Light.Color = l->diffColor.getValue(0,0);
			Light.Intensity = l->diffIntensity.getValue(0,0);
			Light.UseAttenuation = false;
			Light.FalloffRadius = l->AttenEnd.getValue(0,0)*(modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
			Vec3D lpos = l->pos;
			lpos *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
			MakeModelFaceForwards(lpos,true);

			AnimVec3D animPos = AnimVec3D(AnimVector(lpos.x,0),AnimVector(lpos.y,0),AnimVector(lpos.z,0));
			Light.AnimData = AnimationData(animPos,animValue0,animValue1);

			if (l->UseAttenuation.getValue(0,0) > 0.0f){
				Light.UseAttenuation = true;
			}

			scene.Lights.push_back(Light);
		}
		// No attachment lights have been found so far...
		if (att != NULL){
			if (att->model){
				wxLogMessage(wxT("Att Model found."));
			}
			for (size_t c=0; c<att->children.size(); c++) {
				Attachment *att2 = att->children[c];
				for (size_t j=0; j<att2->children.size(); j++) {
					Model *mAttChild = static_cast<Model*>(att2->children[j]->model);

					if (mAttChild){
						if (mAttChild->header.nLights > 0){
							wxLogMessage(wxT("Found Attached light! Model: %s, on %s"),mAttChild->modelname.c_str(),m->modelname.c_str());
#ifdef _DEBUG
							g_modelViewer->SetStatusText(wxString::Format(wxT("Found Attached light! Model: %s, on %s"),mAttChild->modelname.c_str(),m->modelname.c_str()));
#endif
						}
						/*for (size_t x=0;x<mAttChild->header.nLights;x++){
							ModelLight *l = &mAttChild->lights[x];

							Vec3D color = l->diffColor.getValue(0,0);
							float intense = l->diffIntensity.getValue(0,0);
							bool useAtten = false;
							float AttenEnd = l->AttenEnd.getValue(0,0);

							if (l->UseAttenuation.getValue(0,0) > 0){
								useAtten = true;
							}

							//WriteLWSceneLight(fs,lcount,l->pos,l->type,color,intense,useAtten,AttenEnd,2.5);
							
						}*/
					}
				}
			}
		}
		
		// Cameras
		if (m->header.nCameras > 0){
			wxLogMessage(wxT("Model has %i %s."),m->header.nCameras,(m->header.nCameras>1?wxT("Cameras"):wxT("Camera")));
			g_modelViewer->SetStatusText(wxT("Gathering Cameras..."));
		}else{
			wxLogMessage(wxT("Model does not have a Camera."));
		}
		if (m->hasCamera == true){
			for (size_t i=0;i<m->cam.size();i++){
				size_t CameraID = LW_CamCount.GetValue();
				LWCamera C;
				wxLogMessage(wxT("Processing Camera %02i of %02i..."),CameraID,m->cam.size()-1);

				ModelCamera *cam = &m->cam[i];
				C.FieldOfView = cam->fov;
				C.Name = wxString(wxT("Camera %02i"),i);
				size_t CameraTargetID = LW_ObjCount.GetPlus()+1;
				size_t anim = m->animManager->GetAnim();

				// Camera Target
				C.TargetObjectID = CameraTargetID;
				LWSceneObj CamTarget(wxString::Format(wxT("Camera %02i Target"),i), CameraTargetID, 0, LW_ITEMTYPE_NOPARENT, true);
				AnimationData TData;
				if (cam->tTarget.data[anim].size() > 1){
					wxLogMessage(wxT("Camera Target %02i has Animation."),i);
					AnimVec3D cTPos;
					for (size_t x=0;x<cam->tTarget.data[anim].size();x++){
						Vec3D a = cam->target + cam->tTarget.data[anim][x];
						MakeModelFaceForwards(a,true);
						a *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
						size_t ctime = cam->tTarget.times[anim][x];
						float ftime = (float)(ctime/FRAMES_PER_SECOND);
						if (floor(ftime) < scene.FirstFrame){
							scene.FirstFrame = floor(ftime);
						}
						if (ceil(ftime) > scene.LastFrame)
							scene.LastFrame = ceil(ftime);
						AnimVector tpx, tpy, tpz;
						tpx.Push(-a.x,ctime);
						tpy.Push(a.y,ctime);
						tpz.Push(a.z,ctime);

						cTPos = AnimVec3D(tpx,tpy,tpz);
					}
					TData = AnimationData(cTPos, animValue0, animValue1);
				}else{
					wxLogMessage(wxT("Camera Target %02i is static."),i);
					Vec3D camt = cam->target;
					MakeModelFaceForwards(camt,true);
					camt *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
					AnimVec3D cTPos(AnimVec3D(AnimVector(-camt.x,0),AnimVector(camt.y,0),AnimVector(camt.z,0)));
					TData = AnimationData(cTPos, animValue0, animValue1);
				}
				CamTarget.AnimData = TData;
				scene.Objects.push_back(CamTarget);

				
				AnimationData CamData;
				if (cam->tPos.data[anim].size() > 1){
					AnimVector cpx, cpy, cpz;
					// Animations
					for (size_t x=0;x<cam->tPos.data[anim].size();x++){
						// Position Data
						Vec3D p_val = cam->pos + cam->tPos.data[anim][x];
						MakeModelFaceForwards(p_val,true);
						p_val *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
						size_t ctime = cam->tPos.times[anim][x];
						float ftime = (float)(ctime/FRAMES_PER_SECOND);
						if (floor(ftime) < scene.FirstFrame){
							scene.FirstFrame = floor(ftime);
						}
						if (ceil(ftime) > scene.LastFrame)
							scene.LastFrame = ceil(ftime);
						cpx.Push(-p_val.x,ctime);
						cpy.Push(p_val.y,ctime);
						cpz.Push(p_val.z,ctime);
					}
					AnimVec3D cPos(AnimVec3D(cpx,cpy,cpz));
					CamData = AnimationData(cPos, animValue0, animValue1);
				}else{
					Vec3D vect = cam->pos;
					MakeModelFaceForwards(vect,true);
					vect *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
					AnimVec3D cPos(AnimVec3D(AnimVector(-vect.x,0),AnimVector(vect.y,0),AnimVector(vect.z,0)));
					CamData = AnimationData(cPos, animValue0, animValue1);
				}
				C.AnimData = CamData;
				wxLogMessage(wxT("Finished processing Camera %02i."),CameraID);
				scene.Cameras.push_back(C);
			}
		}
	}
	
	g_modelViewer->SetStatusText(wxT("Finished Gathering M2 Data!"));
	return Object;
	Object.~LWObject();
}

// Gather WMO Data
LWObject GatherWMOforLWO(WMO *m, const char *fn, LWScene &scene){
	g_modelViewer->SetStatusText(wxT("Gathering WMO Data for Lightwave Exporter..."));
	wxString RootDir(fn, wxConvUTF8);
	wxString FileName(fn, wxConvUTF8);

	LWObject Object;

	if (!m){
		return Object;
		Object.~LWObject();
	}

	if (modelExport_LW_PreserveDir == true){
		wxString Path, Name;

		Path << FileName.BeforeLast(SLASH);
		Name << FileName.AfterLast(SLASH);
		MakeDirs(Path,wxT("Objects"));
		FileName.Empty();
		FileName << Path << SLASH << wxT("Objects") << SLASH << Name;
	}
	if (modelExport_PreserveDir == true){
		wxString Path1, Path2, Name;

		Path1 << FileName.BeforeLast(SLASH);
		Name << FileName.AfterLast(SLASH);
		Path2 << m->name.BeforeLast(SLASH);
		MakeDirs(Path1,Path2);
		FileName.Empty();
		FileName << Path1 << SLASH << Path2 << SLASH << Name;
	}

	Object.SourceType = wxT("WMO");
	LogExportData(wxT("LWO"),m->name,FileName);

	// Main Object
	LWLayer Layer;
	Layer.Name = m->name.AfterLast(MPQ_SLASH).BeforeLast('.');
	Layer.ParentLayer = -1;

	// Bounding Box for the Layer
	Layer.BoundingBox1 = m->v1;
	Layer.BoundingBox2 = m->v2;

	uint32 SurfCounter = 0;

	// Process Groups
	g_modelViewer->SetStatusText(wxString::Format(wxT("Processing %i Groups..."), m->nGroups));
	for (size_t g=0;g<m->nGroups; g++) {
		g_modelViewer->SetStatusText(wxString::Format(wxT("Processing Group %i of %i..."), g, m->nGroups));
		WMOGroup *group = &m->groups[g];
		//uint32 GPolyCounter = 0;

		//wxLogMessage(wxT("\nGroup %i Info:\n   Batches: %i\n   Indices: %i\n   Vertices: %i"),g,group->nBatches,group->nIndices,group->nVertices);
		Object.PartNames.push_back(wxString(group->name.c_str(), wxConvUTF8));

		Layer.HasVectorColors = group->hascv;

		// Points Batches
		for (size_t b=0; b<group->nBatches; b++){
			WMOBatch *batch = &group->batches[b];
			//wxLogMessage(wxT("\nBatch %i Info:\n   Indice-Start: %i\n   Indice-Count: %i\n   Vert-Start: %i\n   Vert-End: %i"),b,batch->indexStart,batch->indexCount,batch->vertexStart,batch->vertexEnd);
			uint32 *Vert2Point = new uint32[group->nVertices];

			uint32 t = batch->texture;
			WMOMaterial *mat = &m->mat[t];
			bool doublesided = ((mat->flags & WMO_MATERIAL_CULL)?false:true);
			float Surf_Diff = 1.0f;
			float Surf_Lum = 0.0f;
			wxString matName;

			// Add Images to Model
			LWClip ClipImage;
			wxString Texture = m->textures[t];
			wxLogMessage(wxT("Texture: %s"),Texture.c_str());

			ClipImage.Filename = Texture.AfterLast(SLASH).BeforeLast('.');
			ClipImage.Source = Texture.BeforeLast(SLASH);
			ClipImage.TagID = m->nGroups + SurfCounter;
			Object.Images.push_back(ClipImage);

			wxString LWFilename = Texture;
			if (modelExport_LW_PreserveDir == true){
				wxString Path = RootDir.BeforeLast(SLASH);
				wxString Name = Texture.AfterLast(SLASH);

				MakeDirs(Path,wxT("Images"));

				LWFilename.Empty();
				LWFilename <<Path<<SLASH<<wxT("Images")<<SLASH<<Name;
			}
			if (modelExport_PreserveDir == true){
				wxString Path1(LWFilename.BeforeLast(SLASH));
				wxString Path2(Texture.BeforeLast(SLASH));
				wxString Name(LWFilename.AfterLast(SLASH));
				//wxLogMessage(wxT("Path1 (root): %s, Path2(Img Path): %s, Name (Img Name): %s"),Path1,Path2,Name);

				MakeDirs(Path1,Path2);

				LWFilename.Empty();
				LWFilename << Path1<<SLASH<<Path2<<SLASH<<Name;
			}
			//LWFilename = LWFilename.AfterLast(SLASH);
			SaveTexture2(Texture,RootDir.BeforeLast(SLASH),wxT("LWO"),wxT("tga"));

			LWSurf_Image SurfColor_Image = LWSurf_Image(ClipImage.TagID,LW_TEXTUREAXIS_UV,0.0f,0.0f);

			matName = ClipImage.Filename;

			if (matName.Len() == 0)
				matName = wxString::Format(wxT("Material_%03i"), mat->tex);

			// If Doublesided
			if (doublesided == false) {
				matName = matName + wxT("_Dbl");
			}

			LWSurface Surface(matName,Texture,SurfColor_Image,LWSurf_Image(),LWSurf_Image(),Vec3D(1,1,1),Surf_Diff,Surf_Lum,doublesided,Layer.HasVectorColors);
			Object.Surfaces.push_back(Surface);

			// Process Verticies
			for (size_t v=batch->vertexStart; v<=batch->vertexEnd; v++) {
				// --== Point Data ==--
				LWPoint Point;
				uint32 pointnum = (uint32)Layer.Points.size();

				// Points
				// Using straight verts causes the model to come out on it's side.
				Point.PointData.x = group->vertices[v].x;
				Point.PointData.y = group->vertices[v].z;
				Point.PointData.z = group->vertices[v].y;
				Point.UVData = group->texcoords[v];		// UVs
				// Weight Data not needed for WMOs

				// Vertex Colors
				LWVertexColor vc;
				if (group->hascv) {
					WMOVertColor wvc = group->VertexColors[v];
					vc.r = wvc.r;
					vc.g = wvc.g;
					vc.b = wvc.b;
					vc.a = wvc.a;
				}
				Point.VertexColors = LWVertexColor(vc.r,vc.g,vc.b,vc.a);

				Vert2Point[v] = pointnum;
				//wxLogMessage(wxT("Vert %i = Point %i"),v,pointnum);
				Layer.Points.push_back(Point);
			}

			// Process Indices
			for (size_t i=0; i<batch->indexCount; i+=3) {
				size_t ci = batch->indexStart+i;

				// --== Polygon Data ==--	
				LWPoly Poly;
				Poly.PolyData.numVerts = 3;
				for (ssize_t x=0;x<3;x++){
					// Mod is needed, cause otherwise the polys will be generated facing the wrong way. 
					// Proper order: 0, 2, 1
					int mod = 0;
					if (x==1){
						mod = 1;
					}else if (x==2){
						mod = -1;
					}

					// Polygon Indice
					size_t a = ci + x + mod;
					size_t b = group->IndiceToVerts[a];
					//wxLogMessage(wxT("Group: %i, a: %i, b:%i, Final Indice: %i"),g,a,b,Vert2Point[b]);
					Poly.PolyData.indice[x] = Vert2Point[b];
					Poly.Normals.indice[x] = Vert2Point[b];

					// Normal Indice
					Vec3D nvdir;
					nvdir.x = group->normals[b].x;
					nvdir.y = group->normals[b].z;
					nvdir.z = -group->normals[b].y;
					Poly.Normals.direction[x] = nvdir;
				}
				Poly.PartTagID = g;
				Poly.SurfTagID = m->nGroups + SurfCounter;
				Poly.Normals.polygon = Layer.Polys.size();
				Poly.NormalMapName = Layer.Name + wxString(wxT("_NormalMap"));
				//wxLogMessage(wxT("Normal Data: Poly %i, i1:%i, i2:%i, i3:%i\nND	VectorDir i1: X:%f, Y:%f, Z:%f\nND	VectorDir i2: X:%f, Y:%f, Z:%f\nND	VectorDir i3: X:%f, Y:%f, Z:%f"),Layer.Polys.size(),Poly.Normals.indice[0],Poly.Normals.indice[1],Poly.Normals.indice[2],Poly.Normals.direction[0].x,Poly.Normals.direction[0].y,Poly.Normals.direction[0].z,Poly.Normals.direction[1].x,Poly.Normals.direction[1].y,Poly.Normals.direction[1].z,Poly.Normals.direction[2].x,Poly.Normals.direction[2].y,Poly.Normals.direction[2].z);
				Layer.Polys.push_back(Poly);
			}
			SurfCounter++;
		}
	}
	
	g_modelViewer->SetStatusText(wxT("Checking for Vector Colors..."));
	if (Layer.HasVectorColors == true){
		for (size_t i=0;i<Object.Surfaces.size();i++){
			Object.Surfaces[i].hasVertColors = true;
		}
	}
	Object.Layers.push_back(Layer);
	wxLogMessage(wxT("Completed WMO Gathering. Building Basic Scene Data..."));

	// Scene Data
	g_modelViewer->SetStatusText(wxT("Gathering Scene Data..."));
	size_t WMOObjID = LW_ObjCount.GetPlus();
	LWSceneObj scObject(FileName,WMOObjID,-1);
	scene.Objects.push_back(scObject);
	if ((modelExport_LW_ExportLights == true) && (m->nLights > 0)){
		g_modelViewer->SetStatusText(wxT("Gathering Lights..."));
		for (size_t x=0;x<m->nLights;x++){
			size_t LightID = scene.Lights.size();
			LWLight l;
			WMOLight cl = m->lights[x];
			Vec3D Lcolor(cl.fcolor.x,cl.fcolor.y, cl.fcolor.z);
			float Lint = cl.intensity;

			while ((Lcolor.x > 1.0f)||(Lcolor.y > 1.0f)||(Lcolor.z > 1.0f)) {
				Lcolor.x = Lcolor.x * 0.99;
				Lcolor.y = Lcolor.y * 0.99;
				Lcolor.z = Lcolor.z * 0.99;
				Lint = Lint / 0.99;
			}
			
			l.LightID = (uint32)LightID;
			l.Color = Lcolor;
			l.Intensity = Lint;

			AnimVector lx,ly,lz;
			Vec3D lpos = cl.pos;
			lpos *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
			lx.Push(-lpos.z,0,0);
			ly.Push(lpos.y,0,0);
			lz.Push(-lpos.x,0,0);

			l.AnimData = AnimationData(AnimVec3D(lx,ly,lz),animValue0,animValue1);
			l.FalloffRadius = 2.5f;
			if (cl.useatten > 0) {
				l.FalloffRadius = cl.attenEnd*(modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
			}
			l.LightType = LW_LIGHTTYPE_POINT;
			l.ParentType = LW_ITEMTYPE_OBJECT;
			l.ParentID = WMOObjID;		// These lights will always be parented to the WMO object.
			wxString lNum,liNum;
			lNum << (unsigned int)LightID;
			liNum << (unsigned int)m->nLights;
			while (lNum.Len() < liNum.Len()){
				lNum = wxString(wxT("0")) + lNum;
			}
			l.Name = wxString::Format(wxT("%s Light %03i"), m->name.AfterLast(MPQ_SLASH).BeforeLast('.').c_str(), LightID);

			scene.Lights.push_back(l);
		}
	}

	// Doodads
	wxLogMessage(wxT("Compiled Scene Data. Checking Doodads..."));
	g_modelViewer->SetStatusText(wxT("Processing Doodads..."));
/*
	Need to get this working with layers before we enable it.
	For now, use the old method in the main function.	
*/
	int currset = m->doodadset;
	if (modelExport_LW_ExportDoodads == true){
		if ((modelExport_LW_DoodadsAs == 0)||(modelExport_LW_DoodadsAs == 1)){			// Doodads as Nulls or Scene Objects...
			g_modelViewer->SetStatusText(wxT("Exporting Doodads as part of the Scene File..."));
			for (size_t ds=0;ds<m->nDoodadSets;ds++){
				size_t ddSetID = LW_ObjCount.GetPlus();
				LWSceneObj doodadset(wxString(m->doodadsets[ds].name, wxConvUTF8), ddSetID, WMOObjID, LW_ITEMTYPE_OBJECT, true);
				doodadset.AnimData = AnimationData(animValue0, animValue0, animValue1);
				scene.Objects.push_back(doodadset);

				// Load the doodads
				m->doodadset = (int)ds;
				m->updateModels();

				wxLogMessage(wxT("Processing Doodadset %i: %s, Num Doodads in set: %i, Starts: %i"),ds, m->doodadsets[ds].name,m->doodadsets[ds].size,m->doodadsets[ds].start);
				for (size_t dd=m->doodadsets[ds].start;dd<(m->doodadsets[ds].start+m->doodadsets[ds].size);dd++){
					wxLogMessage(wxT("Processing Doodad #%i, %s..."),dd, m->modelis[dd].filename.c_str());
					WMOModelInstance *ddinstance = &m->modelis[dd];
					size_t ddID = LW_ObjCount.GetPlus();

					wxString ddfilename = ddinstance->filename;
					bool isNull = true;
					if (modelExport_LW_DoodadsAs == 1){
						ddfilename = RootDir.BeforeLast(SLASH);
						ddfilename << SLASH << ddinstance->filename.AfterLast(SLASH).BeforeLast(wxT('.')) << wxT(".lwo");
						if (modelExport_LW_PreserveDir == true){
							wxString Path, Name;

							// Object
							Path << RootDir.BeforeLast(SLASH);
							Name << ddfilename.AfterLast(SLASH);
							MakeDirs(Path,wxT("Objects"));
							ddfilename.Empty();
							ddfilename << Path << SLASH << wxT("Objects") << SLASH << Name;
						}
						if (modelExport_PreserveDir == true){
							wxString Path1, Path2, Name;

							// Objects
							Path1 << ddfilename.BeforeLast(SLASH);
							Name << ddfilename.AfterLast(SLASH);
							Path2 << ddinstance->filename.BeforeLast(SLASH);
							MakeDirs(Path1,Path2);
							ddfilename.Empty();
							ddfilename << Path1 << SLASH << Path2 << SLASH << Name;
						}
						isNull = false;
					}
					//wxLogMessage(wxT("Doodad File Name: %s"),ddinstance->filename);
					LWSceneObj doodad(ddfilename,ddID,ddSetID,LW_ITEMTYPE_OBJECT,isNull);
					Matrix ddmat;
					Quaternion qRot = Quaternion(ddinstance->dir,ddinstance->w);
					float ddiscale = ddinstance->sc*(modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
					ddmat.scale(Vec3D(ddiscale,ddiscale,ddiscale));
					ddmat.translation(ddinstance->pos);
					ddmat.QRotate(qRot);

					Vec3D ddid = ddinstance->dir;
					//ddid.x += (float)(PI/2); // (PI/2) = 90 degree turn
					Vec3D rot = QuaternionToXYZ(ddid,ddinstance->w);

					AnimVector dpx,dpy,dpz,drx,dry,drz,ds;
					ds.Push(ddiscale,0);
					Vec3D ddipos = ddinstance->pos;
					ddipos *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
					dpx.Push(-ddipos.z,0);
					dpy.Push(ddipos.y,0);
					dpz.Push(-ddipos.x,0);
					drx.Push(rot.x,0);
					dry.Push(-rot.y,0);
					drz.Push(rot.z,0);

					doodad.AnimData = AnimationData(AnimVec3D(dpx,dpy,dpz),AnimVec3D(drx,dry,drz),AnimVec3D(ds,ds,ds));

					scene.Objects.push_back(doodad);
					wxLogMessage(wxT("Doodad added to scene. Checking for Lights..."));

					if (!ddinstance->model){
						wxLogMessage(wxT("Loading Model..."));
						ddinstance->loadModel(m->loadedModels);
						wxLogMessage(wxT("Finished Loading Model."));
					}
					Model *ddm = ddinstance->model;
					uint32 ddnLights = ddm->header.nLights;
					if ((modelExport_LW_ExportLights == true) && (ddnLights > 0)){
						wxLogMessage(wxT("Processing %i Doodad Lights..."),ddnLights);
						for (size_t ddl=0;ddl<ddm->header.nLights;ddl++){
							wxLogMessage(wxT("Gathering Light %i data..."),ddl);
							size_t LightID = LW_LightCount.GetPlus();
							LWLight l;
							//wxLogMessage(wxT("Model Light..."));
							ModelLight cl = ddm->lights[ddl];
							//wxLogMessage(wxT("Diff Light Color..."));
							Vec3D diffColor = cl.diffColor.getValue(0,0);
							//wxLogMessage(wxT("Light RGB Color..."));
							Vec3D Lcolor(diffColor.x, diffColor.y, diffColor.z);
							//wxLogMessage(wxT("Light Intensity..."));
							float Lint = cl.diffIntensity.getValue(0,0);

							wxLogMessage(wxT("Adjusting Light levels..."));
							while ((Lcolor.x > 1.0f)||(Lcolor.y > 1.0f)||(Lcolor.z > 1.0f)) {
								Lcolor.x = Lcolor.x * 0.99;
								Lcolor.y = Lcolor.y * 0.99;
								Lcolor.z = Lcolor.z * 0.99;
								Lint = Lint / 0.99;
							}
							
							wxLogMessage(wxT("Processing Location..."));
							AnimVector lx,ly,lz;
							Vec3D lpos = cl.pos;
							lpos *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
							lx.Push(-lpos.z,0,0);
							ly.Push(lpos.y,0,0);
							lz.Push(-lpos.x,0,0);
							
							wxLogMessage(wxT("Setting variables..."));
							l.AnimData = AnimationData(AnimVec3D(lx,ly,lz),animValue0,animValue1);

							l.LightID = LightID;
							l.Color = Lcolor;
							l.Intensity = Lint;
							if (cl.type == MODELLIGHT_DIRECTIONAL){
								l.LightType = LW_LIGHTTYPE_DISTANT;
							}else{
								l.LightType = LW_LIGHTTYPE_POINT;
							}

							l.FalloffRadius = 2.5f;
							if (cl.UseAttenuation.getValue(0,0) > 0) {
								l.FalloffRadius = cl.AttenEnd.getValue(0,0)*(modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
							}
							l.ParentType = LW_ITEMTYPE_OBJECT;
							l.ParentID = ddID;
							wxString lNum,liNum;
							lNum << LightID;
							liNum << ddnLights;
							while (lNum.Len() < liNum.Len()){
								lNum = wxString(wxT("0")) + lNum;
							}
							wxString lName(ddm->name.AfterLast(MPQ_SLASH).BeforeLast('.'));
							l.Name = lName << wxT(" Light ") << (wxChar)LightID;

							scene.Lights.push_back(l);
						}
						wxLogMessage(wxT("Finished Processing Doodad Lights."));
					}
					wxLogMessage(wxT("Finished Processing Doodad."));
				}
			}
			wxLogMessage(wxT("Scene completed!"));
			if (modelExport_LW_DoodadsAs == 1){
				wxLogMessage(wxT("Exporting Doodad Model..."));
				wxArrayString used;
				for (size_t i=0;i<m->modelis.size();i++){
					Model *ddm = m->modelis[i].model;
					bool tripped = false;
					for (size_t x=0;x<used.size();x++){
						if (used[x] == ddm->modelname){
							tripped = true;
						}
					}
					if (tripped == false){
						wxString fname = RootDir.BeforeLast(SLASH);
						fname << SLASH << ddm->modelname.AfterLast(SLASH).BeforeLast(wxT('.')) << wxT(".lwo");
						wxLogMessage(wxT("Pre-Gather Doodad Model Filename: %s"),fname.c_str());
						// Must gather before paths, else it generates files in the wrong place.
						LWScene empty_scene;
						wxLogMessage(wxT("Gathering Doodad Model..."));
						LWObject DDObject = GatherM2forLWO(NULL,ddm,true,fname,empty_scene,false);
						if (modelExport_LW_PreserveDir == true){
							wxString Path, Name;

							// Object
							Path << RootDir.BeforeLast(SLASH);
							Name << fname.AfterLast(SLASH);
							MakeDirs(Path,wxT("Objects"));
							fname.Empty();
							fname << Path << SLASH << wxT("Objects") << SLASH << Name;
						}
						if (modelExport_PreserveDir == true){
							wxString Path1, Path2, Name;

							// Objects
							Path1 << fname.BeforeLast(SLASH);
							Name << fname.AfterLast(SLASH);
							Path2 << ddm->modelname.BeforeLast(SLASH);
							MakeDirs(Path1,Path2);
							fname.Empty();
							fname << Path1 << SLASH << Path2 << SLASH << Name;
						}
						wxLogMessage(wxT("Gathering Complete."));
						wxLogMessage(wxT("Final Doodad Model Filename: %s"),fname.c_str());

						if (DDObject.SourceType == wxEmptyString){
							//wxMessageBox(wxT("Error gathering information for export."),wxT("Export Error"));
							wxLogMessage(wxT("Error gathering Object data for Doodad %s."),ddm->modelname.c_str());
						}
						if (WriteLWObject(fname, DDObject) != EXPORT_OKAY){
							wxLogMessage(wxT("Error writing object file for Doodad %s.") ,ddm->modelname.c_str());
						}
						used.push_back(ddm->modelname);
					}
				}
			}
		}else if (modelExport_LW_DoodadsAs == 2){	// Doodad Sets as Seperate Layers...
#ifdef _DEBUG
			wxMessageBox(wxT("Functionality for this Doodad Export Type\nhas not yet been implemented."),wxT("NYI"));
			/*
			g_modelViewer->SetStatusText(wxT("Exporting Doodads as seperate layers of the object..."));
			for (size_t ds=0;ds<m->nDoodadSets;ds++){
				wxLogMessage(wxT("Processing Doodadset %i: %s"),ds,m->doodadsets[ds].name);
				bool doodadAdded = false;

				wxLogMessage(wxT("Adding %i Doodads to Layer %i..."), m->doodadsets[ds].size, ds+1);
				for (size_t dd=m->doodadsets[ds].start;dd<(m->doodadsets[ds].start+m->doodadsets[ds].size);dd++){
					WMOModelInstance *ddinstance = &m->modelis[dd];
					wxLogMessage(wxT("Processing Doodad %i: %s"),dd,ddinstance->filename);

					wxLogMessage(wxT("Doodad Instance is Animated: %s"),(ddinstance->model->animated?wxT("true"):wxT("false")));

					LWObject Doodad = GatherM2forLWO(NULL,ddinstance->model,true,FileName,LWScene(),false);

					// --== Model Debugger ==--
					// Exports the model immediately after gathering, to help determine if a problem is with the gathering function or the doodad-placement functions.
					wxString doodadname = wxString(fn, wxConvUTF8).BeforeLast('.') << wxT("_") << ddinstance->filename.AfterLast(MPQ_SLASH).BeforeLast('.') << wxT(".lwo");
					if (modelExport_LW_PreserveDir == true){
						wxString Path, Name;

						Path << doodadname.BeforeLast(SLASH);
						Name << doodadname.AfterLast(SLASH);

						MakeDirs(Path,wxT("Objects"));

						doodadname.Empty();
						doodadname << Path << SLASH << wxT("Objects") << SLASH << Name;
					}
					if (modelExport_PreserveDir == true){
						wxString Path1, Path2, Name;
						Path1 << doodadname.BeforeLast(SLASH);
						Name << doodadname.AfterLast(SLASH);
						Path2 << m->name.BeforeLast(SLASH);

						MakeDirs(Path1,Path2);

						doodadname.Empty();
						doodadname << Path1 << SLASH << Path2 << SLASH << Name;
					}
					wxLogMessage(wxT("Exporting Doodad Test Model: %s"),doodadname);
					WriteLWObject(doodadname, Doodad);
					// End Model Debugger

					if (Doodad.SourceType == wxEmptyString){
						wxLogMessage(wxT("Error Gathering Doodad Model."));
						continue;
					}
					//wxLogMessage(wxT("Finished Gathering Doodad %i with #%i Layers."),dd,Doodad.Layers.size());

					// Move, rotate & scale Doodad
					Vec3D TheArchitect = ddinstance->pos;
					TheArchitect *= (modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0);
					Vec3D Trinity(ddinstance->sc,-(ddinstance->sc),-(ddinstance->sc));
					Vec3D Seraph(1,1,1);
					Vec3D Oracle(-(ddinstance->dir.z),ddinstance->dir.x,ddinstance->dir.y);
					Quaternion Morphius(Oracle,ddinstance->w);
					Quaternion Niobe(Vec4D(0,0,0,0));

					for (size_t i=0;i<Doodad.Layers[0].Points.size();i++){
						Vec3D AgentSmith = Doodad.Layers[0].Points[i].PointData;

						Matrix Neo;

						Neo.translation(AgentSmith);					// Set Original Position
						Neo.quaternionRotate(Niobe);					// Set Original Rotation
						Neo.scale(Seraph);								// Set Original Scale

						Neo *= Matrix::newTranslation(TheArchitect);	// Apply New Position
						Neo *= Matrix::newQuatRotate(Morphius);			// Apply New Rotation
						Neo *= Matrix::newScale(Trinity);				// Apply New Scale

						Doodad.Layers[0].Points[i].PointData = Neo * AgentSmith;
					}

					wxString ddPrefix(wxT("Doodad "));
					wxString ddnum, numdds;
					ddnum << (unsigned int)dd;
					numdds << (unsigned int)m->doodadsets[ds].size;
					while (ddnum.Length() < numdds.Length()) {
						ddnum = wxString(wxT("0")) << ddnum;
					}					
					ddPrefix << ddnum << wxT("_") << m->modelis[dd].filename.AfterLast(MPQ_SLASH).BeforeLast('.') << wxT("_");
					//wxLogMessage(wxT("Doodad Prefix: %s"),ddPrefix);
					Object.Plus(Doodad,ds+1,ddPrefix);
					doodadAdded = true;
					Doodad.~LWObject();

					uint32 ddID = (uint32)scene.Objects.size();

					LWSceneObj ddlyr(FileName,ddID,0,LW_ITEMTYPE_OBJECT,false,(ds+2));
					scene.Objects.push_back(ddlyr);

					uint32 ddnLights = ddinstance->model->header.nLights;
					Model *ddm = ddinstance->model;
					if ((modelExport_LW_ExportLights == true) && (ddnLights > 0)){
						for (size_t x=0;x<ddnLights;x++){
							size_t LightID = scene.Lights.size();
							LWLight l;
							ModelLight cl = ddm->lights[x];
							Vec3D diffColor = cl.diffColor.getValue(0,0);
							Vec3D Lcolor(diffColor.x, diffColor.y, diffColor.z);
							float Lint = cl.diffIntensity.getValue(0,0);

							while ((Lcolor.x > 1.0f)||(Lcolor.y > 1.0f)||(Lcolor.z > 1.0f)) {
								Lcolor.x = Lcolor.x * 0.99;
								Lcolor.y = Lcolor.y * 0.99;
								Lcolor.z = Lcolor.z * 0.99;
								Lint = Lint / 0.99;
							}

							l.LightID = (uint32)LightID;
							l.Color = Lcolor;
							l.Intensity = Lint;
							if (cl.type == MODELLIGHT_DIRECTIONAL){
								l.LightType = LW_LIGHTTYPE_DISTANT;
							}else{
								l.LightType = LW_LIGHTTYPE_POINT;
							}
							l.AnimData.Push(cl.pos,Vec3D(0,0,0),Vec3D(1,1,1),0);

							l.FalloffRadius = 2.5f;
							if (cl.UseAttenuation.getValue(0,0) > 0) {
								l.FalloffRadius = cl.AttenEnd.getValue(0,0);
							}
							l.ParentType = LW_ITEMTYPE_OBJECT;
							l.ParentID = (int32)ddID;
							wxString lNum,liNum;
							lNum << (unsigned int)LightID;
							liNum << (unsigned int)m->nLights;
							while (lNum.Len() < liNum.Len()){
								lNum = wxString(wxT("0")) + lNum;
							}
							wxString lName(m->name.AfterLast(MPQ_SLASH).BeforeLast('.'));
							l.Name = lName << wxT(" Light ") << (wxChar)LightID;

							LightID++;
							scene.Lights.push_back(l);
						}
					}
				}
				if (doodadAdded == true){
					Object.Layers[ds+1].Name = wxString(m->doodadsets[ds].name);
					Object.Layers[ds+1].ParentLayer = 0;		// Set Parent to main WMO model.
				}
			}
			*/
#endif
		}else if (modelExport_LW_DoodadsAs == 3){	// All Doodads as a Single Layer...
			g_modelViewer->SetStatusText(wxT("Exporting all Doodads as a single layer of the object..."));
		}else if (modelExport_LW_DoodadsAs == 4){	// Each Group's Doodads as a Single Layer...
			g_modelViewer->SetStatusText(wxT("Exporting each Doodad group as a single layer of the object..."));
		}
	}
	
	m->showDoodadSet(currset);

	g_modelViewer->SetStatusText(wxT("Finished Gathering WMO Data!"));
	return Object;
	Object.~LWObject();
}

// Gather ADT Data
LWObject GatherADTforLWO(MapTile *m, const char *fn, LWScene &scene){
	g_modelViewer->SetStatusText(wxT("Gathering ADT Data for Lightwave Exporter..."));
	wxString FileName(fn, wxConvUTF8);
	LWObject Object;

	if (!m){
		return Object;
		Object.~LWObject();
	}

	if (modelExport_LW_PreserveDir == true){
		wxString Path, Name;

		Path << FileName.BeforeLast(SLASH);
		Name << FileName.AfterLast(SLASH);
		MakeDirs(Path,wxT("Objects"));
		FileName.Empty();
		FileName << Path << SLASH << wxT("Objects") << SLASH << Name;
	}
	if (modelExport_PreserveDir == true){
		wxString Path1, Path2, Name;

		Path1 << FileName.BeforeLast(SLASH);
		Name << FileName.AfterLast(SLASH);
		Path2 << m->name.BeforeLast(SLASH);
		MakeDirs(Path1,Path2);
		FileName.Empty();
		FileName << Path1 << SLASH << Path2 << SLASH << Name;
	}

	Object.SourceType = wxT("ADT");
	LogExportData(wxT("LWO"),m->name,FileName);

	// Main Object
	LWLayer Layer;
	Layer.Name = m->name.AfterLast(MPQ_SLASH).BeforeLast('.');
	Layer.ParentLayer = -1;

	// Bounding Box for the Layer
	//Layer.BoundingBox1 = m->v1;
	//Layer.BoundingBox2 = m->v2;

	// Process Chunks
	g_modelViewer->SetStatusText(wxT("Processing Chunk Data..."));
	for (ssize_t c1=0;c1<16;c1++){
		for (ssize_t c2=0;c2<16;c2++){
			MapChunk *chunk = &m->chunks[c1][c2];
			for (ssize_t num=0;num<145;num++){
				LWPoint a;
				a.PointData = (chunk->tv[num]*(modelExport_ScaleToRealWorld == true?REALWORLD_SCALE:1.0));
				Layer.Points.push_back(a);
			}
		}
	}
	Object.Layers.push_back(Layer);

	g_modelViewer->SetStatusText(wxT("Finished Gathering ADT Data!"));
	return Object;
	Object.~LWObject();
}

//---------------------------------------------
// --== Global Exporting Functions ==--
//---------------------------------------------

// Export M2s
size_t ExportLWO_M2(Attachment *att, Model *m, const char *fn, bool init){
	g_modelViewer->SetStatusText(wxT("Preparing Lightwave M2 Exporter..."));
	wxString filename(fn, wxConvUTF8);
	wxString scfilename = wxString(fn, wxConvUTF8).BeforeLast('.') + wxT(".lws");
	LW_ObjCount.Reset();
	LW_LightCount.Reset();
	LW_BoneCount.Reset();
	LW_CamCount.Reset();

	if (modelExport_LW_PreserveDir == true){
		wxString Path, Name;

		// Object
		Path << filename.BeforeLast(SLASH);
		Name << filename.AfterLast(SLASH);
		MakeDirs(Path,wxT("Objects"));
		filename.Empty();
		filename << Path << SLASH << wxT("Objects") << SLASH << Name;

		// Scene
		Path.Empty();
		Name.Empty();
		Path << scfilename.BeforeLast(SLASH);
		Name << scfilename.AfterLast(SLASH);
		MakeDirs(Path,wxT("Scenes"));
		scfilename.Empty();
		scfilename << Path << SLASH << wxT("Scenes") << SLASH << Name;
	}
	if (m->modelType != MT_CHAR){
		if (modelExport_PreserveDir == true){
			wxString Path1, Path2, Name;

			// Objects
			Path1 << filename.BeforeLast(SLASH);
			Name << filename.AfterLast(SLASH);
			Path2 << m->name.BeforeLast(SLASH);
			MakeDirs(Path1,Path2);
			filename.Empty();
			filename << Path1 << SLASH << Path2 << SLASH << Name;

			// Scene
			Path1.Empty();
			Path2.Empty();
			Name.Empty();
			Path1 << scfilename.BeforeLast(SLASH);
			Name << scfilename.AfterLast(SLASH);
			Path2 << m->name.BeforeLast(SLASH);
			MakeDirs(Path1,Path2);
			scfilename.Empty();
			scfilename << Path1 << SLASH << Path2 << SLASH << Name;
		}
	}

	// Scene Data
	LWScene Scene(scfilename.AfterLast(SLASH),scfilename.BeforeLast(SLASH));

	// Object Data
	g_modelViewer->SetStatusText(wxT("Gathering object data..."));
	LWObject Object = GatherM2forLWO(att,m,init,wxString(fn, wxConvUTF8),Scene);
	if (Object.SourceType == wxEmptyString){
		wxMessageBox(wxT("Error gathering information for export."),wxT("Export Error"));
		wxLogMessage(wxT("Failure gathering information for export."));
		return EXPORT_ERROR_NO_DATA;
	}
	g_modelViewer->SetStatusText(wxT("Sending M2 Data to LWO Writing Function..."));
	wxLogMessage(wxT("Sending M2 Data to LWO Writing Function..."));
	size_t objr = WriteLWObject(filename, Object);
	if (objr != EXPORT_OKAY){
		if ((objr == EXPORT_ERROR_NO_DATA)||(objr == EXPORT_ERROR_FILE_ACCESS)){
			wxString msg = wxT("Error Writing the M2 file to a Lightwave Object.");
			wxMessageBox(msg,wxT("Writing Error"));
			wxLogMessage(msg);
		}else if (objr == EXPORT_ERROR_BAD_FILENAME){
			wxString msg = wxT("Bad Filename. Failed writing Lightwave Object.");
			wxMessageBox(msg,wxT("Writing Error"));
			wxLogMessage(msg);
		}else if (objr == EXPORT_ERROR_NO_OVERWRITE){
			wxLogMessage(wxT("Writing stopped. Did not overwrite the existing Lightwave Object file."));
		}else{
			wxString msg = wxT("Error Writing the M2 file to a Lightwave Object.");
			wxMessageBox(msg,wxT("Writing Error"));
			wxLogMessage(msg);
		}
		return objr;
	}else{
		wxLogMessage(wxT("LWO Object \"%s\" Writing Complete."),filename.c_str());
		g_modelViewer->SetStatusText(wxT("M2 Object File successfully written."));
	}
	Object.~LWObject();

	// Scene Data
	// Export only if the Object file was successfully written.
	size_t scnr = EXPORT_OKAY;
	
	g_modelViewer->SetStatusText(wxT("Preparing M2 Scene Data..."));
	bool writescene = false;
	if (objr == EXPORT_OKAY)
		writescene = true;

	// Option to write scene even if model fails/is not overwritten
	if (modelExport_LW_AlwaysWriteSceneFile == true)
		writescene = true;

	// Write the Scene File
	if (writescene == true){
		g_modelViewer->SetStatusText(wxT("Sending Scene data to be written..."));
		wxLogMessage(wxT("Writing Scene file..."));
		WriteLWScene(&Scene);
	}else{
		wxLogMessage(wxT("Scene file not written."));
	}
	
	wxLogMessage(wxT("Cleaning Scene File Data..."));
	Scene.~LWScene();

	wxLogMessage(wxT("Checking Scene error code..."));
	if (scnr != EXPORT_OKAY){
		g_modelViewer->SetStatusText(wxT("An error occured while writing the scene file."));
		wxLogMessage(wxT("An error occured while writing the scene file."));
		return scnr;
	}
	
	g_modelViewer->SetStatusText(wxT("Lightwave M2 Export complete!"));
	wxLogMessage(wxT("Lightwave M2 Export finished with no errors."));
	return EXPORT_OKAY;
}

// Export WMOs
size_t ExportLWO_WMO(WMO *m, const char *fn){
	g_modelViewer->SetStatusText(wxT("Preparing Lightwave WMO Exporter..."));
	wxString filename(fn, wxConvUTF8);
	wxString scfilename = filename.BeforeLast('.') + wxT(".lws");
	LW_ObjCount.Reset();
	LW_LightCount.Reset();
	LW_BoneCount.Reset();
	LW_CamCount.Reset();

	if (modelExport_LW_PreserveDir == true){
		wxString Path, Name;

		// Object
		Path << filename.BeforeLast(SLASH);
		Name << filename.AfterLast(SLASH);
		MakeDirs(Path,wxT("Objects"));
		filename.Empty();
		filename << Path << SLASH << wxT("Objects") << SLASH << Name;

		// Scene
		Path.Empty();
		Name.Empty();
		Path << scfilename.BeforeLast(SLASH);
		Name << scfilename.AfterLast(SLASH);
		MakeDirs(Path,wxT("Scenes"));
		scfilename.Empty();
		scfilename << Path << SLASH << wxT("Scenes") << SLASH << Name;
	}
	if (modelExport_PreserveDir == true){
		wxString Path1, Path2, Name;

		// Objects
		Path1 << filename.BeforeLast(SLASH);
		Name << filename.AfterLast(SLASH);
		Path2 << m->name.BeforeLast(SLASH);
		MakeDirs(Path1,Path2);
		filename.Empty();
		filename << Path1 << SLASH << Path2 << SLASH << Name;

		// Scene
		Path1.Empty();
		Path2.Empty();
		Name.Empty();
		Path1 << scfilename.BeforeLast(SLASH);
		Name << scfilename.AfterLast(SLASH);
		Path2 << m->name.BeforeLast(SLASH);
		MakeDirs(Path1,Path2);
		scfilename.Empty();
		scfilename << Path1 << SLASH << Path2 << SLASH << Name;
	}

	// Scene Data
	LWScene Scene(scfilename.AfterLast(SLASH), scfilename.BeforeLast(SLASH));

	// Object Data
	g_modelViewer->SetStatusText(wxT("Requesting WMO Data..."));
	LWObject Object = GatherWMOforLWO(m, fn, Scene);
	if (Object.SourceType == wxEmptyString){
		wxMessageBox(wxT("Error gathering information for export."),wxT("Export Error"));
		wxLogMessage(wxT("Failure gathering information for export."));
		return EXPORT_ERROR_NO_DATA;
	}

	// Write LWO File
	wxLogMessage(wxT("Sending WMO Data to LWO Writing Function..."));
	g_modelViewer->SetStatusText(wxT("Sending WMO Data to LWO Writing Function..."));
	size_t objr = WriteLWObject(filename, Object);
	if (objr != EXPORT_OKAY){
		if ((objr == EXPORT_ERROR_NO_DATA)||(objr == EXPORT_ERROR_FILE_ACCESS)){
			wxString msg = wxT("Error Writing the WMO file to a Lightwave Object.");
			wxMessageBox(msg,wxT("Writing Error"));
			wxLogMessage(msg);
		}else if (objr == EXPORT_ERROR_BAD_FILENAME){
			wxString msg = wxT("Bad Filename. Failed writing Lightwave Object.");
			wxMessageBox(msg,wxT("Writing Error"));
			wxLogMessage(msg);
		}else if (objr == EXPORT_ERROR_NO_OVERWRITE){
			wxLogMessage(wxT("Writing stopped. Did not overwrite the existing Lightwave Object file."));
		}else{
			wxString msg = wxT("Error Writing the WMO file to a Lightwave Object.");
			wxMessageBox(msg,wxT("Writing Error"));
			wxLogMessage(msg);
		}
		return objr;
	}else{
		wxLogMessage(wxT("LWO Object Writing Complete."));
	}
	Object.~LWObject();

	// Scene Data
	g_modelViewer->SetStatusText(wxT("Preparing WMO Scene data..."));
	size_t scnr = EXPORT_OKAY;

	// Export only if the Object file was successfully written.
	bool writescene = false;
	if (objr == EXPORT_OKAY)
		writescene = true;

	// Option to write scene even if model fails/is not overwritten
	if (modelExport_LW_AlwaysWriteSceneFile == true)
		writescene = true;

	// Write the Scene File
	if (writescene == true){
		g_modelViewer->SetStatusText(wxT("Sending Scene data to be written..."));
		wxLogMessage(wxT("Writing Scene file..."));
		scnr = WriteLWScene(&Scene);
	}else{
		wxLogMessage(wxT("Scene file not written."));
	}
	wxLogMessage(wxT("Cleaning Scene File Data..."));
	Scene.~LWScene();

	wxLogMessage(wxT("Checking Scene error code..."));
	if (scnr != EXPORT_OKAY){
		g_modelViewer->SetStatusText(wxT("An error occured while writing the scene file."));
		wxLogMessage(wxT("An error occured while writing the scene file."));
		return scnr;
	}
	
	g_modelViewer->SetStatusText(wxT("Lightwave WMO Export complete!"));
	wxLogMessage(wxT("Lightwave WMO Export finished with no errors."));
	return EXPORT_OKAY;
}

// ADT Exporter
size_t ExportLWO_ADT(MapTile *m, const char *fn){
	g_modelViewer->SetStatusText(wxT("Preparing Lightwave ADT Exporter..."));
	wxString filename(fn, wxConvUTF8);
	wxString scfilename = wxString(fn, wxConvUTF8).BeforeLast('.') + wxT(".lws");
	LW_ObjCount.Reset();
	LW_LightCount.Reset();
	LW_BoneCount.Reset();
	LW_CamCount.Reset();

	if (modelExport_LW_PreserveDir == true){
		wxString Path, Name;

		// Object
		Path << filename.BeforeLast(SLASH);
		Name << filename.AfterLast(SLASH);
		MakeDirs(Path,wxT("Objects"));
		filename.Empty();
		filename << Path << SLASH << wxT("Objects") << SLASH << Name;

		// Scene
		Path.Empty();
		Name.Empty();
		Path << scfilename.BeforeLast(SLASH);
		Name << scfilename.AfterLast(SLASH);
		MakeDirs(Path,wxT("Scenes"));
		scfilename.Empty();
		scfilename << Path << SLASH << wxT("Scenes") << SLASH << Name;
	}
	if (modelExport_PreserveDir == true){
		wxString Path1, Path2, Name;

		// Objects
		Path1 << filename.BeforeLast(SLASH);
		Name << filename.AfterLast(SLASH);
		Path2 << m->name.BeforeLast(SLASH);
		MakeDirs(Path1,Path2);
		filename.Empty();
		filename << Path1 << SLASH << Path2 << SLASH << Name;

		// Scene
		Path1.Empty();
		Path2.Empty();
		Name.Empty();
		Path1 << scfilename.BeforeLast(SLASH);
		Name << scfilename.AfterLast(SLASH);
		Path2 << m->name.BeforeLast(SLASH);
		MakeDirs(Path1,Path2);
		scfilename.Empty();
		scfilename << Path1 << SLASH << Path2 << SLASH << Name;
	}

	// Scene Data
	LWScene Scene(scfilename.AfterLast(SLASH),scfilename.BeforeLast(SLASH));

	// Object Data
	LWObject Object = GatherADTforLWO(m,fn,Scene);
	if (Object.SourceType == wxEmptyString){
		wxMessageBox(wxT("Error gathering information for export."),wxT("Export Error"));
		wxLogMessage(wxT("Failure gathering information for export."));
		return EXPORT_ERROR_NO_DATA;
	}

	// Write LWO File
	wxLogMessage(wxT("Sending ADT Data to LWO Writing Function..."));
	size_t objr = WriteLWObject(filename, Object);

	if (objr != EXPORT_OKAY){
		if ((objr == EXPORT_ERROR_NO_DATA)||(objr == EXPORT_ERROR_FILE_ACCESS)){
			wxString msg = wxT("Error Writing the ADT file to a Lightwave Object.");
			wxMessageBox(msg,wxT("Writing Error"));
			wxLogMessage(msg);
		}else if (objr == EXPORT_ERROR_BAD_FILENAME){
			wxString msg = wxT("Bad Filename. Failed writing Lightwave Object.");
			wxMessageBox(msg,wxT("Writing Error"));
			wxLogMessage(msg);
		}else if (objr == EXPORT_ERROR_NO_OVERWRITE){
			wxLogMessage(wxT("Writing stopped. Did not overwrite the existing Lightwave Object file."));
		}else{
			wxString msg = wxT("Error Writing the ADT file to a Lightwave Object.");
			wxMessageBox(msg,wxT("Writing Error"));
			wxLogMessage(msg);
		}
		return objr;
	}else{
		wxLogMessage(wxT("LWO Object Writing Complete."));
	}
	Object.~LWObject();

	// Scene file writing not yet ready...
	Scene.~LWScene();

	g_modelViewer->SetStatusText(wxT("Lightwave ADT Export complete!"));
	wxLogMessage(wxT("Lightwave ADT Export finished with no errors."));
	return EXPORT_OKAY;
}
