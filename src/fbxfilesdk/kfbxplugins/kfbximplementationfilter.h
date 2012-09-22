/*!  \file kfbximplementationfilter.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXIMPLEMENTATIONFILTER_H
#define FBXFILESDK_KFBXPLUGINS_KFBXIMPLEMENTATIONFILTER_H

/**************************************************************************************

 Copyright (C) 2001 - 2009 Autodesk, Inc. and/or its licensors.
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

#include <fbxfilesdk/kfbxplugins/kfbxobjectfilter.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

class KFbxCriteria;

/** \brief This object represents a shading node filter criteria 
  * based on the shading node implementation.
  * \nosubgrouping
  */
class KFBX_DLL KFbxImplementationFilter : public KFbxObjectFilter
{

public:

	static const char * sCHR_ANY_SHADING_API;
	static const char * sCHR_ANY_SHADING_API_VERSION;
	static const char * sCHR_ANY_SHADING_LANGUAGE;
	static const char * sCHR_ANY_SHADING_LANGUAGE_VERSION;


    /** Constructor
	  * \param pShadingAPI a string containing the implementation API name:
	  *		MentalRay
	  *		OpenGL
	  *		DirectX
	  *
	  * \param pShadingAPIVersion a string containing the implementation API version:
	  *		eg. 1.0
	  *
	  * \param pShadingLanguage a string identifying the implementation language name:
	  *		GLSL	= GL Shading Language
	  *		HLSL	= High Level Shading Language
	  *		CGFX	= CG effect(NVidia)
	  *		RIB		= RenderMan (RIB)
	  *		etc...
	  *
	  * \param pShadingLanguageVersion a string identifying the implementation language version:
	  *		eg. 1.0
	  *
	  * \remarks by default the created criteria correspond to any shader
	  */
	KFbxImplementationFilter(
		const char * pShadingAPI				= sCHR_ANY_SHADING_API,
		const char * pShadingAPIVersion			= sCHR_ANY_SHADING_API_VERSION,
		const char * pShadingLanguage			= sCHR_ANY_SHADING_LANGUAGE,
		const char * pShadingLanguageVersion	= sCHR_ANY_SHADING_LANGUAGE_VERSION
	);

    //! Destructor.
    virtual ~KFbxImplementationFilter();

	/** Tells if this filter matches the given shading node implementation
	  * \param pObjectPtr The given shading node implementation
	  */
	virtual bool Match(const KFbxObject * pObjectPtr) const;

	//! Stores the shading API
	KString mShadingAPI;

	//! Stores the shading API Version
	KString mShadingAPIVersion;

	//! Stores the shading language
	KString mShadingLanguage;

	//! Stores the shading language version
	KString mShadingLanguageVersion;

///////////////////////////////////////////////////////////////////////////////
//
//  WARNING!
//
//	Anything beyond these lines may not be documented accurately and is 
// 	subject to change without notice.
//
///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS

	/** Utility method to determine if the given object is a shading node
	  * that we recognize.
	  */
	static bool IsShadingObject( const KFbxObject* pObject );

	/** Returns a criteria suitable for use with querying connections 
	  * to shading nodes that we recognize, on KFbxObjects.
	  */
	static KFbxCriteria Criteria();
private:

#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS
};

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXIMPLEMENTATIONFILTER_H

