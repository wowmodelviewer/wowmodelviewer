/*!  \file kfbxanimlayer.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXANIMLAYER_H
#define FBXFILESDK_KFBXPLUGINS_KFBXANIMLAYER_H

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

#include <fbxfilesdk/fbxcore/fbxcollection/kfbxcollection.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

class KFbxAnimCurveNode;

/** The animation layer is a collection of animation curve nodes. Its purpose is to store
  * a variable number of KFbxAnimCurveNodes. The class provides different states flags (bool properties), 
  * an animatable weight, and the blending mode flag to indicate how the data on this layer is interacting
  * with the data of the other layers during the evaluation. 
  * \nosubgrouping
  */
class KFBX_DLL KFbxAnimLayer : public KFbxCollection
{
    KFBXOBJECT_DECLARE(KFbxAnimLayer, KFbxCollection);

public:
    //////////////////////////////////////////////////////////////////////////
    //
    // Properties
    //
    //////////////////////////////////////////////////////////////////////////

    /** This property stores the weight factor.
      * The weight factor is the percentage of influence this layer has during
      * the evaluation.
      *
      * Default value is \c 100.0
      */
    KFbxTypedProperty<fbxDouble1>       Weight;

    /** This property stores the mute state.
      * The mute state indicates that this layer should be excluded from the evaluation.
      *
      * Default value is \c false
      */
    KFbxTypedProperty<fbxBool1>         Mute;

    /** This property stores the solo state.
      * The solo state indicates that this layer is the only one that should be
      * processed during the evaluation.
      *
      * Default value is \c false
      */
    KFbxTypedProperty<fbxBool1>         Solo;

    /** This property stores the lock state.
      * The lock state indicates that this layer has been "locked" from editing operations
      * and should no longer receive keyframes.
      *
      * Default value is \c false
      */
    KFbxTypedProperty<fbxBool1>         Lock;

    /** This property stores the display color.
      * This color can be used by applications if they display a graphical represention
      * of the layer. The FBX SDK does not use it but guarantees that the value is saved to the FBX
      * file and retrieved from it.
      *
      * Default value is \c (0.8, 0.8, 0.8)
      */
    KFbxTypedProperty<fbxDouble3>       Color;

    /** This property stores the blend mode.
      * The blend mode is used to specify how this layer influences the animation evaluation. See the
      * EBlendMode enumeration for the description of the modes.
      *
      * Default value is \c eModeAdditive
      */
    KFbxTypedProperty<fbxEnum>			BlendMode;

    /** This property stores the rotation accumulation mode.
      * This option indicate how the rotation curves on this layer combine with any preceding layers 
      * that share the same attributes. See the ERotationAccumulationMode enumeration for the description
      * of the modes.
      *
      * Default value is \c eRotAccuModeByLayer
      */
    KFbxTypedProperty<fbxEnum>			RotationAccumulationMode;

    /** This property stores the scale accumulation mode.
      * This option indicate how the scale curves on this layer combine with any preceding layers 
      * that share the same attributes. See the EScaleAccumulationMode enumeration for the description
      * of the modes.
      *
      * Default value is \c eScaleAccuModeMultiply
      */
    KFbxTypedProperty<fbxEnum>			ScaleAccumulationMode;

	//! Reset this object properties to their default value.
	void Reset();

    /**
      * \name BlendMode bypass functions.
      * This section provides methods to bypass the current layer blend mode by data type.
      * When the state is \c true, the evaluators that are processing the layer will 
      * need to consider that, for the given data type, the blend mode is forced to be Overwrite. 
      * If the state is left to its default \c false value, then the layer blend mode applies.
      * \remarks This section only supports the basic types defined in the kfbxtypes.h header file.
      */
    //@{

		/** Set the bypass flag for the given data type.
		  * \param pType The fbxType identifier.
		  * \param pState The new state of the bypass flag.
		  * \remarks If pType is eMAX_TYPES, then pState is applied to all the data types.
		  */
		void SetBlendModeBypass(EFbxType pType, bool pState);

		/** Get the current state of the bypass flag for the given data type.
		  * \param pType The fbxType identifier.
		  * \return The current state of the flag for a valid pType value and \c false in any other case.
		  */
		bool GetBlendModeBypass(EFbxType pType);

    //@}


    /** \enum EBlendMode Blend mode between animation layers.
      */
	typedef enum
	{
		eBlendModeAdditive,				/*!<The layer "adds" its animation to layers that precede it in the 
										   stack and affect the same attributes. */
		eBlendModeOverride,				/*!<The layer "overrides" the animation of any layer that shares 
										   the same attributes and precedes it in the stack. */
		eBlendModeOverridePassthrough	/*!<This mode is like the eBlendModeOverride but the Weight value 
											influence how much animation from the preceding layers is 
											allowed to passthrough. When using this mode with a Weight of 
											100.0, this layer is completely opaque and it masks any animation
											from the preceding layers for the same attribute. If the Weight
											is 50.0, half of this layer animation is mixed with half of the
											animation of the preceding layers for the same attribute. */
	} EBlendMode;
    
    /** \enum ERotationAccumulationMode Rotation accumulation mode of animation layer.
      */
	typedef enum
	{
		eRotAccuModeByLayer,		/*!< Rotation values are weighted per layer and the result 
									     rotation curves are calculated using concatenated quaternion values. */
		eRotAccuModeByChannel		/*!< Rotation values are weighted per component and the result 
									     rotation curves are calculated by adding each independent Euler XYZ value. */
	} ERotationAccumulationMode;

    /** \enum EScaleAccumulationMode Scale accumulation mode of animation layer.
      */
	typedef enum
	{
		eScaleAccuModeMultiply,	/*!< Independent XYZ scale values per layer are calculated using 
									 the layer weight value as an exponent, and result scale curves 
									 are calculated by multiplying each independent XYZ scale value. */
		eScaleAccuModeAdditive /*!< Result scale curves are calculated by adding each independent XYZ value. */
	} EScaleAccumulationMode;

    /**
      * \name CurveNode Management
      */
    //@{
        /** Create a KFbxAnimCurveNode based on the property data type.
          * \param pProperty The property that the created KFbxAnimCurveNode will be connected to.
          * \return Pointer to the created KFbxAnimCurveNode, or NULL if an error occurred.
          * \remarks This function will fail if the property eANIMATABLE flag it is not set.
          * \remarks This function sets the ePUBLISHED flag of the property.
          * \remarks The newly created KFbxAnimCurveNode is automatically connected to both
          *         this object and the property.
          */
        KFbxAnimCurveNode* CreateCurveNode(KFbxProperty& pProperty);
    //@}

#ifndef DOXYGEN_SHOULD_SKIP_THIS
	///////////////////////////////////////////////////////////////////////////////
	//  WARNING!
	//	Anything beyond these lines may not be documented accurately and is 
	// 	subject to change without notice.
	///////////////////////////////////////////////////////////////////////////////

protected:
    KFbxAnimLayer(KFbxSdkManager& pManager, char const* pName, KError* pError=0);

	virtual KFbxAnimLayer* GetAnimLayer();

private:
    bool ConstructProperties(bool pForceSet);

    KFbxTypedProperty<fbxULongLong1>    mBlendModeBypass;	

    mutable KError* mError;

	friend class KFbxObject;
#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS
};

typedef KFbxAnimLayer* HKKFbxAnimLayer;

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXANIMLAYER_H

