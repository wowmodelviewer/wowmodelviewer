/*!  \file kfbxanimstack.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXANIMSTACK_H
#define FBXFILESDK_KFBXPLUGINS_KFBXANIMSTACK_H

/**************************************************************************************

 Copyright (C) 2009 - 2010 Autodesk, Inc. and/or its licensors.
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

#include <fbxfilesdk/components/kbaselib/klib/ktime.h>
#include <fbxfilesdk/fbxcore/fbxcollection/kfbxcollection.h>

// these symbols are defined for backward compatibility
#define KFBXTAKENODE_DEFAULT_NAME           "Default"
#define KFBXTAKENODE_ROOT_CURVE_NODE_NAME   "Defaults"

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

class KFbxTakeInfo;
class KFbxThumbnail;
class KFbxAnimEvaluator;

/** The Animation stack is a collection of animation layers. The Fbx document can have one or 
  * more animation stacks. Each stack can be viewed as one "take" in the previous versions of the FBX SDK. 
  * The "stack" terminology comes from the fact that the object contains 1 to n animation layers that 
  * are evaluated according to their blending modes to produce a resulting animation for a given attribute.
  * \nosubgrouping
  */
class KFBX_DLL KFbxAnimStack : public KFbxCollection
{
    KFBXOBJECT_DECLARE(KFbxAnimStack, KFbxCollection);

public:
    //////////////////////////////////////////////////////////////////////////
    //
    // Properties
    //
    //////////////////////////////////////////////////////////////////////////
    /** This property stores a description string of this animation stack.
      * This string can be used to display, in a human readable format, information 
      * relative to this animation stack object. 
      * Default value is "".
      * \remarks The applications using the FBX SDK are not required to manipulate this information.
      */
    KFbxTypedProperty<fbxString> Description;

    /** This property stores the local time span "Start" time.
      * This "start" time should be seen as a time marker. Typically it would represent the whole animation
      * starting time but its use (and update) is left to the calling application (with one exception occurring
      * in the BakeLayers). The FBX SDK do not use this value internally an only guarantee that it will be stored 
      * to the FBX file and retrieved from it. 
      *
      * Default value is 0.
      */
    KFbxTypedProperty<fbxTime> LocalStart;

    /** This property stores the local time span "Stop" time.
      * This "stop" time should be seen as a time marker. Typically it would represent the whole animation
      * ending time but its use (and update) is left to the calling application (with one exception occurring 
      * in the BakeLayers). The FBX SDK do not use this value internally an only guarantee that it will be stored 
      * to the FBX file and retrieved from it. 
      *
      * Default value is 0
      */
    KFbxTypedProperty<fbxTime> LocalStop;

    /** This property stores the reference time span "Start" time.
      * This reference start time is another time marker that can be used by the calling application. The FBX SDK 
      * never use it and only guarantees that this value is stored in the FBX file and retrieved from it. 
      *
      * Default value is 0
      */
    KFbxTypedProperty<fbxTime> ReferenceStart;

    /** This property stores the reference time span "Stop" time.
      * This reference start time is another time marker that can be used by the calling application. The FBX SDK 
      * never use it and only guarantees that this value is stored in the FBX file and retrieved from it. 
      *
      * Default value is 0
      */
    KFbxTypedProperty<fbxTime> ReferenceStop;
  
    /** Reset the object time spans either to their default values or from the pTakeInfo structure, if provided.
      * \param pTakeInfo The take info to be used during reset.
      */
    void Reset(const KFbxTakeInfo* pTakeInfo = NULL);

    /**
      * \name Utility functions.
      *
      */
    //@{
        /** Get the LocalStart and LocalStop time properties as a KTimeSpan.
          * \return The current local time span.
          */
        KTimeSpan GetLocalTimeSpan() const;

        /** Set the LocalStart and LocalStop time properties from a KTimeSpan.
          * \param pTimeSpan The new local time span.
          */
        void SetLocalTimeSpan(KTimeSpan& pTimeSpan);

        /** Get the ReferenceStart and ReferenceStop time properties as a KTimeSpan.
          * \return The current reference time span.
          */
        KTimeSpan GetReferenceTimeSpan() const;

        /** Set the ReferenceStart and ReferenceStop time properties from a KTimeSpan.
          * \param pTimeSpan The new reference time span.
          */
        void SetReferenceTimeSpan(KTimeSpan& pTimeSpan);

        /** Get the thumbnail image associated to this animation stack.
          * This method exists for legacy reasons. In the newer FBX files, there can only be one 
          * thumbnail image and it belongs to the KFbxDocument.
          * \return Pointer to the thumbnail.
          */
        KFbxThumbnail* GetTakeThumbnail();

        /** Set the take thumbnail.
          * This method exists for legacy reasons. In the newer FBX files, there can only be one 
          * thumbnail image and it belongs to the KFbxDocument.
          * \param pTakeThumbnail The referenced thumbnail object.
          */
        void SetTakeThumbnail(KFbxThumbnail* pTakeThumbnail);

        /** Bake all the animation layers on the base layer.
          * This function will process all the properties on every animation layer and will generate a resampled set of
          * animation keys (representing the layers's evaluated result) on the base layer. Once this operation is completed
          * successfully, all the layers (except the base one) are destroyed. Properties that are only defined on the base 
          * layer will remain unaffected by the resampling. The stack local timespan is updated with the overall animation range.
          * 
          * \param pEvaluator The layer evaluator. This is the engine that evaluates the overall result of any given
          *                   property according to the layers flags.
          * \param pStart   The start time of the resampling range.
          * \param pStop    The stop time of the resampling range.
          * \param pPeriod  The time increment for the resampling.
          * \return \c True if the operation was successful and \c false in case of errors.
          * \remarks If this AnimStack contains only one AnimLayer, the function will return false and do nothing.
          */
        bool BakeLayers(KFbxAnimEvaluator* pEvaluator, KTime pStart, KTime pStop, KTime pPeriod);
                        
    //@}

#ifndef DOXYGEN_SHOULD_SKIP_THIS
    ///////////////////////////////////////////////////////////////////////////////
    //  WARNING!
    //    Anything beyond these lines may not be documented accurately and is 
    //     subject to change without notice.
    ///////////////////////////////////////////////////////////////////////////////
    virtual KFbxObject& Copy(const KFbxObject& pObject);

protected:
    KFbxThumbnail* mTakeThumbnail;    
    KFbxAnimStack(KFbxSdkManager& pManager, char const* pName, KError* pError=0);

    virtual bool ConstructProperties(bool pForceSet);
    virtual KFbxAnimStack* GetAnimStack();

private:

    mutable KError*                             mError;
        
    friend class KFbxObject;
#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS
};

typedef KFbxAnimStack* HKKFbxAnimStack;

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXANIMSTACK_H

