#ifndef EXPORTERS_H
#define EXPORTERS_H

// Exporter Data
struct Exporter_Type {
	wxString Name;
	size_t ID;
	wxString MenuText;
	bool canM2;
	bool canWMO;
	bool canADT;

	Exporter_Type(wxString name, size_t id, wxString menutext, bool can_m2 = false, bool can_wmo = false, bool can_adt = false){
		Name = name;
		ID = id;
		MenuText = menutext;
		canM2 = can_m2;
		canWMO = can_wmo;
		canADT = can_adt;
	}
};

// Exporters
const static size_t ExporterTypeCount = 9;

// This list should be alphabetical.
// Format: Exporter_Type(Exporter Name, Exporter_ID, Export Menu Text, Exports M2, Exports WMO, Exports ADT),
const static Exporter_Type Exporter_Types[ExporterTypeCount] = {
	Exporter_Type(wxT("Collada"),ID_MODELEXPORT_COLLADA,wxT("Collada...")),
#ifdef	_WINDOWS
	Exporter_Type(wxT("FBX"),ID_MODELEXPORT_FBX,wxT("FBX..."),true),
#else
	Exporter_Type(wxT("FBX"),ID_MODELEXPORT_FBX,wxT("FBX...")),	// Disabled like Collada until we get it working on non-windows systems.
#endif
#ifdef _DEBUG
	Exporter_Type(wxT("Lightwave 3D"),ID_MODELEXPORT_LWO,wxT("Lightwave 3D..."),true,true,true),
#else
	Exporter_Type(wxT("Lightwave 3D"),ID_MODELEXPORT_LWO,wxT("Lightwave 3D..."),true,true),
#endif
	Exporter_Type(wxT("M3"),ID_MODELEXPORT_M3,wxT("M3..."),true),
	Exporter_Type(wxT("Milkshape"),ID_MODELEXPORT_MS3D,wxT("Milkshape..."),true),
	Exporter_Type(wxT("Ogre XML"),ID_MODELEXPORT_OGRE,wxT("Ogre XML..."),true),
	Exporter_Type(wxT("Wavefront OBJ"),ID_MODELEXPORT_OBJ,wxT("Wavefront OBJ..."),true,true),
	Exporter_Type(wxT("X3D"),ID_MODELEXPORT_X3D,wxT("X3D..."),true),
	Exporter_Type(wxT("X3D in XHTML"),ID_MODELEXPORT_XHTML,wxT("X3D in XHTML..."),true),
};


#endif