/*!  \file kfbxsurfacephong.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXSURFACEPHONG_H
#define FBXFILESDK_KFBXPLUGINS_KFBXSURFACEPHONG_H

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

#include <fbxfilesdk/kfbxplugins/kfbxsurfacelambert.h>
#include <fbxfilesdk/kfbxplugins/kfbxcolor.h>
#include <fbxfilesdk/kfbxplugins/kfbxgroupname.h>

#include <fbxfilesdk/components/kbaselib/klib/kerror.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

/** This class contains settings for Phong Materials.
  * \nosubgrouping
  */
class KFBX_DLL KFbxSurfacePhong : public KFbxSurfaceLambert
{
	KFBXOBJECT_DECLARE(KFbxSurfacePhong, KFbxSurfaceLambert);

public:
	/**
	 * \name Material properties
	 */
	//@{
	
	/** Returns the specular color property.
     *  \return The specular color.
	 */
	KFbxPropertyDouble3 GetSpecularColor() const;
	
	/** Returns the specular factor property. 
     *  This factor is used to attenuate the specular color.
	 * \return The specular factor.
	 */
	KFbxPropertyDouble1 GetSpecularFactor() const;
	
	/** Returns the shininess property. This property controls the aspect
	 * of the shiny spot. It is the specular exponent in the Phong
	 * illumination model.
     * \return The shininess.
	 */
	KFbxPropertyDouble1 GetShininess() const;
	
	/** Returns the reflection color property. This property is used to
	 * implement reflection mapping.
     * \return The reflection color.
	 */
	KFbxPropertyDouble3 GetReflectionColor() const;
	
	/** Returns the reflection factor property. This property is used to
	 * attenuate the reflection color.
     * \return The reflection factor.
	 */
	KFbxPropertyDouble1 GetReflectionFactor() const;
	
	//@}

	//////////////////////////////////////////////////////////////////////////
	// Static values
	//////////////////////////////////////////////////////////////////////////

	/**
	  * \name Default property values
	  */
	//@{
	
	static fbxDouble3 sSpecularDefault;
	static fbxDouble1 sSpecularFactorDefault;

	static fbxDouble1 sShininessDefault;
	
	static fbxDouble3 sReflectionDefault;
	static fbxDouble1 sReflectionFactorDefault;

    //@}

///////////////////////////////////////////////////////////////////////////////
//
//  WARNING!
//
//	Anything beyond these lines may not be documented accurately and is 
// 	subject to change without notice.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef DOXYGEN_SHOULD_SKIP_THIS

	// Clone
	virtual KFbxObject* Clone(KFbxObject* pContainer, KFbxObject::ECloneType pCloneType) const;

protected:
	KFbxSurfacePhong(KFbxSdkManager& pManager, char const* pName);

	virtual bool ConstructProperties(bool pForceSet);

	// From KFbxObject
	virtual KString		GetTypeName() const;

	// Local
	void Init();

	KFbxPropertyDouble3 Specular;
	KFbxPropertyDouble1 SpecularFactor;
	KFbxPropertyDouble1 Shininess;

	KFbxPropertyDouble3 Reflection;
	KFbxPropertyDouble1 ReflectionFactor;

#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS 

};

typedef KFbxSurfaceMaterial* HKFbxSurfaceMaterial;

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXSURFACEPHONG_H

