/*!  \file kfbxsurfacelambert.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXSURFACELAMBERT_H
#define FBXFILESDK_KFBXPLUGINS_KFBXSURFACELAMBERT_H

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

#include <fbxfilesdk/kfbxplugins/kfbxsurfacematerial.h>
#include <fbxfilesdk/kfbxplugins/kfbxcolor.h>
#include <fbxfilesdk/kfbxplugins/kfbxgroupname.h>

#include <fbxfilesdk/components/kbaselib/klib/kerror.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

/** This class contains settings for Lambert Materials.
  * \nosubgrouping
  */
class KFBX_DLL KFbxSurfaceLambert : public KFbxSurfaceMaterial
{
	KFBXOBJECT_DECLARE(KFbxSurfaceLambert,KFbxSurfaceMaterial);

public:
	/**
	 * \name Material properties
	 */
	//@{
	
	/** Returns the emissive color property.
     *  \return The emissive color.
	 */
	KFbxPropertyDouble3 GetEmissiveColor() const;
	
	/** Returns the emissive factor property. This factor is used to
	 *  attenuate the emissive color.
     *  \return The emissive factor.
	 */
	KFbxPropertyDouble1 GetEmissiveFactor() const;
	
	/** Returns the ambient color property.
     *  \return The ambient color.
	 */
	KFbxPropertyDouble3 GetAmbientColor() const;
	
	/** Returns the ambient factor property. This factor is used to
	 * attenuate the ambient color.
     *  \return The ambient factor.
	 */
	KFbxPropertyDouble1 GetAmbientFactor() const;
	
	/** Returns the diffuse color property.
     *  \return The diffuse color.
	 */
	KFbxPropertyDouble3 GetDiffuseColor() const;
	
	/** Returns the diffuse factor property. This factor is used to
	 * attenuate the diffuse color.
     *  \return The diffuse factor.
	 */
	KFbxPropertyDouble1 GetDiffuseFactor() const;
	
	/** Returns the bump property. This property is used to distort the
	 * surface normal and create the illusion of a bumpy surface.
     *  \return The bump property.
	 */
	KFbxPropertyDouble3 GetBump() const;

    /** Returns the bump factor property. This factor is used to
    * make a surface more or less bumpy.
    *  \return The bump factor.
    */
    KFbxPropertyDouble1 GetBumpFactor() const;
	
	/** Returns the transparent color property. This property is used to make a
	 * surface more or less transparent.
     *  \return The transparent color.
	 */
	KFbxPropertyDouble3 GetTransparentColor() const;
	
	/** Returns the transparency factor property. This property is used to make a
	 * surface more or less opaque (0 = opaque, 1 = transparent).
     *  \return The transparency factor.
	 */
	KFbxPropertyDouble1 GetTransparencyFactor() const;

	/** Returns the displacement color property. This property is used to make a
	 * surface more or less displaced.
     *  \return The displacement color.
	 */
	KFbxPropertyDouble3 GetDisplacementColor() const;
	
	/** Returns the displacement factor property. This property is used to make a
	 * surface more or less displaced.
     *  \return The displacement factor
	 */
	KFbxPropertyDouble1 GetDisplacementFactor() const;
	
	//@}

	//////////////////////////////////////////////////////////////////////////
	// Static values
	//////////////////////////////////////////////////////////////////////////

	/**
	  * \name Default property values
	  */
	//@{

	static fbxDouble3 sEmissiveDefault;
	static fbxDouble1 sEmissiveFactorDefault;

	static fbxDouble3 sAmbientDefault;
	static fbxDouble1 sAmbientFactorDefault;

	static fbxDouble3 sDiffuseDefault;
	static fbxDouble1 sDiffuseFactorDefault;
	
	static fbxDouble3 sBumpDefault;
    static fbxDouble3 sNormalMapDefault;
    static fbxDouble1 sBumpFactorDefault;

	static fbxDouble3 sTransparentDefault;
	static fbxDouble1 sTransparencyFactorDefault;

    static fbxDouble3 sDisplacementDefault;
    static fbxDouble1 sDisplacementFactorDefault;

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
	KFbxSurfaceLambert(KFbxSdkManager& pManager, char const* pName);

	virtual bool ConstructProperties(bool pForceSet);

	// From KFbxObject
	virtual KString		GetTypeName() const;

	// Local
	void Init();

	KFbxPropertyDouble3 Emissive;
	KFbxPropertyDouble1 EmissiveFactor;
	
	KFbxPropertyDouble3 Ambient;
	KFbxPropertyDouble1 AmbientFactor;
	
	KFbxPropertyDouble3 Diffuse;
	KFbxPropertyDouble1 DiffuseFactor;
	
	KFbxPropertyDouble3 Bump;
	KFbxPropertyDouble3 NormalMap;
    KFbxPropertyDouble1 BumpFactor;

	KFbxPropertyDouble3 TransparentColor;
	KFbxPropertyDouble1 TransparencyFactor;

    KFbxPropertyDouble3 DisplacementColor;
    KFbxPropertyDouble1 DisplacementFactor;

#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS 

};

typedef KFbxSurfaceMaterial* HKFbxSurfaceMaterial;

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXSURFACELAMBERT_H

