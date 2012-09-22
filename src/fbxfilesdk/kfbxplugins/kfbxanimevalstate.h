/*!  \file kfbxanimevalstate.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXEVALUATION_STATE_H
#define FBXFILESDK_KFBXPLUGINS_KFBXEVALUATION_STATE_H

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
#include <fbxfilesdk/kfbxplugins/kfbxnode.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

class KMBTransform;
class KFbxNodeEvalState;
struct KFbxAnimEvalState_prv;

/** This class hold results from animation evaluations. To re-use an evaluation state, it is possible to invalidate
  * or to reset it. Invalidating an evaluation state is the quickest way to re-use an evaluation state object for the
  * same scene with the same objects, because it only zeros all the entries, while a reset will delete all entries.
  * Unless the scene change, it is recommended to invalidate evaluation states instead of resetting them, for performance
  * purposes.
  * \internal
  * \see KFbxAnimEvaluator
  */
class KFBX_DLL KFbxAnimEvalState
{
public:
    /** Constructor.
      */
    KFbxAnimEvalState();

    /** Get the time of this evaluation state.
      * \return The time associated with this evaluation state.
      */
    KTime GetTime() const;

    /** Reset an evaluation state by deleting the cache it contains. This will remove all entries in the cache.
      */
    void Reset();

    /** Invalidate an evaluation state by zeroing the cache it contains, and changing its associated time. All
	  * node and property entries will remain in the list, but will become in an not-evaluated state.
      * \param pTime The time at which the evaluation state should be set after the invalidation.
      */
    void Invalidate(KTime pTime);

    /** Invalidate a node evaluation state.
      * \param pNode The node that needs to be re-evaluated in next evaluation.
      */
	void InvalidateNode(KFbxNode* pNode);

    /** Invalidate a property evaluation state.
      * \param pProperty The property that needs to be re-evaluated in next evaluation.
      */
	void InvalidateProperty(KFbxProperty& pProperty);

    /** Get node transform evaluation result from the evaluation state.
      * \param pNode The node for which the value was stored.
      * \param pDirectIndex Index to retrieve the information in the evaluation state array, to speed up performance. (Use -1 if index is unknown).
      * \param pNeedEval The function will set this parameter to \c true if the value in the state needs a re-evaluation.
      * \return The global or local matrix transform for the specified node.
      */
	KFbxNodeEvalState* GetNodeTransform(KFbxNode* pNode, int& pDirectIndex, bool& pNeedEval);

	/** Set a node evaluate state to evaluated.
	  * \param pDirectIndex The index of the node in the evaluation state.
	  */
	void SetNodeEvaluated(int pDirectIndex);

    /** Get a property evaluation result from the evaluation state.
      * \param pProperty The property for which the value was stored.
      * \param pDirectIndex Index to retrieve the information in the evaluation state array, to speed up performance. (Use -1 if index is unknown).
	  * \param pNeedEval The function will set this parameter to \c true if the value in the state needs a re-evaluation.
	  * \param pScene The FBX scene used for evaluation
      * \return The result value that was stored.
      * \remarks This function is not well suited for real-time applications, since it
      *             performs a find in the array. But its reliable and simple to use.
      */
    KFbxAnimCurveNode* GetPropertyValue(KFbxProperty& pProperty, int& pDirectIndex, bool& pNeedEval, KFbxScene* pScene);

	/** Set a property evaluate state to evaluated.
	  * \param pDirectIndex The index of the property in the evaluation state.
	  */
	void SetPropertyEvaluated(int pDirectIndex);

///////////////////////////////////////////////////////////////////////////////
//
//  WARNING!
//
//    Anything beyond these lines may not be documented accurately and is 
//     subject to change without notice.
//
///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    virtual ~KFbxAnimEvalState();

private:
    KFbxAnimEvalState_prv* mData;
#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS 
};

///////////////////////////////////////////////////////////////////////////////
//
//  WARNING!
//
//    Anything beyond these lines may not be documented accurately and is 
//     subject to change without notice.
//
///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/** This class hold results for node evaluation.
  * \nosubgrouping
  */
class KFBX_DLL KFbxNodeEvalState
{
public:
	KFbxNodeEvalState(KFbxNode* pNode);

	/** mLT is used to hold result value of LclTranslation property from node evaluation.
	*/
	KFbxVector4         mLT;
	/** mLR is used to hold result value of LclRotation property from node evaluation.
	*/
	KFbxVector4         mLR;
	/** mLS is used to hold result value of LclScaling property from node evaluation.
	*/
	KFbxVector4         mLS;

	/** mLX is used to hold result local transform matrix from node evaluation.
	* Pivots, offsets, pre/post rotation and all other transforms are taken into consideration.
	*/
	KFbxXMatrix			mLX;
	/** mGX is used to hold result global transform matrix from node evaluation.
	* Pivots, offsets, pre/post rotation and all other transforms are taken into consideration.
	*/	
	KFbxXMatrix			mGX;
	/** mMBX is used to hold the corresponding KMBTransform of the node.
	* This KMBTransform takes all transform-related info, including pivots, offsets, pre/post rotation, rotation order, limits, etc.
	* The evaluation is actually done through the utility functions of KMBTransform.
	*/
	KMBTransform*		mMBX;

	int					mParentIndex, mTargetIndex, mTargetUpIndex;
	KFbxAnimCurveNode*	mCurveNode[3];
	KFbxAnimLayer*		mLayer;
};

#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS 

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif /* FBXFILESDK_KFBXPLUGINS_KFBXEVALUATION_STATE_H */
