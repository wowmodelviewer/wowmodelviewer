/*!  \file kfbxskin.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXSKIN_H
#define FBXFILESDK_KFBXPLUGINS_KFBXSKIN_H

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

#include <fbxfilesdk/kfbxplugins/kfbxdeformer.h>
#include <fbxfilesdk/kfbxmath/kfbxmatrix.h>
#include <fbxfilesdk/kfbxplugins/kfbxgroupname.h>

#include <fbxfilesdk/components/kbaselib/klib/kerror.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

class KFbxSdkManager;
class KFbxGeometry;
class KFbxCluster;
class KFbxNode;

/** FBX SDK skin class
  * \nosubgrouping
  */

class KFBX_DLL KFbxSkin : public KFbxDeformer
{
    KFBXOBJECT_DECLARE(KFbxSkin,KFbxDeformer);

public:
    /** Set deformation accuracy.
      * \param pDeformAccuracy         value for deformation accuracy.
      */
    void SetDeformAccuracy(double pDeformAccuracy);

    /** Get deformation accuracy.
      * \return                        deformation accuracy value.
      */
    double GetDeformAccuracy() const;

    /** Set the geometry affected by this skin deformer.
      * \param pGeometry               Pointer to the geometry object to set.
      * \return                        \c true on success, \c false otherwise.
      */
    bool SetGeometry(KFbxGeometry* pGeometry);

    /** Get the geometry affected by this skin deformer.
      * \return                        a pointer to the geometry if set or NULL.
      */
    KFbxGeometry* GetGeometry();

    /** Add a cluster.
      * \param pCluster                Pointer to the cluster object to add.
      * \return                        \c true on success, \c false otherwise.
      */
    bool AddCluster(KFbxCluster* pCluster);

    /** Remove cluster at given index.
      * \param pCluster                Pointer to the cluster to remove from this skin deformer.
      * \return                        Pointer to cluster or \c NULL if pCluster is not owned by this skin deformer.
      */
    KFbxCluster* RemoveCluster(KFbxCluster* pCluster);

    /** Get the number of clusters.
      * \return                        Number of clusters that have been added to this object.
      */
    int GetClusterCount() const;

    /** Get cluster at given index.
      * \param pIndex                  Index of cluster.
      * \return                        Pointer to cluster or \c NULL if index is out of range.
      */
    KFbxCluster* GetCluster(int pIndex);

    /** Get cluster at given index.
      * \param pIndex                  Index of cluster.
      * \return                        Pointer to cluster or \c NULL if index is out of range.
      */
    KFbxCluster const* GetCluster(int pIndex) const;

    /** Get the type of the deformer.
      * \return                        Deformer type identifier.
      */
    EDeformerType GetDeformerType()  const {return eSKIN; };

///////////////////////////////////////////////////////////////////////////////
//
//  WARNING!
//
//  Anything beyond these lines may not be documented accurately and is
//  subject to change without notice.
//
///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS

    virtual KFbxObject& Copy(const KFbxObject& pObject);
    virtual KFbxObject* Clone(KFbxObject* pContainer, KFbxObject::ECloneType pCloneType) const;

protected:
    KFbxSkin(KFbxSdkManager& pManager, char const* pName);

    virtual KString     GetTypeName() const;
    virtual KStringList GetTypeFlags() const;

    // Skin deformer
    double mDeformAccuracy;

    friend class KFbxScene;

#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS
};

typedef KFbxSkin* HKFbxSkin;

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXSKIN_H

