/*!  \file kfbxlayeredtexture.h
 */
#ifndef FBXFILESDK_KFBXPLUGINS_KFBXLAYEREDTEXTURE_H 
#define FBXFILESDK_KFBXPLUGINS_KFBXLAYEREDTEXTURE_H

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

#include <fbxfilesdk/kfbxplugins/kfbxgroupname.h>
#include <fbxfilesdk/kfbxplugins/kfbxtexture.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

/** KFbxLayeredTexture is a combination of multiple textures(KFbxTexture) blended sequentially.
  * For example, you can access individual texture by:
  * \code
  * KFbxTexture* pIndiTexture = lLayeredTexture->GetSrcObject(KFbxTexture::ClassId, pTextureIndex);
  * \endcode
  * \nosubgrouping
  * \see KFbxTexture
  */
class KFBX_DLL KFbxLayeredTexture : public KFbxTexture
{
	KFBXOBJECT_DECLARE(KFbxLayeredTexture,KFbxTexture);

public:
	/** \enum EBlendMode Blend modes.
	  * - \e eTRANSLUCENT
	  * - \e eADDITIVE
	  * - \e eMODULATE
	  * - \e eMODULATE2
	  * - \e eOVER
	  */
	typedef enum 
	{
		eTRANSLUCENT,
		eADDITIVE,
		eMODULATE,
		eMODULATE2,
		eOVER
	} EBlendMode;

	/** Equivalence operator.
	  * \param pOther                      The object for comparison.
	  * \return                            \c True if pOther is equivalent to this object, returns \c false otherwise.
	  */
	bool operator==( const KFbxLayeredTexture& pOther ) const;

    /** Sets the blending mode of a specified texture.
      * \param pIndex                      The texture index.
      * \param pMode                       The blend mode to be set.
      * \return                            \c True if successful, returns \c false otherwise.
      */
    bool SetTextureBlendMode( int pIndex, EBlendMode pMode ); 

    /** Returns the blending mode of a specified texture
      * \param pIndex                      The texture index.
      * \param pMode                       The parameter that will hold the returned blend mode.
      * \return                            \c True if successful, returns \c false otherwise.
      */
    bool GetTextureBlendMode( int pIndex, EBlendMode& pMode ) const;

///////////////////////////////////////////////////////////////////////////////
//
//  WARNING!
//
//	Anything beyond these lines may not be documented accurately and is 
// 	subject to change without notice.
//
///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS

protected:

    struct InputData
    {
        EBlendMode mBlendMode;
    };

public:
    KArrayTemplate<InputData> mInputData;

protected:
    KFbxLayeredTexture(KFbxSdkManager& pManager, char const* pName);  

	virtual KFbxObject* Clone(KFbxObject* pContainer, KFbxObject::ECloneType pCloneType) const;

    virtual bool ConnecNotify (KFbxConnectEvent const &pEvent);

    bool RemoveInputData( int pIndex );
#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS
};

inline EFbxType FbxTypeOf( KFbxLayeredTexture::EBlendMode const &pItem )				{ return eENUM; }

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXLAYEREDTEXTURE_H

