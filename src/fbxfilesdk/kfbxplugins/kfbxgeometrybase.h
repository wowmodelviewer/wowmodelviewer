/*!  \file kfbxgeometrybase.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXGEOMETRYBASE_H
#define FBXFILESDK_KFBXPLUGINS_KFBXGEOMETRYBASE_H

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

#include <fbxfilesdk/kfbxmath/kfbxvector4.h>

#include <fbxfilesdk/kfbxplugins/kfbxlayercontainer.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

class KFbxSdkManager;

/** This class is the base class for geometric object such as meshes, NURBS and patches.
  * Use the KFbxGeometryBase class to manage the control points, normals, binormals and tangents of the
  * geometries. 
  * The meaning of "control point" is dependant of the geometry object type. For meshes, the "control point"
  * is the physical 3D coordinate of polygon vertices while, for NURBS, it is the the actual control point on the curves
  * defining the surface. This class also allow you to define normals, binormals and tangents regardless of the type of 
  * geometric object. However, in reality, applying such definitions to NURBS and patches does not make much sense
  * since these definitions would only exist at the control points and inbetween them, the interpolation would certainly not
  * follow the curve. 
  *
  * Geometric objects are using a system of layered data to extend their construction definition. For example, a typical 
  * layer for a Mesh contains Normals, UVs and Materials but client applications can decide to define another set of 
  * Normals and UVs and swap them during the rendering phase to produce some different results. The combinations are limitless
  * and it would be impossible to discuss them all. This example has been presented to show one possible context where
  * layers can be used. More information can be found in the KFbxLayerContainer and KFbxLayer classes description.
  * \nosubgrouping
  */
class KFBX_DLL KFbxGeometryBase : public KFbxLayerContainer
{
    KFBXOBJECT_DECLARE(KFbxGeometryBase,KFbxLayerContainer);
public:

    /**
      * \name Control Points, Normals, Binormals and Tangent Management.
      */
    //@{

    /** Allocates memory space for the array of control points.
      * \param pCount     The number of control points.
      * \remarks          Any previously allocated array of control points will be cleared.
      */
    virtual void InitControlPoints(int pCount);

    /** Allocates memory space for the array of normals.
      * \param pCount     The desired size for the normal array. If pCount is specified, the array will be the same size as pCount.
      *                   If pCount is not specified, the array will be the same length as the array of control points.
      * \remarks          This function must be called after function KFbxLayerContainer::InitControlPoints().
      * \remarks          The normals initialized with this function will have the ReferenceMode set to eDIRECT. Also,
      *                   the array will always be defined on layer 0.
      */
    void InitNormals(int pCount = 0 );

    /** Allocates memory space for the array of normals cloned from the pSrc.
      * \param pSrc       The source geometry from which the normals information is cloned.
      * \remarks          This function must be called with the argument, otherwise it does not do anything. Also,
      *                   it will only process the normals array defined on layer 0 of the pSrc.
      */
    void InitNormals(KFbxGeometryBase* pSrc);

    /** Allocates memory space for the array of tangents on specified layer.
      * \param pCount      The desired size of the tangent array. If pCount is specified, the array will be the same size as pCount.
      *                    If pCount is not specified, the array will be the same length as the array of control points.
	  * \param pLayerIndex The specified layer index to allocate memory space for the array of tangents.
	  * \param pName       The specified name for the allocated tangents array.
      * \remarks           This function must be called after function KFbxLayerContainer::InitControlPoints().
      *                    The tangents initialized with this function will have the reference mode set to eDIRECT.
      */
    void InitTangents(int pCount = 0, const int pLayerIndex = 0, const char* pName = "" );

    /** Allocates memory space for the array of tangents cloned from the pSrc on the specified layer.
      * \param pSrc        The source geometry from which the tangents information is cloned.
	  * \param pLayerIndex The specified layer index to allocate memory space for cloned array of tangents from the pSrc.
      * \remarks           This function must be called with the argument, otherwise it does not do anything.
      */
    void InitTangents(KFbxGeometryBase* pSrc, const int pLayerIndex = 0);

    /** Allocates memory space for the array of binormals.
      * \param pCount      The desired size of the binormal array. If pCount is specified, the array will have the same size as pCount.
      *                    If pCount is not specified, the array will be the same length as the array of control points.
	  * \param pLayerIndex The specified layer index to allocate memory space for the array of binormals.
	  * \param pName       The specified name for the allocated binormals array.
      * \remarks           This function must be called after function KFbxLayerContainer::InitControlPoints().
      *                    The binormals initialized with this function will have the reference mode set to eDIRECT.
      */
    void InitBinormals(int pCount = 0, const int pLayerIndex = 0, const char* pName = "" );

    /** Allocates memory space for the array of binormals cloned from the pSrc.
      * \param pSrc        The source geometry from which the binormals information is cloned.
	  * \param pLayerIndex The specified layer index to allocate memory space for cloned array of binormals from the pSrc.
      * \remarks           This function must be called with the argument, otherwise it does not do anything.
      */
    void InitBinormals(KFbxGeometryBase* pSrc, const int pLayerIndex = 0);

    /** Sets the control point and the normal values at the specified index.
      * \param pCtrlPoint     The value of the control point.
      * \param pNormal        The value of the normal.
      * \param pIndex         The specified index of the control point/normal.
      * \param pI2DSearch     When \c true AND the normals array reference mode is eINDEX_TO_DIRECT, search pNormal in the
      *                       existing array to avoid inserting if it already exist. NOTE: This feature uses a linear
      *                       search algorithm, therefore it can be time consuming if the DIRECT array of normals contains
      *                       a huge number of elements.
      * \remarks              If the arrays (control points and normals) are not big enough to store the values at the 
	  *                       specified index, they will be automatically resized to accomodate the new entries.
      */
    virtual void SetControlPointAt(const KFbxVector4 &pCtrlPoint , const KFbxVector4 &pNormal , int pIndex, bool pI2DSearch = false);


    /** Sets the control point at a specified index.
    * \param pCtrlPoint     The value of the control point.
    * \param pIndex         The specified index of the control point.
    *
    * \remarks              If the array is not big enough to store the value at the specified index, it will be 
	*						automatically resized to accomodate the new entry.
    */
    virtual void SetControlPointAt(const KFbxVector4 &pCtrlPoint , int pIndex);

    /** Gets the control point at the specified index.
    * \param pIndex         The specified index of the control point.
    * \return               The value of the specific control point.
    *
    * \remarks              If index is out of range, KFbxVector4(0, 0, 0) is returned.
    */
    virtual KFbxVector4 GetControlPointAt(int pIndex) const;

    /** Sets the control point normal value at the specified index.
    * \param pNormal        The value of the normal.
    * \param pIndex         The specified index of the normal.
    * \param pI2DSearch     When \c true AND the normals array reference mode is eINDEX_TO_DIRECT, search pNormal in the
    *                       existing array to avoid inserting it if it already exist. NOTE: this feature uses a linear
    *                       search algorithm, therefore it can be time consuming if the DIRECT array of normals contains
    *                       a huge number of elements.
    * \remarks              If the array is not big enough to store the value at the specified index, it will be 
	*						automatically resized to accomodate the new entry.
    */
    virtual void SetControlPointNormalAt(const KFbxVector4 &pNormal, int pIndex, bool pI2DSearch=false);

    /** Returns the number of control points.
      * \return     The number of control points allocated in the geometry.
      */
    virtual int GetControlPointsCount() const;


    /** Returns a pointer to the array of control points.
      * \return      Pointer to the array of control points, or \c NULL if the array has not been allocated.
      * \remarks     Use the function KFbxGeometryBase::InitControlPoints() to allocate the array.
      */
    virtual KFbxVector4* GetControlPoints() const;

    /** Allocates memory space for the array of control points.
      * \param pCount     The number of control points.
      * \remarks          Any previously allocated array of control points will NOT be cleared.
      */
	virtual void SetControlPointCount(int pCount);

    //@}


    /**
      * \name Public and fast access Properties
      */
    //@{
        //! The minimum value of the control points bounding box. 
        KFbxTypedProperty<fbxDouble3>               BBoxMin;

        //! The maximum value of the control points bounding box.
        KFbxTypedProperty<fbxDouble3>               BBoxMax;

        /** Computes the control points Bounding box.
          */
        void ComputeBBox();
    //@}

    /**
      * \name Off-loading Serialization section
      *  The methods in this section are typically called by a peripheral (KFbxPeripheral). There should be no
	  *  real interest in calling them directly. The functions will write/read the memory dump of the data contained
	  *  in this class. Each data block written/read will start with an (int) value representing the number of items
	  *  in the array. If this value (v) is not zero, it will be followed by the array content. A block of data that is
	  *  (v * sizeof(array item size)) bytes big. The methods will also call the parent class ones to dump the Layers content.
      */
    //@{
        /** Writes the content of the geometry object to the specified stream.
          * \param pStream The destination stream.
          * \return \c True if the content is successfully processed
          * by the receiving stream, \c false otherwise.
          */
        virtual bool ContentWriteTo(KFbxStream& pStream) const;

        /** Reads the content of the geometry object from the specified stream.
          * \param pStream The source stream.
          * \return \c True if the geometry object fills itself with the received data
          * from the stream successfully, \c false otherwise.
          */
        virtual bool ContentReadFrom(const KFbxStream& pStream);
    //@}

        /** Calculate the actual amount of memory used by this geometry object. 
          * \return The memory size in bytes (includes the amount use by the data defined in the layers).
          */
        virtual int MemoryUsage() const;

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

	/** 
	  * \name Utility methods
	  * All the methods in this section are wrappers used to simplify the internal code 
	  * of the FBX SDK. It is strongly recommended that clients access the data using the 
	  * appropriate public Layer methods instead since they provide more control and are 
	  * less subject to changes. For example, to access the direct array of normals, 
	  * instead of calling \e GetNormals(&anArray) do:
	  * \code
	  * GetLayer(layer)->GetNormals()->GetDirectArray();
	  * \endcode
	  *     
	  * and to access the index array:
	  * \code
	  * GetLayer(layer)->GetNormals()->GetIndexArray();
	  * \endcode
	  */
	 //@{	
	//! Get an access to the Normals direct array defined on the layer 0.
    bool GetNormals(KFbxLayerElementArrayTemplate<KFbxVector4>** pLockableArray) const;
	//! Get an access to the Normals index array defined on the layer 0.
    bool GetNormalsIndices(KFbxLayerElementArrayTemplate<int>** pLockableArray) const;
	//! Get access to the Tangents direct array defined on the specified layer.
    bool GetTangents(KFbxLayerElementArrayTemplate<KFbxVector4>** pLockableArray, const int pLayerIndex = 0) const;
	//! Get access to the Tangents index array defined on the specified layer.
    bool GetTangentsIndices(KFbxLayerElementArrayTemplate<int>** pLockableArray, const int pLayerIndex = 0) const;
	//! Get access to the Binormals direct array defined on the specified layer.
    bool GetBinormals(KFbxLayerElementArrayTemplate<KFbxVector4>** pLockableArray, const int pLayerIndex = 0) const;
	//! Get access to the Binormals index array defined on the specified layer.
    bool GetBinormalsIndices(KFbxLayerElementArrayTemplate<int>** pLockableArray, const int pLayerIndex = 0) const;
	//@}

protected:
    KFbxGeometryBase(KFbxSdkManager& pManager, char const* pName);

    virtual bool ConstructProperties(bool pForceSet);
	//! Empties all the data (control points and layers) of the geometry object.
    virtual void ContentClear();

public:
    KArrayTemplate<KFbxVector4> mControlPoints;

protected:
    friend class KFbxGeometryConverter;
#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS

};

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXGEOMETRYBASE_H

