/*!  \file kfbxanimcurvenode.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXANIMCURVENODE_H
#define FBXFILESDK_KFBXPLUGINS_KFBXANIMCURVENODE_H

/**************************************************************************************

 Copyright (C) 2010 Autodesk, Inc. and/or its licensors.
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

#include <fbxfilesdk/kfbxplugins/kfbxobject.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>
class KFbxAnimStack;
class KFbxAnimCurve;
static void CollectAnimFromCurveNode(KFCurve **lSrc, KFCurveNode *fcn, unsigned int nbCrvs, KFbxAnimCurveNode *cn);

/** This class is an composite of animation curves.
  * \nosubgrouping
  */
class KFBX_DLL KFbxAnimCurveNode : public KFbxObject
{
    KFBXOBJECT_DECLARE(KFbxAnimCurveNode, KFbxObject);

public:
    /**
      * \name Utility functions.
      *
      */
    //@{
        /** Returns true if animation keys are found.
          * \param pRecurse When \c true and this AnimCurveNode is composite, descend to the children.
          * \return \c true if at least one AnimCurve is found and contains animation keys.
          */
        bool IsAnimated(bool pRecurse=false) const;

        /** Find out start and end time of the animation.
          * This function retrieves the including time span for all
          * the Curve's time span of this curve node.
          * \param pStart Reference to receive start time.
          * \param pStop Reference to receive end time.
          * \return \c true on success, \c false otherwise.
          * \remarks \c false is also returned if this curve node has no animation.
          */
        bool GetAnimationInterval(KTime& pStart, KTime& pStop) const;

        /** Test this object to see if it is a composite KFbxAnimCurveNode or a "leaf".
          * A composite KFbxAnimCurveNode is an object whose src connections are only KFbxAnimCurveNode
          * and his channels property is totally empty.
          * \return \c true if this object is a composite, \c false otherwise.
          */
        bool IsComposite() const;

        /** Recursively look for the KFbxAnimCurveNode matching the passed named argument.
          * \param pName Name of the KFbxAnimCurveNode we are looking for.
          * \return The found anim curve node or NULL.
          * \remarks If pName is an empty string, this function automatically return NULL.
          */
        KFbxAnimCurveNode* Find(const char* pName);

        /** Create a KFbxAnimCurveNode compatible with the specified property data type.
          * \param pProperty Property to be compatible with.
          * \param pScene The scene the created KFbxAnimCurveNode will belong to.
          * \return The pointer to the newly created KFbxAnimCurveNode. Returns NULL if an error occurred. 
          * \remarks This function does not connect the newly created object to the property.
          * \remarks This function detects fbxDouble3, fbxDouble4 and fbxDouble44 properties DataTypes and
          *         automatically adds the required channels properties. Any other DataType is not 
          *         specifically processed and the channels properties are left empty and need to be filled
          *         using the AddChannel() function.
          */
        static KFbxAnimCurveNode* CreateTypedCurveNode(KFbxProperty& pProperty, KFbxScene* pScene);

        /** Get the total number of channels properties defined in the composite member.
          * \return The number of channels properties.
          */
        unsigned int GetChannelsCount() const;

        /** Get the index of the named channel.
          * \param pChannelName Name of the channel for which we want the index.
          * \return the index of the named channel or -1 if the name does not exist.
          */
        int GetChannelIndex(const char* pChannelName) const;

        /** Get the name of the channel.
          * \param pChannelId Index of the channel for which we want the name.
          * \return the name of the indexed channel or "" if the index is invalid.
          */
        KString GetChannelName(int pChannelId) const;

        /** Empties the composite member.
          * \remarks This function will remove all the channels added with the AddChannel() method
          *         regardless of their use and/or connections.
          */
        void ResetChannels();

        /** Adds the specified channel property.
          * \param pChnlName Channel name.
          * \param pValue Default value of the channel.
          * \return \c True if successful.
          * \remarks It is an error to try to add a channel that already exists.
          */
        template <class T> bool AddChannel(const char* pChnlName, T const &pValue)
        {
            if (!pChnlName || strlen(pChnlName)==0) return false;
            KFbxProperty c = GetChannel(pChnlName);
            if (c.IsValid()) 
            {
                return false;
            }

            mChannels.BeginCreateOrFindProperty();
            KFbxDataType dt = GetFbxDataType(FbxTypeOf(pValue));
            c = KFbxProperty::Create(mChannels, dt, pChnlName);
            KFbxSet<T>(c, pValue);
            mChannels.EndCreateOrFindProperty();
            return true;
        }

        /** Set the default value of the channel.
          * \param pChnlName Channel name.
          * \param pValue    New default value of this channel.
          */
        template <class T> void SetChannelValue(const char* pChnlName, T pValue)
        {
            KFbxProperty c = GetChannel(pChnlName);
            if (c.IsValid()) KFbxSet<T>(c, pValue);
        }

        /** Set the default value of the channel.
          * \param pChnlId   Channel index.
          * \param pValue    New default value of this channel.
          */
        template <class T> void SetChannelValue(unsigned int pChnlId, T pValue)
        {
            KFbxProperty c = GetChannel(pChnlId);
            if (c.IsValid()) KFbxSet<T>(c, pValue);
        }

        /** Get the default value of the channel.
          * \param pChnlName Channel name.
          * \param pInitVal  Value returned if the specified channel is invalid.
          * \return The default value of this channel.
          */
        template <class T> T GetChannelValue(const char* pChnlName, T pInitVal)
        {
            T v = pInitVal;
            KFbxProperty c = GetChannel(pChnlName);
            if (c.IsValid()) KFbxGet<T>(c, v);
            return v;
        }

        /** Get the default value of the channel.
          * \param pChnlId Channel index.
          * \param pInitVal  Value returned if the specified channel is invalid.
          * \return The default value of this channel.
          */
        template <class T> T GetChannelValue(unsigned int pChnlId, T pInitVal)
        {
            T v = pInitVal;
            KFbxProperty c = GetChannel(pChnlId);
            if (c.IsValid()) KFbxGet<T>(c, v);
            return v;
        }
    //@}

    /**
      * \name KFbxAnimCurve management.
      *
      */
    //@{
        /** Disconnect the AnimCurves from the channel.
          * \param pCurve       The curve to disconnect from the channel.
          * \param pChnlId      The channel index.
          * \return \c true if the disconnection was made, \c false if an error occurred.
          */
        bool DisconnectFromChannel(KFbxAnimCurve* pCurve, unsigned int pChnlId);

        /** Connects the given animation curve to the specified channel.
          * \param pCurve   The curve to connect to the channel.
          * \param pChnl    The name of the channel the curve is to be connected to.
          * \param pInFront When \c true, all the current connections are moved after this one.
          *                 making this one the first. By default, the connection is the last one.
          * \return \c true if the connection was made, \c false if an error occurred.
          */
        bool ConnectToChannel(KFbxAnimCurve* pCurve, const char* pChnl, bool pInFront = false);

        /** Connects the given animation curve to the specified channel.
          * \param pCurve   The curve to connect to the channel.
          * \param pChnlId  Index of the channel the curve is to be connected to.
          * \param pInFront When \c true, all the current connections are moved after this one.
          *                 making this one the first. By default, the connection is the last one.
          * \return \c true if the connection was made, \c false if an error occurred.
          * \remarks The index is 0 based.
          */
        bool ConnectToChannel(KFbxAnimCurve* pCurve, unsigned int pChnlId, bool pInFront = false);

        /** Creates a new curve and connects it to the specified channel of this curve node (or pCurveNodeName if this one is a composite).
          * \param pCurveNodeName Name of the KFbxAnimCurveNode we are looking for (if this one is a composite).
          * \param pChannel Channel identifier.
          * \return Pointer to the KFbxAnimCurve or NULL if an error occurred.
          * \remarks pCurveNodeName cannot be empty.
          * \remarks If the pChannel identifier is left NULL, use the first valid channel.
          */
        KFbxAnimCurve* CreateCurve(const char* pCurveNodeName, const char* pChannel);

        /** Creates a new curve and connects it to the specified channel of this curve node (or pCurveNodeName if this one is a composite).
          * \param pCurveNodeName Name of the KFbxAnimCurveNode we are looking for (if this one is a composite).
          * \param pChannelId Channel index.
          * \return Pointer to the KFbxAnimCurve or NULL if an error occurred.
          * \remarks pCurveNodeName cannot be empty.
          * \remarks If the pChannel identifier is left NULL, use the first valid channel.
          */
        KFbxAnimCurve* CreateCurve(const char* pCurveNodeName, unsigned int pChannelId = 0);
        
        /** Get the number of KFbxAnimCurve connected to the specified channel.
          * \param pChannelId Channel index.
          * \param pCurveNodeName Name of the KFbxAnimCurveNode we are looking for.
          * \return The number of animation curves on the specified channel or 0 if an error occurred.
          * \remarks This method fails if the KFbxAnimCurveNode does not exist.
          * \remarks It is an error if the specified channel cannot be found in this curve node.
          * \remarks If the pCurveNodeName is left NULL only look for the curves on this node even if it is a composite.
          */
        int GetCurveCount(unsigned int pChannelId, const char* pCurveNodeName = NULL);

        /** Get the KFbxAnimCurve of the specified channel.
          * \param pChannelId Channel index.
          * \param pId The index of the desired anim curve (in case there is more than one)
          * \param pCurveNodeName Name of the KFbxAnimCurveNode we are looking for (if this object is a composite).
          * \return Pointer to the KFbxAnimCurve that matches the criteria.
          * \remarks This method fails if the KFbxAnimCurveNode does not exist.
          * \remarks If the specified channel cannot be found in this curve node, 
          *         the function return NULL.
          * \remarks If the pCurveNodeName is left NULL only look for the curve on this node even if it is a composite.
          */
        KFbxAnimCurve* GetCurve(unsigned int pChannelId, unsigned int pId = 0, const char* pCurveNodeName = NULL);

     //@}

#ifndef DOXYGEN_SHOULD_SKIP_THIS
	///////////////////////////////////////////////////////////////////////////////
	//  WARNING!
	//	Anything beyond these lines may not be documented accurately and is 
	// 	subject to change without notice.
	///////////////////////////////////////////////////////////////////////////////

	virtual KFbxObject& Copy(const KFbxObject& pObject);

    void Evaluate(double* pData, KTime pTime);

    /**
      * \name Internal use only.
      * This section is for internal use and is subject to change without notice.
      */
    //@{
        
    /** Create the compatible KFCurveNode structure.
      * This method will create a temporary KFCurveNode object and all the necessary
      * children to represent all the connections of this KFbxAnimCurveNode.
      * Each call to this method will clear the previous content of the KFCurveNode but does not
      * destroy it. An explicit call to ReleaseKFCurveNode has to be made.
      *
      * \remarks The = operator WILL NOT copy the KFCurveNode
      *
      */
    KFCurveNode* GetKFCurveNode(bool pNoCreate=false);

    /** Destroy the KFCurveNode structure.
      * This method will destroy the KFCurveNode that has been created by the call to GetKFCurveNode.
      * \remarks See the source code for the specific behavior for the deletion of the KFCurves.
      */
    void ReleaseKFCurveNode();

    //! Sync the each channel property with the KFCurve's mValue stored in the KFCurveNode
    void SyncChannelsWithKFCurve();

    inline bool GetUseQuaternion() {return mUseQuaternion;}; 
	void SetUseQuaternion(bool pVal); 

    // sync the KFCurveNodeLayerType based on the property Datatype (see the source code for details)
    void SetKFCurveNodeLayerType(KFbxProperty& pProp);

    static const char* CurveNodeNameFrom(const char* pName);
    //@}

private:
    /** Access the specified channel property.
      * \param pChnl Name of the desired channel.
      * \return The corresponding channel property if found or an empty one.
      * \remarks The returned property must always be tested for its validity.
      * \remarks If pChnl is NULL this method returns the first channel property.
      */
    KFbxProperty GetChannel(const char* pChnl);

    /** Access the specified channel property.
      * \param pChnlId Index of the desired channel.
      * \return The corresponding channel property if found or an empty one.
      * \remarks The returned property must always be tested for its validity.
      * \remarks The index is 0 based.
      */
    KFbxProperty GetChannel(unsigned int pChnlId);

protected:
    KFbxAnimCurveNode(KFbxSdkManager& pManager, char const* pName);
    KFbxAnimCurveNode* Find(KFbxAnimCurveNode* pRoot, const KString& pName);
    virtual bool ConstructProperties( bool pForceSet );
    virtual void Destruct(bool pRecursive, bool pDependents);

private:
	friend class KFbxObject;
	friend void CollectAnimFromCurveNode(KFCurve **lSrc, KFCurveNode *fcn, unsigned int nbCrvs, KFbxAnimCurveNode *cn);
    unsigned char	mNonRemovableChannels;
    KFbxProperty	mChannels;
    KFbxProperty*	mCurrentlyProcessed;
    KFCurveNode*	mFCurveNode;
    bool*			mOwnedKFCurve;
    int				mKFCurveNodeLayerType;
    bool			mUseQuaternion;
	int*			mDirectIndexes;
	int				mDirectIndexesSize;

    KFbxAnimCurve* GetCurve(unsigned int pChannelId, unsigned int pId, KFbxAnimCurveNode* pCurveNode);
    bool ConnectToChannel(KFbxProperty& p, KFbxAnimCurve* pCurve, bool pInFront);
    void ResetKFCurveNode();
    void SyncKFCurveValue(KFbxAnimCurve* pCurve, double pVal);
	void ReleaseOwnershipOfKFCurve(int pIndex);

#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS
};
typedef KFbxAnimCurveNode* HKKFbxAnimCurveNode;

KFBX_DLL void GetAllAnimCurves(KFbxAnimStack* pAnimStack, KArrayTemplate<KFbxAnimCurve*>& pCurves);
KFBX_DLL void GetAllAnimCurves(KFbxObject* pObj, KFbxAnimStack* pAnimStack, KArrayTemplate<KFbxAnimCurve*>& pCurves);

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXANIMCURVENODE_H

