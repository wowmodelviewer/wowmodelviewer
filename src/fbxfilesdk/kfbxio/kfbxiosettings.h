/*!  \file kfbxiosettings.h
 */

#ifndef FBXFILESDK_KFBXIO_KFBXIOSETTINGS_H
#define FBXFILESDK_KFBXIO_KFBXIOSETTINGS_H

/**************************************************************************************

 Copyright (C) 2001 - 2010 Autodesk, Inc. and/or its licensors.
 All Rights Reserved.

 The coded instructions, statements, computer programs, and/or related material 
 (collectively the "Data") in these files contain unpublished information 
 proprietary to Autodesk, Inc. and/or its licensors, which is protected by 
 Canada and United States of America federal copyright law and by international 
 treaties. 
 
 The Data may not be disclosed or distributed to third parties, in whole or in
 part, without the prior written consent of Autodesk, Inc. ("Autodesk").

 THE DATA IS PROVIDED "AS IS" AND WITHOUT WARRANTY.
 ALL WARRANTIES ARE EXPRESSLY EXCLUDED AND DISCLAIMED. AUTODESK MAKES NO
 WARRANTY OF ANY KIND WITH RESPECT TO THE DATA, EXPRESS, IMPLIED OR ARISING
 BY CUSTOM OR TRADE USAGE, AND DISCLAIMS ANY IMPLIED WARRANTIES OF TITLE, 
 NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE OR USE. 
 WITHOUT LIMITING THE FOREGOING, AUTODESK DOES NOT WARRANT THAT THE OPERATION
 OF THE DATA WILL BE UNINTERRUPTED OR ERROR FREE. 
 
 IN NO EVENT SHALL AUTODESK, ITS AFFILIATES, PARENT COMPANIES, LICENSORS
 OR SUPPLIERS ("AUTODESK GROUP") BE LIABLE FOR ANY LOSSES, DAMAGES OR EXPENSES
 OF ANY KIND (INCLUDING WITHOUT LIMITATION PUNITIVE OR MULTIPLE DAMAGES OR OTHER
 SPECIAL, DIRECT, INDIRECT, EXEMPLARY, INCIDENTAL, LOSS OF PROFITS, REVENUE
 OR DATA, COST OF COVER OR CONSEQUENTIAL LOSSES OR DAMAGES OF ANY KIND),
 HOWEVER CAUSED, AND REGARDLESS OF THE THEORY OF LIABILITY, WHETHER DERIVED
 FROM CONTRACT, TORT (INCLUDING, BUT NOT LIMITED TO, NEGLIGENCE), OR OTHERWISE,
 ARISING OUT OF OR RELATING TO THE DATA OR ITS USE OR ANY OTHER PERFORMANCE,
 WHETHER OR NOT AUTODESK HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH LOSS
 OR DAMAGE. 

**************************************************************************************/
#include <fbxfilesdk/fbxfilesdk_def.h>

#include <fbxfilesdk/components/kbaselib/klib/karrayul.h>
#include <fbxfilesdk/components/kbaselib/klib/kstring.h>

#include <fbxfilesdk/kfbxplugins/kfbxobject.h>

#include <fbxfilesdk/components/libxml2-2.6.4/libxml.h>
#include <fbxfilesdk/components/libxml2-2.6.4/include/libxml/parser.h>
#include <fbxfilesdk/components/libxml2-2.6.4/include/libxml/tree.h>
#include <fbxfilesdk/components/libxml2-2.6.4/include/libxml/globals.h>

// undef the macro mkdir, since it conflict with function mkdir in Qt\4.2.3\src\corelib\io\qdir.h
#if (defined(_MSC_VER) || defined(__MINGW32__)) && defined(mkdir)
	#undef mkdir
#endif

#include <fbxfilesdk/kfbxplugins/kfbxtypes.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

#include <fbxfilesdk/kfbxio/kfbxiosettingspath.h>

#define IOSVisible    true
#define IOSHidden     false

#define IOSSavable    true
#define IOSNotSavable false

#define IOSEnabled    true
#define IOSDisabled   false

#define IOSBinary     0
#define IOSASCII      1

class KFbxIOSettings;

// class to handle old StreamOptions information
class KsoInfo
{

public: enum EIMPEXP
{
    eIMPORT = 0,
    eEXPORT
};

public:
    //! Default constructor.
    KsoInfo();

    //! Set values to default 
    void Reset(EIMPEXP pImpExp);
    
	//! If pTimeMode is set to KTime::eDEFAULT_MODE, pCustomFrameRate is used instead.
    void SetTimeMode(KTime::ETimeMode pTimeMode, double pCustomFrameRate = 0.0);

	//! A time mode set to KTime::eDEFAULT_MODE means a custom frame rate is set.
    KTime::ETimeMode GetTimeMode(){ return mTimeMode; }

	//! Get frame period associated with time mode or a custom frame period if time mode is set to KTime::eDEFAULT_MODE.
    KTime GetFramePeriod();
	
    void SetASFScene(KFbxObject* pASFScene, bool pASFSceneOwned = false);
    KFbxObject* GetASFScene(){ return mASFScene; }

	void Set_IOS(KFbxIOSettings *pios){ios = pios;}

    //-----------------
    // data

private: 
        KTime::ETimeMode mTimeMode;
        KFbxObject * mASFScene;

        EIMPEXP mImpExp;

		KFbxIOSettings *ios;
};

class KFbxSdkManager;


// http://www.microsoft.com/globaldev/reference/winxp/langtla.mspx

//  LOCALE                            TLA  LCID

//  English - United States           ENU  0409
//  German - Germany                  DEU  0407
//  French - France                   FRA  040c
//  Japanese - Japan                  JPN  0411
//  Korean(Extended Wansung) - Korea  KOR  0412
//  Chinese - PRC                     CHS  0804

enum FBXUILANGUAGE
{
    eENU = 0,			        // MAX 409 English - United States
	eDEU,           	        // MAX 407 German - Germany
	eFRA,            	        // MAX 40c French - France
	eJPN,           	        // MAX 411 Japanese - Japan
    eKOR,            	        // MAX 412 Korean(Extended Wansung) - Korea 
    eCHS,	                    // MAX 804 Chinese - PRC
    eFBXUILANGUAGE_COUNT
};

// this class is located in the UserDataPtr of each KFbxIOSettings properties
class PropInfo
{
public:
	PropInfo();
    ~PropInfo();

	void            * UIWidget;            // UI widget for showing the property
	void            * cbValueChanged;      // call back when value changed
    void            * cbDirty;             // call back when value changed
    KStringList       labels;              // list of labels in many languages
};

/** KFbxIOSettings
*	Class used as container for IO settings.
*   All settings are organized in a property hierarchy.
*   A node of settings may contain a bunch of information related to an operation (ex: import / export).
*   It may also contain informations related to a user dialog offering user options.
*/
class KFBX_DLL KFbxIOSettings : public KFbxObject
{
	KFBXOBJECT_DECLARE(KFbxIOSettings,KFbxObject);

public:
	virtual void Destruct(bool pRecursive, bool pDependents);

        /** Add a property group under the root prop
		  * \param pName
		  * \param pDataType
		  * \param pLabel
		  */
	KFbxProperty AddPropertyGroup(char const* pName, KFbxDataType const &pDataType=KFbxDataType(), char const* pLabel="");

		/** Add a property group under another parent prop
		  * \param pParentProperty
		  * \param pName
		  * \param pDataType
		  * \param pLabel
		  * \param pVisible
		  * \param pSavable
		  * \param pEnabled
		  */
public: KFbxProperty AddPropertyGroup(	KFbxProperty const &pParentProperty, 
			                          	char const*		    pName, 
									  	KFbxDataType const &pDataType      = KFbxDataType(), 
									  	char const*         pLabel         = "",
									  	bool				pVisible	   = true,
									  	bool				pSavable	   = true,
									  	bool				pEnabled	   = true
									 );

		/** Add a property under another parent prop with a value to set
		  * \param pParentProperty
		  * \param pName
		  * \param pDataType
		  * \param pLabel
		  * \param pValue
		  * \param pValueType
		  * \param pVisible
		  * \param pSavable
		  * \param pEnabled
		  */		  
public: KFbxProperty AddProperty(    KFbxProperty const     &pParentProperty, 
			                         char const*            pName, 
								     KFbxDataType const     &pDataType      = KFbxDataType(), 
								     char const*            pLabel          = "",
									 void const*            pValue          = NULL,
								     EFbxType               pValueType      = eUNIDENTIFIED,
									 bool					pVisible		= true,
									 bool					pSavable		= true,
									 bool					pEnabled		= true
								   );

		/** Add a property under another parent prop with a value to set and a min max values
		  * \param pParentProperty
		  * \param pName
		  * \param pDataType
		  * \param pLabel
		  * \param pValue
		  * \param pMinValue
		  * \param pMaxValue
		  * \param pValueType
		  * \param pVisible
		  * \param pSavable
		  * \param pEnabled
		  */		  		  
public: KFbxProperty AddPropertyMinMax(    KFbxProperty const     &pParentProperty, 
			                         char const*            pName, 
								     KFbxDataType const     &pDataType      = KFbxDataType(), 
								     char const*            pLabel          = "",
									 void const*            pValue          = NULL,
									 double const*			pMinValue		= NULL,
									 double const*			pMaxValue		= NULL,
								     EFbxType               pValueType      = eUNIDENTIFIED,									 
									 bool					pVisible		= true,
									 bool					pSavable		= true,
									 bool					pEnabled		= true
								   );


		/** get a prop by description ex: "Export|Animation|Bake"
		  * \param pName
		  */
public: KFbxProperty GetProperty(char const* pName) const;

		/** get prop by description from a parent property
		  * \param pParentProperty
		  * \param pName
		  */
public: KFbxProperty GetProperty(KFbxProperty const &pParentProperty, char const* pName) const;

        /** get a bool prop by prop path
		  * \param pName
		  * \param pDefValue
		  */
public: bool GetBoolProp(char const* pName, bool pDefValue) const;

        /** set a bool prop by prop path
		  * \param pName
		  * \param pValue
		  */
public: void SetBoolProp(char const* pName, bool pValue);

        /** get a double prop by prop path
		  * \param pName
		  * \param pDefValue
		  */
public: double GetDoubleProp(char const* pName, double pDefValue) const;

        /** set a double prop by prop path
		  * \param pName
		  * \param pValue
		  */
public: void   SetDoubleProp(char const* pName, double pValue);

        /** get a int prop by prop path
		  * \param pName
		  * \param pDefValue
		  */
public: int    GetIntProp(char const* pName, int pDefValue) const;

        /** set a int prop by prop path
		  * \param pName
		  * \param pValue
		  */
public: void   SetIntProp(char const* pName, int pValue);

        /** get a KTime prop by prop path
		  * \param pName 
		  * \param pDefValue
		  */
public: KTime  GetKTimeProp(char const* pName, KTime pDefValue) const;

        /** set a KTime prop by prop path
		  * \param pName 
		  * \param pValue
		  */
public: void   SetKTimeProp(char const* pName, KTime pValue);

        /** get an enum prop by prop path
		  * \param pName
		  * \param pDefValue
		  */
public: KString GetEnumProp(char const* pName, KString pDefValue) const;

        /** get an enum prop by prop path
		  * \param pName
		  * \param pDefValue
		  */
public: int     GetEnumProp(char const* pName, int pDefValue) const;

        /** get an enum prop by prop path
		  * \param pName
		  * \param pValue
		  */
public: int     GetEnumIndex(char const* pName, KString pValue) const;

		/** set an enum prop by prop path
		  * \param pName
		  * \param pValue
		  */
public: void    SetEnumProp(char const* pName, KString pValue);

		/** set an enum prop by prop path
		  * \param pName
		  * \param pValue
		  */
public: void    SetEnumProp(char const* pName, int pValue);

		/** remove an enum prop by enum value
		  * \param pName
		  * \param pValue
		  */
public: void    RemoveEnumPropValue(char const* pName, KString pValue);

		/** empty all the values of the enum prop
                        * \param pName
		  */
public: void    EmptyEnumProp(char const* pName);

		/** check enum value if present
		  * \param &pProp a ref to an enum prop
		  * \param &enumString s ref to string
		  */
public: bool	IsEnumExist(KFbxProperty& pProp, KString& enumString) const;

		/** get enum index
		  * \param &pProp a ref to an enum prop
		  * \param &enumString s ref to string
		  * \param pNoCase 
		  */
public: int		GetEnumIndex(KFbxProperty& pProp, KString& enumString, bool pNoCase = false) const;

		/** set a specific flag value on a specific property.
		  * \param pName
		  * \param propFlag
		  * \param pValue
		  */
public: bool    SetFlag(char const* pName, FbxPropertyFlags::eFbxPropertyFlags propFlag, bool pValue);

        /** get a string prop by prop path
		  * \param pName
		  * \param pDefValue
		  */
public: KString GetStringProp(char const* pName, KString pDefValue) const;

        /** set a string prop by prop path
		  * \param pName
		  * \param pValue
		  */
public: void    SetStringProp(char const* pName, KString pValue);

		/** Load the settings hierarchy from an XML file
		  * \param path The path of the XML file.
		  * \return \c True on success, \c false otherwise.
		  */
public:	virtual bool ReadXMLFile(KString& path);

		/** Write the settings hierarchy to an XML file
		  * \param path The path of the XML file.
          * \return \c True on success, \c false otherwise.
		  */
public:	virtual bool WriteXMLFile(KString& path);

		/** Write the settings hierarchy to an XML file
		  * \param pFullPath The path of the XML file.
		  * \param propPath a prop Path
          * \return \c True on success, \c false otherwise.
		  */
public: bool WriteXmlPropToFile(KString& pFullPath, KString& propPath);



///////////////////////////////////////////////////////////////////////////////
//
//  WARNING!
//
//  Anything beyond these lines may not be documented accurately and is
//  subject to change without notice.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef DOXYGEN_SHOULD_SKIP_THIS

// protected constructor
protected:	KFbxIOSettings(KFbxSdkManager& pManager,char const* pName);

// the caller must delete the KString returned
private:	KString* GetAttributeFromXmlNode(xmlNode* node, KString* attName);

// the caller must delete the KString returned
private:	KString* GetInnerTextXmlNode(xmlNode* node);

private:	void CreatePropRecursive(xmlNode* pNode, KFbxProperty& pPropParent);
public:		virtual KFbxObject* Clone(KFbxObject* pContainer, KFbxObject::ECloneType pType) const;

private:	xmlNode* GetChildXmlNode(xmlNode* parent, KString* nodeName);
private:	xmlNode* AddChildXmlNode(xmlNode* parent, KString* nodeName, KString* nodeValue = NULL);
private:	xmlAttr* AddAttributeToXmlNode(xmlNode* node, KString* attName, KString* attValue = NULL);

private:    void AddNewPropInfo(KFbxProperty &pProp);
private:    void DeletePropInfo(KFbxProperty &pProp);
private:    void DeleteAllPropInfo(KFbxProperty &pProp);

public:     PropInfo * GetPropInfo(KFbxProperty &pProp);

public:     FBXUILANGUAGE UILanguage;
public:     KString GetLanguageLabel(KFbxProperty &pProp);
public:     void SetLanguageLabel(KFbxProperty &pProp, KString &pLabel);
public:     FBXUILANGUAGE Get_Max_Runtime_Language(KString regLocation);

public:     KsoInfo impInfo;
public:     KsoInfo expInfo;

			// add recurively a xml node equivalent for a KFbxProperty and all child KFbxProperties
private:	void AddXMLProp(KFbxProperty& pProp, xmlNodePtr pParent, xmlDocPtr pXmlDoc);

			// create a new xmlNode with all KFbxProperty info
private:	xmlNodePtr GetXMLNode(KFbxProperty& pProp);

public:		static KString GetUserMyDocumentDir();
public:		void SetPropVisible(KFbxProperty &pProp, bool pWithChildren, bool pVisible);

public:		virtual bool ConstructProperties(bool pForceSet);

			// Read an XML file from MyDocument dir
public:		bool ReadXmlPropFromMyDocument(KString& subDir, KString& filename);

			// Write property branch to an XML file in MyDocument dir
public:		bool WriteXmlPropToMyDocument(KString& subDir, KString& filename, KString& propPath);

public:	enum ELoadMode         { eMERGE, eEXCLUSIVE_MERGE, eCREATE };
public: enum EQuaternionMode   { eAS_QUATERNION, eAS_EULER, eRESAMPLE };
public: enum EObjectDerivation { eBY_LAYER, eBY_ENTITY, eBY_BLOCK }; 

public:	enum ESysUnits
	{
		kUNITS_USER,
		kUNITS_INCHES, 
		kUNITS_FEET,
		kUNITS_YARDS,
		kUNITS_MILES,
		kUNITS_MILLIMETERS,
		kUNITS_CENTIMETERS,
		kUNITS_METERS,
		kUNITS_KILOMETERS
	};

public:	enum ESysFrameRate  
	{
		kFRAMERATE_USER,
		kFRAMERATE_HOURS,
		kFRAMERATE_MINUTES,
		kFRAMERATE_SECONDS,
		kFRAMERATE_MILLISECONDS,
		kFRAMERATE_GAMES_15,
		kFRAMERATE_FILM_24,
		kFRAMERATE_PAL_25,
		kFRAMERATE_NTSC_30,
		kFRAMERATE_SHOWSCAN_48,
		kFRAMERATE_PALFIELD_50,
		kFRAMERATE_NTSCFIELD_60
	};
    
// Max
public:	enum EEnveloppeSystem
	{
		eSKIN_MODIFIER,
		ePHYSIQUE,
		eBONESPRO,
		eENVELOPPE_SYSTEM_COUNT
	};

// Max
public: enum EGeometryType
	{
		eTRIANGLE,
		eSIMPLIFIED_POLY,
		ePOLY,
		eNURB,
		ePATCH,
		eGEOMETRY_TYPE_COUNT
	};

#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS

};

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXIO_KFBXIOSETTINGS_H

