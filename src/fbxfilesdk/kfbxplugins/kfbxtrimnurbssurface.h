/*!  \file kfbxtrimnurbssurface.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXTRIMNURBSSURFACE_H
#define FBXFILESDK_KFBXPLUGINS_KFBXTRIMNURBSSURFACE_H

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

#include <fbxfilesdk/kfbxplugins/kfbxgeometry.h>
#include <fbxfilesdk/kfbxplugins/kfbxnurbssurface.h>
#include <fbxfilesdk/kfbxplugins/kfbxnurbscurve.h>
#include <fbxfilesdk/kfbxplugins/kfbxgenericnode.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

/** KFbxBoundary describes a trimming boundary for a trimmed NURBS object.
  * Note:Outer boundaries run counter-clockwise in UV space and inner
  * boundaries run clockwise. An outer boundary represents the outer edges
  * of the trimmed surface whereas the inner boundaries define "holes" in
  * the surface.
  */
class KFBX_DLL KFbxBoundary : public KFbxGeometry
{
    KFBXOBJECT_DECLARE(KFbxBoundary,KFbxGeometry);

public:

    //! Properties
    static const char* sOuterFlag;

    /** This property handles outer flag.
    *
    * Default value is false.
    */
    KFbxTypedProperty<fbxBool1> OuterFlag;

    /** Adds an edge to this boundary.
      * \param pCurve       The curve to be appended to the end of this boundary
      */
    void AddCurve( KFbxNurbsCurve* pCurve );

    /** Returns the number of edges within this boundary.
      * \return             The number of edges within this boundary
      */
    int GetCurveCount() const;

    /** Returns the edge at the specified index.
      * \param pIndex       The specified index, no bound checking is done.
      * \return             The edge at the specified index if
      *                     pIndex is in the range [0, GetEdgeCount() ),
      *                     otherwise the return value is undefined.
      */
    KFbxNurbsCurve* GetCurve( int pIndex );

    /** Returns the edge at the specified index.
      * \param pIndex       The specified index, no bound checking is done.
      * \return             The edge at the specified index if
      *                     pIndex is in the range [0, GetEdgeCount() ),
      *                     otherwise, the return value is undefined.
      */
    KFbxNurbsCurve const* GetCurve( int pIndex ) const;


    //! Returns the type of node attribute.
    virtual EAttributeType GetAttributeType() const;

    /** Detects if the point is in the boundary's control hull.
      * \param pPoint       The point to be detected.
      * \return             \c True if the point is in the boundary's control hull, returns \c false if it is not in the control hull.
      */
    bool IsPointInControlHull(const KFbxVector4& pPoint );

    /** Computes the origin point in the boundary
      * \return             The origin point.
      */
    KFbxVector4 ComputePointInBoundary();

#ifndef DOXYGEN_SHOULD_SKIP_THIS
///////////////////////////////////////////////////////////////////////////////
//
//  WARNING!
//
//  Anything beyond these lines may not be documented accurately and is
//  subject to change without notice.
//
///////////////////////////////////////////////////////////////////////////////

    virtual KFbxObject& Copy(const KFbxObject& pObject);
    virtual KFbxObject* Clone(KFbxObject* pContainer, KFbxObject::ECloneType pCloneType = eDEEP_CLONE) const;

protected:
    KFbxBoundary(KFbxSdkManager& pManager, char const* pName);
    virtual void Construct(const KFbxBoundary* pFrom);

    virtual KString GetTypeName() const;
    void Reset();

    bool LineSegmentIntersect( const KFbxVector4 & pStart1, const KFbxVector4 & pEnd1,
                               const KFbxVector4 & pStart2, const KFbxVector4 & pEnd2 ) const;

public:
    void ClearCurves();
    void CopyCurves( KFbxBoundary const& pOther );
    bool IsValid(bool mustClosed = true);
    bool IsCounterClockwise();

#endif // DOXYGEN_SHOULD_SKIP_THIS
};


/** KFbxTrimNurbsSurface describes a NURBS surface with regions
    trimmed or cut away with trimming boundaries.
  */
class KFBX_DLL KFbxTrimNurbsSurface : public KFbxGeometry
{
    KFBXOBJECT_DECLARE(KFbxTrimNurbsSurface,KFbxGeometry);
public:
    //! Returns the type of node attribute.
    virtual EAttributeType GetAttributeType() const;


    /** Returns the number of regions on this trimmed NURBS surface.
      * Note: There is always at least one region.
      * \return             The number of regions
      */
    int GetTrimRegionCount() const;

    /** Calls this function before adding boundaries to a new trim region.
      * The number of regions is incremented on this call.
      */
    void BeginTrimRegion();

    /** Calls this function after the last boundary for a given region is added.
      * If no boundaries are added between the calls to BeginTrimRegion
      * and EndTrimRegion, the last region is removed.
      */
    void EndTrimRegion();

    /** Appends a trimming boundary to the set of trimming boundaries.
      * The first boundary specified for a given trim region should be
      * the outer boundary. All other boundaries are inner boundaries.
      * This must be called after a call to BeginTrimRegion(). Boundaries
      * cannot be shared among regions. Duplicate the boundary if necessary.
      * \param pBoundary        The boundary to add.
      * \return                 \c True if the boundary is added successfully.
      *                         If the boundary is not added successfully, returns \c false.
      */
    bool              AddBoundary( KFbxBoundary* pBoundary );

    /** Returns the boundary at a given index for the specified region
      * \param pIndex           The index of the boundary to retrieve, no bound checking is done.
      * \param pRegionIndex     The index of the region which is bordered by the boundary.
      * \return                 The trimming boundary at index pIndex,
      *                         if pIndex is in the range [0, GetBoundaryCount() ),
      *                         otherwise the result is undefined.
      */
    KFbxBoundary*     GetBoundary( int pIndex, int pRegionIndex = 0 );

    /** Returns the boundary at a given index for the specified region
      * \param pIndex           The index of the boundary to retrieve, no bound checking is done.
      * \param pRegionIndex     The index of the region which is bordered by the boundary.
      * \return                 The trimming boundary at index pIndex,
      *                         if pIndex is in the range [0, GetBoundaryCount() ),
      *                         otherwise the result is undefined.
      */
    KFbxBoundary const*     GetBoundary( int pIndex, int pRegionIndex = 0 ) const;

    /** Returns the number of boundaries for a given region.
	  * \param pRegionIndex     The index of the region. 
      * \return                 The number of trim boundaries for the given region.
      */
    int               GetBoundaryCount(int pRegionIndex = 0) const;

    /** Sets the NURBS surface that is trimmed by the trimming boundaries.
      * \param pNurbs           The NURBS surface to be trimmed.
      */
    void       SetNurbsSurface( KFbxNurbsSurface const* pNurbs );

    /** Returns the NURBS surface that is trimmed by the trim boundaries.
      * \return                 A pointer to the (untrimmed) NURBS surface.
      */
    KFbxNurbsSurface* GetNurbsSurface();

    /** Returns the NURBS surface that is trimmed by the trim boundaries.
      * \return                 A pointer to the (untrimmed) NURBS surface.
      */
    KFbxNurbsSurface const* GetNurbsSurface() const;

    /** Sets the flag which indicates whether the surface normals are flipped. 
      * You can flip the normals of the surface to reverse the surface.
      * \param pFlip            If \c true, the surface is reversed. If it is false, the surface is not reversed.
      */
    inline void SetFlipNormals( bool pFlip ) { mFlipNormals = pFlip; }

    /** Checks if the normals are flipped.
      * \return                 \c True if normals are flipped, returns \c false if they are not flipped.
      */
    inline bool GetFlipNormals() const { return  mFlipNormals; }



    /**
      * \name Shape Management
      */
    //@{

    /** Adds a shape and its associated name to the NURBS surface.
      * Shapes on trim NURBS are stored on the untrimmed surface.
      * Thus, this is the same as calling GetNurbsSurface()->AddShape().
      * See KFbxGeometry::AddShape() for a description of the method.
      * \param pShape           A pointer to the shape object.
      * \param pShapeName       The name given to the shape.
	  * \return -1
      */
    virtual int AddShape(KFbxShape* pShape, char const* pShapeName);

    /** Removes all shapes without destroying them.
      * Shapes on trim NURBS are stored on the untrimmed surface.
      * Thus, this is the same as calling GetNurbsSurface()->ClearShape().
      * See KFbxGeometry::ClearShape() for a description of the method.
      */
    virtual void ClearShape();

    /** Returns the number of shapes.
      * Shapes on trim NURBS are stored on the untrimmed surface.
      * Thus, this is the same as calling GetNurbsSurface()->GetShapeCount().
      * See KFbxGeometry::GetShapeCount() for a description of the method.
      * \return                 The number of added shapes.          
      */
    virtual int GetShapeCount() const;

    /** Returns the shape found at the specified index.
      * Shapes on trim NURBS are stored on the untrimmed surface.
      * Thus, this is the same as calling GetNurbsSurface()->GetShape().
      * See KFbxGeometry::GetShape() for a description of the method.
      * \param pIndex           The specified shape index.
      * \return                 The shape at the specified index.
      */
    virtual KFbxShape* GetShape(int pIndex);

    /** Returns the shape found at the specified index.
      * Shapes on trim NURBS are stored on the untrimmed surface.
      * Thus, this is the same as calling GetNurbsSurface()->GetShape().
      * See KFbxGeometry::GetShape() for a description of the method.
      * \param pIndex           The specified shape index.
      * \return                 The shape at the specified index.
      */
    virtual KFbxShape const* GetShape(int pIndex) const;


    /** Returns the shape name found at the specified index.
      * Shapes on trim NURBS are stored on the untrimmed surface.
      * Thus, this is the same as calling GetNurbsSurface()->GetShapeName().
      * See KFbxGeometry::GetShapeName() for a description of the method.
      * \param pIndex           The specified shape index.
      * \return                 The shape name found at the specified index.
      */
    virtual char const* GetShapeName(int pIndex) const;



    /** Returns a shape channel.
      * Shapes on trim NURBS are stored on the untrimmed surface.
      * Thus, this is the same as calling GetNurbsSurface()->GetShapeChannel()
      * See KFbxGeometry::GetShapeChannel() for a description of the method.
      * \param pIndex           Shape index.
      * \param pLayer           The animation layer to get the requested animation curve.
      * \param pCreateAsNeeded  If \c true, the animation curve is created if it is not already present.
      */
    virtual KFbxAnimCurve* GetShapeChannel(int pIndex, KFbxAnimLayer* pLayer, bool pCreateAsNeeded = false);

    /** Returns a shape channel.
      * This method is deprecated and should not be used anymore. Instead, you can call the equivalent using KFbxAnimLayer.
     * \param pIndex
      * \param pCreateAsNeeded
      * \param pTakeName
      */
    virtual KFCurve* GetShapeChannel(int pIndex, bool pCreateAsNeeded = false, char const* pTakeName = NULL);
    //@}

     /** Returns the count of the NURBS surface's control points.
       * \return                The NURBS surface's control points count.
       */
    virtual int GetControlPointsCount() const;

    /** Sets the control point and the normal values for a specified index.
      * \param pCtrlPoint         The value of the control point.
      * \param pNormal            The value of the normal.
      * \param pIndex             The specified index.
      */
    virtual void SetControlPointAt(KFbxVector4 &pCtrlPoint, KFbxVector4 &pNormal , int pIndex);

     //!Returns the NURBS surface's control points.
    virtual KFbxVector4* GetControlPoints() const;

#ifndef DOXYGEN_SHOULD_SKIP_THIS
///////////////////////////////////////////////////////////////////////////////
//
//  WARNING!
//
//  Anything beyond these lines may not be documented accurately and is
//  subject to change without notice.
//
///////////////////////////////////////////////////////////////////////////////

    virtual KFbxObject& Copy(const KFbxObject& pObject);
    virtual KFbxObject* Clone(KFbxObject* pContainer, KFbxObject::ECloneType pCloneType = eDEEP_CLONE) const;

    /**
      * \param mustClosed     boundary curve must be closed
      */
    bool IsValid(bool mustClosed = true);

protected:
    KFbxTrimNurbsSurface(KFbxSdkManager& pManager, char const* pName);

public:
    virtual KString GetTypeName() const;

    void ClearBoundaries();

    void CopyBoundaries( KFbxTrimNurbsSurface const& pOther );

    bool IsValid(int pRegion, bool mustClosed = true);

    void RebuildRegions();

private:
    bool mFlipNormals;

    KArrayTemplate<int> mRegionIndices;

    bool mNewRegion;

#endif // DOXYGEN_SHOULD_SKIP_THIS
};

typedef KFbxTrimNurbsSurface*   HKFbxTrimNurbsSurface;
typedef KFbxBoundary*           HKFbxBoundary;

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXTRIMNURBSSURFACE_H

