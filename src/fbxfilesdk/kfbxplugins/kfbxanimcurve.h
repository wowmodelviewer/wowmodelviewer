/*!  \file kfbxanimcurve.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXANIMCURVE_H
#define FBXFILESDK_KFBXPLUGINS_KFBXANIMCURVE_H

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

#include <fbxfilesdk/components/kfcurve/kfcurve.h>
#include <fbxfilesdk/kfbxplugins/kfbxanimcurvebase.h>
#include <fbxfilesdk/kfbxplugins/kfbxobject.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

/** Definitions used for the FBX animation curves and keys.
  */

class KFBX_DLL KFbxAnimCurveDef {

public:
	static const int   kWEIGHT_DIVIDER = 9999; // precise enough and can be divided by 3 without error
	static const float kDEFAULT_WEIGHT;
	static const float kMIN_WEIGHT;
	static const float kMAX_WEIGHT;
	static const float kDEFAULT_VELOCITY;

	//! Key tangent mode for cubic interpolation.
	typedef enum {
		//! Auto key (spline cardinal).
		eTANGENT_AUTO                     = KFCURVE_TANGEANT_AUTO,
		//! Spline TCB (Tension, Continuity, Bias)
		eTANGENT_TCB                      = KFCURVE_TANGEANT_TCB,
		//! Next slope at the left equal to slope at the right.
		eTANGENT_USER                     = KFCURVE_TANGEANT_USER,
		//! Independent left and right slopes, with next slope at the left equal to slope at the right.
		eTANGENT_BREAK                    = KFCURVE_TANGEANT_BREAK,
		//! Independent left and right slopes, with auto key.
		eTANGENT_AUTO_BREAK               = KFCURVE_TANGEANT_AUTO_BREAK,
		//! Clamp: key should be flat if next or previous key has the same value (overrides tangent mode).
		eTANGENT_GENERIC_CLAMP            = KFCURVE_GENERIC_CLAMP,
		//! Time independent tangent (overrides tangent mode).
		eTANGENT_GENERIC_TIME_INDEPENDENT = KFCURVE_GENERIC_TIME_INDEPENDENT

	} ETangentMode;

	//! Key interpolation type.
	typedef enum {
		//! Constant value until next key.
		eINTERPOLATION_CONSTANT = KFCURVE_INTERPOLATION_CONSTANT,
		//! Linear progression to next key.
		eINTERPOLATION_LINEAR   = KFCURVE_INTERPOLATION_LINEAR,
		//! Cubic progression to next key.
		eINTERPOLATION_CUBIC    = KFCURVE_INTERPOLATION_CUBIC,
		//! Any interpolation (Constant, Linear, Cubic)
		eINTERPOLATION_ALL      = KFCURVE_INTERPOLATION_ALL
	} EInterpolationType;

    //! Weighted mode.
    typedef enum {
		//! Tangent has default weights of 0.333; we define this state as not weighted.
        eWEIGHTED_NONE      = KFCURVE_WEIGHTED_NONE,
		//! Right tangent is weighted.
        eWEIGHTED_RIGHT     = KFCURVE_WEIGHTED_RIGHT,
		//! Left tangent is weighted.
        eWEIGHTED_NEXT_LEFT = KFCURVE_WEIGHTED_NEXT_LEFT,
		//! Both left and right tangents are weighted.
        eWEIGHTED_ALL       = KFCURVE_WEIGHTED_ALL
    } EWeightedMode;

    //! Key constant mode.
    typedef enum {
		//! Curve value is constant between this key and the next
        eCONSTANT_STANDARD  = KFCURVE_CONSTANT_STANDARD,
		//! Curve value is constant, with next key's value
        eCONSTANT_NEXT      = KFCURVE_CONSTANT_NEXT,
		//! Any constant mode (standard or next)
        eCONSTANT_ALL       = KFCURVE_CONSTANT_ALL
    } EConstantMode;

    //! Velocity mode. Velocity settings speed up or slow down animation on either side of a key without changing the trajectory of the animation. Unlike Auto and Weight settings, Velocity changes the animation in time, but not in space.
	typedef enum {
		//! No velocity (default).
		eVELOCITY_NONE	=		KFCURVE_VELOCITY_NONE,
		//! Right tangent has velocity.
		eVELOCITY_RIGHT	=		KFCURVE_VELOCITY_RIGHT,
		//! Left tangent has velocity.
		eVELOCITY_NEXT_LEFT =	KFCURVE_VELOCITY_NEXT_LEFT,
		//! Both left and right tangents have velocity.
		eVELOCITY_ALL		=	KFCURVE_VELOCITY_ALL
	} EVelocityMode;

/** Tangent visibility.
*/
	typedef enum {
		//! No tangent is visible.
		eTANGENT_SHOW_NONE		= KFCURVE_TANGEANT_SHOW_NONE,
		//! Left tangent is visible.
		eTANGENT_SHOW_LEFT		= KFCURVE_TANGEANT_SHOW_LEFT,
		//! Right tangent is visible.
		eTANGENT_SHOW_RIGHT		= KFCURVE_TANGEANT_SHOW_RIGHT,
		//! Both left and right tangents are visible.
		eTANGENT_SHOW_BOTH		= KFCURVE_TANGEANT_SHOW_BOTH
	} ETangentVisibility;

	//! KFbxAnimCurveKey data indices for cubic interpolation tangent information.
	typedef enum {
		//! Index of the right derivative, User and Break tangent mode (data are float).
		eRIGHT_SLOPE			= KFCURVEKEY_RIGHT_SLOPE, 
		//! Index of the left derivative for the next key, User and Break tangent mode.
		//!
		eNEXT_LEFT_SLOPE		= KFCURVEKEY_NEXT_LEFT_SLOPE, 

		//! Start index of weight values, User and Break tangent break mode (data are kInt16 tokens from weight and converted to float).
		eWEIGHTS				= KFCURVEKEY_WEIGHTS, 
		//! Index of weight on right tangent, User and Break tangent break mode.
		eRIGHT_WEIGHT			= KFCURVEKEY_RIGHT_WEIGHT, 
		//! Index of weight on next key's left tangent, User and Break tangent break mode.
		eNEXT_LEFT_WEIGHT		= KFCURVEKEY_NEXT_LEFT_WEIGHT, 

		//! Start index of velocity values, Velocity mode
		eVELOCITY				= KFCURVEKEY_VELOCITY,
		//! Index of velocity on right tangent, Velocity mode
		eRIGHT_VELOCITY			= KFCURVEKEY_RIGHT_VELOCITY,
		//! Index of velocity on next key's left tangent, Velocity mode
		eNEXT_LEFT_VELOCITY		= KFCURVEKEY_NEXT_LEFT_VELOCITY, 

		//! Index of Tension, TCB tangent mode (data are floats).
		eTCB_TENSION			= KFCURVEKEY_TCB_TENSION, 
		//! Index of Continuity, TCB tangent mode.
		eTCB_CONTINUITY			= KFCURVEKEY_TCB_CONTINUITY, 
		//! Index of Bias, TCB tangent mode.
		eTCB_BIAS				= KFCURVEKEY_TCB_BIAS,

	} EKFbxAnimCurveDataIndex;
};

/** Interface for animation key objects.
  * \nosubgrouping
  *
  * Users always use KFbxAnimCurveKey. A KFbxAnimCurveKey points to a KFbxAnimCurveKeyImpl.
  * See KFbxAnimCurveKFCurveKey for an example of implemented class.
  */
class KFBX_DLL KFbxAnimCurveKeyImpl
{
public:
    /** Destructor.
      */
    virtual ~KFbxAnimCurveKeyImpl() {};

	/** Assignment operator.
	  */
    virtual KFbxAnimCurveKeyImpl& operator=(const KFbxAnimCurveKeyImpl& pFKey) = 0;

    /** Set time and value of key.
      * \param pTime New time of this key.
      * \param pValue New value of this key.
      */
	virtual void Set(KTime pTime, float pValue) = 0;

    /**	Set a key with cubic interpolation, TCB tangent mode. 	
	* The key is modified according to the other parameters. 
	* The TCB mode controls the tension, continuity,
	* and bias of the curve.
    *	\param pTime	Key time.
    *	\param pValue	Key value.
    *	\param pData0	Tension. Controls the amount of curvature in the animation curve. The higher the tension is, the more linear
	* the curve looks. When the tension is low, the curve looks rounder or wider. 
    *	\param pData1	Continuity. Controls the smoothness or singularity of the curve on the key. 
    *	\param pData2	Bias. Controls if the effect of tension and continuity affect the curve before or after the key.
    */
	virtual void SetTCB(KTime pTime, float pValue, float pData0 = 0.0f, float pData1 = 0.0f, float pData2 = 0.0f) = 0;

    /** Get key value.
    *	\return Key value.
    */
    virtual float GetValue() const = 0;

    /** Set key value.
    * \param pValue The value to set.
    */ 
	virtual void SetValue(float pValue) = 0;

    /** Get key's interpolation type.
    *   \return                  Interpolation type of the queried key.
    */
    virtual KFbxAnimCurveDef::EInterpolationType GetInterpolation() const = 0;

    /** Set key's interpolation type.
    *	\param pInterpolation Key interpolation type.
    */
	virtual void SetInterpolation (KFbxAnimCurveDef::EInterpolationType pInterpolation) = 0;

	/** Get key's tangent mode.
    *   \param pIncludeOverrides Include override flags: Break, Clamp, Time-Independant.
    *	This method is meaningful for cubic interpolation only.
    *			 Using this method for non cubic interpolated key will return unpredictable value.
    *	\return Key tangent mode.
    */
    virtual KFbxAnimCurveDef::ETangentMode GetTangentMode(bool pIncludeOverrides = false) const = 0;

	/** Set tangent mode.
	  * \param pTangentMode Tangent mode to set.
	  */
	virtual void SetTangentMode (KFbxAnimCurveDef::ETangentMode pTangentMode) = 0;

	/** Get key's tangent weight mode.
	*	This method is meaningful for cubic interpolation only.
	*/
	virtual KFbxAnimCurveDef::EWeightedMode GetTangentWeightMode() const = 0;

	/** Set key's tangent weight mode as double value (cubic interpolation, non TCB tangent mode).
    *	This method is meaningful for cubic interpolation only.
    *	\param pTangentWeightMode	Weight mode.
	*	\param pMask				Used to select the affected tangents.
	*/
	virtual void SetTangentWeightMode(KFbxAnimCurveDef::EWeightedMode pTangentWeightMode, KFbxAnimCurveDef::EWeightedMode pMask = KFbxAnimCurveDef::eWEIGHTED_ALL ) = 0;

	/** Get key's tangent velocity mode.
    *	This method is meaningful for cubic interpolation only.
	*/
	virtual KFbxAnimCurveDef::EVelocityMode GetTangentVelocityMode() const = 0;

	/** Set key's tangent velocity mode as double value (cubic interpolation, non TCB tangent mode).
    *	This method is meaningful for cubic interpolation only.
    *	\param pTangentVelocityMode	Velocity mode. 
	*	\param pMask				Used to select the affected tangents
	*/
	virtual void SetTangentVelocityMode(KFbxAnimCurveDef::EVelocityMode pTangentVelocityMode, KFbxAnimCurveDef::EVelocityMode pMask = KFbxAnimCurveDef::eVELOCITY_ALL ) = 0;

	/** Get key constant mode.
	*	Warning: This method is meaningful for constant interpolation only.
	*			 Using this method for non constant interpolated key will return unpredicted value.
	*	\return Key constant mode.
	*/
	virtual KFbxAnimCurveDef::EConstantMode GetConstantMode() const = 0;

	/** Set key's constant mode.
    *	This method is meaningful for constant interpolation only.
	  * \param pMode Constant mode to set.
	  */
	virtual void SetConstantMode(KFbxAnimCurveDef::EConstantMode pMode) = 0;

	/** Get key data.
	  * \param pIndex Data index, dependent on the key tangent mode.
	  * \return Data value.
	  */
	virtual float GetDataFloat(KFbxAnimCurveDef::EKFbxAnimCurveDataIndex pIndex) const = 0;

	/** Set key data.
	  * \param pIndex Data index, dependent on the key tangent mode.
	  * \param pValue Data value to set.
	  */
	virtual void SetDataFloat(KFbxAnimCurveDef::EKFbxAnimCurveDataIndex pIndex, float pValue) = 0;

	/** Set tangent visibility mode. This would indicate what part of the tangent is visible in a graphical interface.
    *	This method is relevant for cubic interpolation only.
    *	\param pVisibility	Tangent visibility mode.
	*/
	virtual void	SetTangentVisibility (KFbxAnimCurveDef::ETangentVisibility pVisibility) = 0;	

	/** Return tangent visibility mode.
    *	This method is relevant for cubic interpolation only.
    *	\return Tangent visibility mode.
	*/
	virtual KFbxAnimCurveDef::ETangentVisibility GetTangentVisibility () const = 0;

	/** Set or unset the tangent break. When this flag is set (KFbxAnimCurveDef::eTANGEAT_BREAK), the key's left and right slopes are independent.
	* When this flag is off, the key's left and right slope are equal.
    * This method is relevant for User (KFbxAnimCurveDef::eTANGENT_USER) and Auto (KFbxAnimCurveDef::eTANGENT_AUTO) tangent modes only.
    * \param pVal Break flag (\c true or \c false).
	*/
	virtual void SetBreak(bool pVal) = 0; 

	/** Get if the tangent has a break. When this flag is set (KFbxAnimCurveDef::eTANGEAT_BREAK), the key's left and right slopes are independent.
	* When this flag is off, the key's left and right slope are equal.
    * This method is relevant for User (KFbxAnimCurveDef::eTANGENT_USER) and Auto (KFbxAnimCurveDef::eTANGENT_AUTO) tangent modes only.
	* \return Break flag (\c true or \c false).
	*/
	virtual bool GetBreak() const = 0; 
};

/** This is the base class interface for the FBX animation curve keys.
A key is defined by a time and a value. It also has tangents that control how the animation curve enters and exits the key.

  * \nosubgrouping
  * This class, for the time being, is implemented by the legacy KFCurveKey
  * structure. However, this class is now the
  * main animation key object of the SDK, and its implementation
  * is subject to change.
  *
  * Default constructor does not initialize data 
  * members. If an instance has to be initialized, use function KFbxAnimCurveKey::Set().
  */
class KFBX_DLL KFbxAnimCurveKey : public KFbxAnimCurveKeyBase
{

public:

	/** Constructor with no argument
	  */
	KFbxAnimCurveKey() : KFbxAnimCurveKeyBase()
    {
		K_ASSERT(mAllocatorFct != NULL);
		mImpl = (*mAllocatorFct)();
    }

	/** Constructor with time.
      * \param pTime The time of key.
      */
    KFbxAnimCurveKey(KTime pTime) : KFbxAnimCurveKeyBase()
    {
		K_ASSERT(mAllocatorFct != NULL);
		mImpl = (*mAllocatorFct)();
        SetTime(pTime);
    }

    /** Constructor with time and value.
      * \param pTime The time of key.
      * \param pVal The value of key.
      */
    KFbxAnimCurveKey(KTime pTime, float pVal) : KFbxAnimCurveKeyBase()
    {
		K_ASSERT(mAllocatorFct != NULL);
		mImpl = (*mAllocatorFct)();
        Set(pTime, pVal);
    }

	/** Copy constructor
	  */
	KFbxAnimCurveKey(KFbxAnimCurveKey const& pFKey) : KFbxAnimCurveKeyBase()
    {
		K_ASSERT(mCopyAllocatorFct != NULL);
		SetTime(pFKey.GetTime());
		mImpl = mCopyAllocatorFct(pFKey.GetImpl());
    }

	/** Destructor
	  */
	~KFbxAnimCurveKey()
    {
		K_ASSERT(mDeallocatorFct != NULL);
		(*mDeallocatorFct)(mImpl);
    }

    /** Assignment operator
      */
    KFbxAnimCurveKey& operator=(const KFbxAnimCurveKey& pFKey)
	{
		K_ASSERT(mImpl);
		if (mImpl)
		{
			*mImpl = *(pFKey.GetImpl());
		}
		SetTime(pFKey.GetTime());
		return *this;
	}

    /** Get time value.
    * \return Time value.
    */
    KTime GetTime() const
	{
		return KFbxAnimCurveKeyBase::GetTime();
	}

    /** Set time value.
    * \param pTime Time value to set.
    */
    void SetTime(const KTime& pTime)
	{
		KFbxAnimCurveKeyBase::SetTime(pTime);
	}

    /** Set time and value of key.
      * \param pTime New time of this key.
      * \param pValue New value of this key.
      */
	void Set(KTime pTime, float pValue)
	{
		KFbxAnimCurveKeyBase::SetTime(pTime);
		mImpl->Set(pTime, pValue);
	}

    /**	Set a key with cubic interpolation, TCB tangent mode. 	
	* The key is modified according to the other parameters. 
	* The TCB mode controls the tension, continuity,
	* and bias of the curve.
    *	\param pTime	Key time.
    *	\param pValue	Key value.
    *	\param pData0	Tension. Controls the amount of curvature in the animation curve. The higher the tension is, the more linear
	* the curve looks. When the tension is low, the curve looks rounder or wider. 
    *	\param pData1	Continuity. Controls the smoothness or singularity of the curve on the key. 
    *	\param pData2	Bias. Controls if the effect of tension and continuity affect the curve before or after the key.
    */
	void SetTCB(KTime pTime, float pValue, float pData0 = 0.0f, float pData1 = 0.0f, float pData2 = 0.0f)
	{
		KFbxAnimCurveKeyBase::SetTime(pTime);
		mImpl->SetTCB(pTime, pValue, pData0, pData1, pData2);
	}

    /** Get key value.
	* \return Key value.
    */
    float GetValue() const
	{
		return mImpl->GetValue();
	}

    /** Set key value.
    * \param pValue The value to set.
    */ 
	void SetValue(float pValue)
	{
		mImpl->SetValue(pValue);
	}

	/** Get key's interpolation type.
    *   \return                  Interpolation type of the queried key.
    */
    KFbxAnimCurveDef::EInterpolationType GetInterpolation()
	{
		return mImpl->GetInterpolation();
	}

    /** Set key's interpolation type.
    *	\param pInterpolation Key interpolation type.
    */
	void SetInterpolation (KFbxAnimCurveDef::EInterpolationType pInterpolation)
	{
		mImpl->SetInterpolation(pInterpolation);
	}

	/** Get key's tangent mode.
    *   \param pIncludeOverrides Include override flags: Break, Clamp, Time-Independant.
    *	This method is meaningful for cubic interpolation only.
    *			 Using this method for non cubic interpolated key will return unpredictable value.
    *	\return Key tangent mode.
    */
    KFbxAnimCurveDef::ETangentMode GetTangentMode(bool pIncludeOverrides = false)
	{
		return mImpl->GetTangentMode(pIncludeOverrides);
	}

	/** Set tangent mode.
	  * \param pTangentMode Tangent mode to set.
	  */
	void SetTangentMode (KFbxAnimCurveDef::ETangentMode pTangentMode)
	{
		mImpl->SetTangentMode(pTangentMode);
	}

	/** Get key's tangent weight mode.
	*	This method is meaningful for cubic interpolation only.
	*/
	KFbxAnimCurveDef::EWeightedMode GetTangentWeightMode() const
	{
		return mImpl->GetTangentWeightMode();
	}

	/** Set key's tangent weight mode as double value (cubic interpolation, non TCB tangent mode).
    *	This method is meaningful for cubic interpolation only.
    *	\param pTangentWeightMode	Weight mode.
	*	\param pMask				Used to select the affected tangents.
	*/
	void SetTangentWeightMode(KFbxAnimCurveDef::EWeightedMode pTangentWeightMode, KFbxAnimCurveDef::EWeightedMode pMask = KFbxAnimCurveDef::eWEIGHTED_ALL )
	{
		mImpl->SetTangentWeightMode(pTangentWeightMode, pMask);
	}


	/** Get key's tangent velocity mode.
    *	This method is meaningful for cubic interpolation only.
	*/
	KFbxAnimCurveDef::EVelocityMode GetTangentVelocityMode() const
	{
		return mImpl->GetTangentVelocityMode();
	}

	/** Set key's tangent velocity mode as double value (cubic interpolation, non TCB tangent mode).
    *	This method is meaningful for cubic interpolation only.
    *	\param pTangentVelocityMode	Velocity mode. 
	*	\param pMask				Used to select the affected tangents
	*/
	void SetTangentVelocityMode(KFbxAnimCurveDef::EVelocityMode pTangentVelocityMode, KFbxAnimCurveDef::EVelocityMode pMask = KFbxAnimCurveDef::eVELOCITY_ALL )
	{
		mImpl->SetTangentVelocityMode(pTangentVelocityMode, pMask);
	}

	/** Get key's constant mode.
    *	\note This method is only relevant if the key's interpolation type is constant (eINTERPOLATION_CONSTANT).
    *	Using this method on a key with an other interpolation type will return unpredictable value.
	*	\return Key constant mode.
	*/
	KFbxAnimCurveDef::EConstantMode GetConstantMode() const
	{
		return mImpl->GetConstantMode();
	}

	/** Set constant mode.
	  * \param pMode Constant mode to set.
	  */
	void SetConstantMode(KFbxAnimCurveDef::EConstantMode pMode)
	{
		mImpl->SetConstantMode(pMode);
	}

	/** Get key data.
	  * \param pIndex Data index, dependent on the key tangent mode.
	  * \return Data value.
	  */
	float GetDataFloat(KFbxAnimCurveDef::EKFbxAnimCurveDataIndex pIndex) const
	{
		return mImpl->GetDataFloat(pIndex);
	}

	/** Set key data.
	  * \param pIndex Data index, dependent on the key tangent mode.
	  * \param pValue Data value to set.
	  */
	void SetDataFloat(KFbxAnimCurveDef::EKFbxAnimCurveDataIndex pIndex, float pValue)
	{
		mImpl->SetDataFloat(pIndex, pValue);
	}

	/** Set tangent visibility mode. This would indicate what part of the tangent is visible in a graphical interface.
    *	This method is relevant for cubic interpolation only.
    *	\param pVisibility	Tangent visibility mode.
	*/
	void	SetTangentVisibility (KFbxAnimCurveDef::ETangentVisibility pVisibility)
	{
		mImpl->SetTangentVisibility(pVisibility);
	}

	/** Return tangent visibility mode.
    *	This method is relevant for cubic interpolation only.
    *	\return Tangent visibility mode.
	*/
	KFbxAnimCurveDef::ETangentVisibility GetTangentVisibility () const
	{
		return mImpl->GetTangentVisibility();
	}

	/** Set or unset the tangent break. When this flag is set (KFbxAnimCurveDef::eTANGENT_BREAK), the key's left and right slopes are independent.
	* When this flag is off, the key's left and right slope are equal.
    * This method is relevant for User (KFbxAnimCurveDef::eTANGENT_USER) and Auto (KFbxAnimCurveDef::eTANGENT_AUTO) tangent modes only.
    * \param pVal Break flag (\c true or \c false).
	*/
	void SetBreak(bool pVal)
	{
		mImpl->SetBreak(pVal);
	}

	/** Get if tangent is break
	* Only valid for User and Auto keys
	*/
	bool GetBreak() const
	{
		return mImpl->GetBreak();
	}

	/** Get key implementation.
      * \return Pointer to implemented instance, e.g. a KFbxAnimCurveKFCurveKey.
      */
	KFbxAnimCurveKeyImpl* GetImpl() const
	{
		return mImpl;
	}

	/** Set allocator function
      * \param pAllocatorFct Allocator function
      */
	static void SetAllocatorFct(KFbxAnimCurveKeyImpl* (*pAllocatorFct)());

	/** Set copy allocator function
      * \param pCopyAllocatorFct Copy allocator function
      */
	static void SetCopyAllocatorFct(KFbxAnimCurveKeyImpl* (*pCopyAllocatorFct)(KFbxAnimCurveKeyImpl*));

	/** Set deallocator function
      * \param pDeallocatorFct Deallocator function
      */
	static void SetDeallocatorFct(void (*pDeallocatorFct)(KFbxAnimCurveKeyImpl*));

private:
	static KFbxAnimCurveKeyImpl* (*mAllocatorFct)();
	static KFbxAnimCurveKeyImpl* (*mCopyAllocatorFct)(KFbxAnimCurveKeyImpl*);
	static void (*mDeallocatorFct)(KFbxAnimCurveKeyImpl*);
	KFbxAnimCurveKeyImpl* mImpl;
};


class KFbxScene;
/** An animation curve, defined by a collection of keys (KFbxAnimCurveKey), and indicating how a value changes over time.
* Since an animation curve is a function, on a given animation curve, only one key per time is
* allowed. The keys are sorted
* in time order. They can be accessed by their index on the curve, from 0 to KFbxAnimCurve::KeyGetCount-1.
* The time unit in FBX (KTime) is 1/46186158000 of one second. 
*
* Each key defines tangents and interpolation that modify the animation curve.
* Tangents control the way the animation curve enters and exits the keys.
* Interpolation indicates the animation curve's behaviour between keys.
*
* Interpolation modes are
* \li Constant - Curve value stays the same until next key
* \li Linear - Animation curve is a straight line
* \li Cubic - Animation curve is a Bezier spline
*
* Tangent modes are
* \li Auto (Spline cardinal)
* \li Spline TCB (Tension, Continuity, Bias)
* \li User (Next slope at the left equal to slope at the right)
*
* Tangent modes can be overriden by more tangent options:
* \li Break (Independent left and right slopes)
* \li Clamp (Key should be flat if next or previous key has the same value)
* \li Time independant
*
* Tangent can be modified some more by adding weights and velocity.
* By default, the weights are 0.333 on either side of the key, and there is 
* no velocity. Velocity settings speed up or slow down animation on either side of 
* a key without changing the trajectory of the animation. Unlike Auto and Weight settings, 
* Velocity changes the animation in time, but not in space.
* 
* @note When adding keys to an animation curve, use KFbxAnimCurve::KeyModifyBegin and KFbxAnimCurve::KeyModifyEnd.
*
* \nosubgrouping
* This class, for the time being, encapsulates the legacy KFCurve
* structure. However, KFbxAnimCurve is now the
* main animation animation curve object of the SDK, and its implementation
* is subject to change.
*/
class KFBX_DLL KFbxAnimCurve : public KFbxAnimCurveBase
{
    KFBXOBJECT_DECLARE_ABSTRACT(KFbxAnimCurve, KFbxAnimCurveBase);

public:

	/**
	  * \name Animation curve creation.
	  *
	  */
	//@{
    /** Create a KFbxAnimCurve.
	  * \param pContainer Scene to which the created animation curve belongs.
	  * \param pName Name of the animation curve.
	  * \return Newly created animation curve
	  */
    static KFbxAnimCurve* Create(KFbxScene* pContainer, char const *pName);
	//@}

    /**
      * \name Key management.
      *
      */
    //@{
    /** Resize animation curve buffer to hold a certain number of keys.
    * \param pKeyCount Number of keys the animation curve will eventually hold.
      */
    virtual void ResizeKeyBuffer(int pKeyCount) = 0;

    /** Call this function prior to adding, removing or editing keys of an animation curve.
      * Call function KFbxAnimCurve::KeyModifyEnd() after modification.
      */
    virtual void KeyModifyBegin () = 0;
        
    /** Call this function after adding, removing or editing keys of an animation curve.
      * Function KFbxAnimCurve::KeyModifyBegin() must have been called prior to modifying the keys.
      */
    virtual void KeyModifyEnd () = 0;

    //! Remove all the keys of the animation curve and free buffer memory.
    virtual void KeyClear () = 0;

    /** Get the number of keys.
      * \return Key count.
      */
    virtual int KeyGetCount () const = 0;

    /** Add a given key at given time. The new key is appended after all the other animation curve's keys.
    *	Function KFbxAnimCurve::KeyInsert() should be used instead if the key 
    * is to be added in the curve and not at the end. This function does not
    * respect the interpolation type and tangents of the neighboring keys. 
    * If there is already a key at the given time, the key is modified and no 
    * new key is added.
    * \param pTime Time of the new key.
    * \param pKey Key to add.
    * \param pLast Key index to speed up search. If this 
    * function is called in a loop, initialize this value to 0 and let it 
    * be updated by each call.
    * \return Index of the key at given time, no matter if it was added 
    * or already present.
    * Key value, interpolation type and tangent mode must be set 
    * explicitly afterwards.
    */
    virtual int KeyAdd (KTime pTime, KFbxAnimCurveKeyBase& pKey, int* pLast = NULL) = 0; 

    /** Add a key at given time. The new key is appended after all the other animation curve's keys.
    *	Function KFbxAnimCurve::KeyInsert() should be used instead if the key 
    * is to be added in the curve and not at the end. This function does not
    * respect of the interpolation type and tangents of the neighboring keys. 
    * If there is already a key a the given time, no key is added.
    *	\param pTime Time of the new key.
    * \param pLast Key index to speed up search. If this 
    * function is called in a loop, initialize this value to 0 and let it 
    * be updated by each call.
    *	\return Index of the key at given time, no matter if it was added 
    * or already present.
    * Key value, interpolation type and tangent mode must be set 
    * explicitly afterwards.
    */
    virtual int KeyAdd (KTime pTime, int* pLast = NULL) = 0;

    /** Set (or replace) key at given index with given key. 
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    * \param pIndex Index of the key to be set or replaced.
    * \param pKey New key at this index.
    * \return \c true if key time is superior to previous key time
    * and inferior to next key time, \c false otherwise.
    */
    virtual bool KeySet(int pIndex,  KFbxAnimCurveKeyBase& pKey) = 0;
        
    /** Remove key at given index. Other key indices are updated automatically.
    *	\param pIndex Index of key to remove.
    *	\return \c true on success, \c false otherwise.
    */
    virtual bool KeyRemove(int pIndex) = 0;

    /** Insert a key at given time.
    *	This function should be used instead of KFbxAnimCurve::KeyAdd() if the key 
    * is to be added in the curve and not at the end. It inserts the key in 
    * respect to the interpolation type and tangents of the neighboring keys. 
    * If there is already a key a the given time, the key is modified and no 
    * new key is added.
    * \param pTime Time of the new key.
    * \param pLast Key index to speed up search. If this 
    * function is called in a loop, initialize this value to 0 and let it 
    * be updated by each call.
    * \return Index of the key at given time, no matter if it was inserted 
    * or already present.
    * Key value must be set explicitly afterwards. The 
    * interpolation type and tangent mode are copied from the previous key.
    */
    virtual int KeyInsert ( KTime pTime, int* pLast = NULL ) = 0;
        
    /** Find key index for a given time.
    * \param pTime Time of the key looked for.
    * \param pLast Key index to speed up search. If this 
    * function is called in a loop, initialize this value to 0 and let it 
    * be updated by each call.
    * \return Key index. The integer part of the key index gives the 
    * index of the closest key with a smaller time. The decimals give
    * the relative position of given time compared to previous and next
    * key times. Returns -1 if animation curve has no key.
	*
	* For example (using seconds for clarity), if there is a key at time 10s with index 5, and a key at
	* time 11s with index 6, KeyFind(10.3s) would return 5.3.
    */
	virtual double KeyFind (KTime pTime, int* pLast = NULL) = 0;

    /** Scale value of all keys.
    *	\param pMultValue Scale applied on key values.
    *	\return \c true on success, \c false otherwise.
    */
	virtual bool KeyScaleValue (float pMultValue) = 0;

    /** Scale value and tangent of all keys.
    *	\param pMultValue Scale applied on key values and tangents.
    *	\return \c true on success, \c false otherwise.
    */
	virtual bool KeyScaleValueAndTangent (float pMultValue) = 0;
    //@}

    /**
    * \name Key Manipulation.
    */

    //@{
    /** General function to set key properties.
	* The key at index pKeyIndex is retrieved and modified according to the other parameters. 
	* The key must have been previously created, for example using KeyAdd.
    *	Use KFbxAnimCurve::SetTCB() in the specific case of setting a key with cubic interpolation and TCB tangent mode.
    *   \param pKeyIndex        Index of the key.
    *	\param pTime			Key time.
    *	\param pValue			Key value.
    *	\param pInterpolation	Key interpolation type.
    *	\param pTangentMode		Key tangent mode (meaningful for cubic interpolation only).
    *	\param pData0			Value of right slope.
    *	\param pData1			Value of next left slope.
    *	\param pTangentWeightMode	Weight mode, if used.
    *	\param pWeight0				Weight for right slope, if tangent weight mode is eWEIGHTED_RIGHT or eWEIGHTED_ALL.
    *	\param pWeight1				Weight for next left slope, if tangent weight mode is eWEIGHTED_NEXT_LEFT or eWEIGHTED_ALL.
    *	\param pVelocity0			Velocity for right slope, if tangent velocity mode is eVELOCITY_RIGHT or eVELOCITY_ALL.
    *	\param pVelocity1			Velocity for next left slope, if tangent velocity mode is eVELOCITY_NEXT_LEFT or eVELOCITY_ALL.
    */
	virtual void KeySet(int pKeyIndex,KTime pTime, float pValue, KFbxAnimCurveDef::EInterpolationType pInterpolation = KFbxAnimCurveDef::eINTERPOLATION_CUBIC, KFbxAnimCurveDef::ETangentMode pTangentMode = KFbxAnimCurveDef::eTANGENT_AUTO, float pData0 = 0.0,float pData1 = 0.0,KFbxAnimCurveDef::EWeightedMode pTangentWeightMode = KFbxAnimCurveDef::eWEIGHTED_NONE, float pWeight0 = KFbxAnimCurveDef::kDEFAULT_WEIGHT,float pWeight1 = KFbxAnimCurveDef::kDEFAULT_WEIGHT,float pVelocity0 = KFbxAnimCurveDef::kDEFAULT_VELOCITY,float pVelocity1 = KFbxAnimCurveDef::kDEFAULT_VELOCITY) = 0;

    /**	Set a key with cubic interpolation, TCB tangent mode. 	
	* The key at index pKeyIndex is retrieved and modified according to the other parameters. 
	* The TCB mode controls the tension, continuity,
	* and bias of the curve.
    *   \param pKeyIndex  Index of the key.
    *	\param pTime	Key time.
    *	\param pValue	Key value.
    *	\param pData0	Tension. Controls the amount of curvature in the animation curve. The higher the tension is, the more linear
	* the curve looks. When the tension is low, the curve looks rounder or wider. 
    *	\param pData1	Continuity. Controls the smoothness or singularity of the curve on the key. 
    *	\param pData2	Bias. Controls if the effect of tension and continuity affect the curve before or after the key.
    */
    virtual void KeySetTCB(int pKeyIndex,KTime pTime, float pValue, float pData0 = 0.0f, float pData1 = 0.0f, float pData2 = 0.0f) = 0;

    /** Get key's interpolation type.
    *   \param pKeyIndex         Index of the queried key.
    *   \return                  Interpolation type of the queried key.
    */
    virtual KFbxAnimCurveDef::EInterpolationType KeyGetInterpolation(int pKeyIndex) const = 0;

    /** Set key's interpolation type.
    *   \param pKeyIndex      Index of the key.
    *	\param pInterpolation Key interpolation type.
    */
    virtual void KeySetInterpolation(int pKeyIndex, KFbxAnimCurveDef::EInterpolationType pInterpolation) = 0;

    /** Get key's constant mode.
    *	\note This method is only relevant if the key's interpolation type is constant (eINTERPOLATION_CONSTANT).
    *	Using this method on a key with an other interpolation type will return unpredictable value.
    *   \param pKeyIndex      Index of the queried key.
    *	\return Key constant mode.
    */
    virtual KFbxAnimCurveDef::EConstantMode KeyGetConstantMode(int pKeyIndex) const = 0;

    /** Get key's tangent mode.
    *   \param pKeyIndex  Index of the key.
    *   \param pIncludeOverrides Include override flags: Break, Clamp, Time-Independant.
    *	This method is meaningful for cubic interpolation only.
    *			 Using this method for non cubic interpolated key will return unpredictable value.
    *	\return Key tangent mode.
    */
    virtual KFbxAnimCurveDef::ETangentMode KeyGetTangentMode(int pKeyIndex, bool pIncludeOverrides = false ) const = 0;

    /** Set key's constant mode.
    *	This method is meaningful for constant interpolation only.
    *   \param pKeyIndex            Index of the key.
    *	\param pMode Key constant mode.
    */
    virtual void KeySetConstantMode(int pKeyIndex, KFbxAnimCurveDef::EConstantMode pMode) = 0;

    /** Set key's tangent mode.
    *	This method is meaningful for cubic interpolation only.
    *   \param pKeyIndex   Index of the key.
    *	\param pTangent Key tangent mode.
    */
    virtual void KeySetTangentMode(int pKeyIndex, KFbxAnimCurveDef::ETangentMode pTangent) = 0;

    /** Get key at given index.
    * \param pIndex Index of the key on the animation curve.
    * \return Pointer to the key at the given index.
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    */
	virtual KFbxAnimCurveKey KeyGet(int pIndex) const = 0;

    /**	Get key value.
    * \param pKeyIndex Index of the queried key.
	* \return Key value.
    */
    virtual float KeyGetValue(int pKeyIndex) const = 0;

    /** Set key value.
    * \param pKeyIndex Index of the key.
    * \param pValue The value to set.
    */ 
    virtual void KeySetValue(int pKeyIndex, float pValue) = 0;

    /** Increment key value.
    *   \param pKeyIndex   Index of the key.
    *	\param pValue Term added to the key value.
    */
    virtual void KeyIncValue(int pKeyIndex, float pValue) = 0;

    /** Multiply key value.
    * \param pKeyIndex   Index of the key.
    * \param pValue Factor multiplying the key value.
	* \see KFbxAnimCurve::KeyMultTangent.
    */
    virtual void KeyMultValue(int pKeyIndex, float pValue) = 0;

    /** Multiply key tangents.
    *	\remark When multiplying a key value, tangents must be
    *         multiplied to conserve the same topology.
    *   \param pKeyIndex   Index of the key.
    *	\param pValue Factor multiplying the key tangents.
    */
    virtual void KeyMultTangent(int pKeyIndex, float pValue) = 0;

    /** Get key time
    *   \param pKeyIndex   Index of the queried key.
    *	\return Key time (time at which this key is occurring).
    */
    virtual KTime KeyGetTime(int pKeyIndex) const = 0;

    /** Set key time.
    * \param pKeyIndex   Index of the key.
    * \param pTime Key time (time at which this key is occurring).
	* \remark The new key time might modify the key index.
    */
    virtual void KeySetTime(int pKeyIndex, KTime pTime) = 0;

    /** Set or unset the tangent break. When this flag is set (KFbxAnimCurveDef::eTANGENT_BREAK), the key's left and right slopes are independent.
	* When this flag is off, the key's left and right slope are equal.
    * This method is relevant for User (KFbxAnimCurveDef::eTANGENT_USER) and Auto (KFbxAnimCurveDef::eTANGENT_AUTO) tangent modes only.
    * \param pKeyIndex Index of the key.
    * \param pVal Break flag (\c true or \c false).
    */
    virtual void KeySetBreak(int pKeyIndex, bool pVal) = 0; 

    /** Get if the tangent has a break. When this flag is set (KFbxAnimCurveDef::eTANGENT_BREAK), the key's left and right slopes are independent.
	* When this flag is off, the key's left and right slope are equal.
    * This method is relevant for User (KFbxAnimCurveDef::eTANGENT_USER) and Auto (KFbxAnimCurveDef::eTANGENT_AUTO) tangent modes only.
    * \param pKeyIndex Index of the queried key.
	* \return Break flag (\c true or \c false).
    */
    virtual bool KeyGetBreak(int pKeyIndex) const = 0; 
    //@}

    /**
      * \name Key Tangent Management.
      */
    //@{
    /** Get the left derivative of a key.
    *	\param pIndex Index of the queried key.
    *	\return Left derivative (Value over time (s)).
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    */
    virtual float KeyGetLeftDerivative(int pIndex) = 0;

    /** Set the left derivative of a key.
    *	\param pIndex Index of the key.
    *	\param pValue Left derivative.
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    * This function is only relevant if previous key interpolation
    * type is eINTERPOLATION_CUBIC and tangent mode is
    * KFbxAnimCurveDef::eTANGENT_USER, KFbxAnimCurveDef::eTANGENT_BREAK or KFbxAnimCurveDef::eTANGENT_AUTO. 
    */
    virtual void KeySetLeftDerivative(int pIndex, float pValue) = 0;

    /** Get the left auto parametric of a key. This is used to compute the slope of Auto and User keys.
    *	\param pIndex Index of the key.
    *	\param pApplyOvershootProtection Clamp flag (eGENERIC_CLAMP) is taken into account.
    *	\return Left auto parametric.
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    */
    virtual float KeyGetLeftAuto(int pIndex, bool pApplyOvershootProtection = false) = 0;

    /** Get the left derivative info (of type KFCurveTangeantInfo) of a key.
    *	\param pIndex Index of the queried key.
    *	\return Left derivative info.
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    */
	virtual KFCurveTangeantInfo KeyGetLeftDerivativeInfo(int pIndex) = 0;

    /** Set the left derivative info (of type KFCurveTangeantInfo) of a key.
    *	\param pIndex Index of the key.
    *	\param pValue Left derivative info.
    *   \param pForceDerivative If \c true, assign the tangent info's derivative value to the key derivative.
	* If \c false, use the tangent info's auto parametric value to recompute the key derivative.
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    * This function is only relevant if previous key interpolation
    * type is eINTERPOLATION_CUBIC and tangent mode is
    * KFbxAnimCurveDef::eTANGENT_USER or KFbxAnimCurveDef::eTANGENT_BREAK.
    */
	virtual void KeySetLeftDerivativeInfo(kFCurveIndex pIndex, KFCurveTangeantInfo pValue, bool pForceDerivative = false) = 0;

    /** Get the right derivative of a key.
    *	\param pIndex Index of the key.
    *	\return Right derivative (Value over time (s)).
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    */
    virtual float KeyGetRightDerivative(int pIndex) = 0;

    /** Set the right derivative of a key.
    *	\param pIndex Index of the key.
    *	\param pValue Right derivative.
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    * This function is only relevant if previous key interpolation
    * type is eINTERPOLATION_CUBIC and tangent mode is
    * KFbxAnimCurveDef::eTANGENT_USER, KFbxAnimCurveDef::eTANGENT_BREAK or KFbxAnimCurveDef::eTANGENT_AUTO.
    */
    virtual void KeySetRightDerivative(int pIndex, float pValue) = 0;

    /** Get the right auto parametric of a key. This is used to compute the slope of Auto and User keys.
    *	\param pIndex Index of the key.
    *	\param pApplyOvershootProtection Clamp flag (eGENERIC_CLAMP) is taken into account.
    *	\return Right auto parametric.
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    */
    virtual float KeyGetRightAuto(int pIndex, bool pApplyOvershootProtection = false) = 0;

    /** Get the right derivative info (of type KFCurveTangeantInfo) of a key.
    *	\param pIndex Index of the queried key.
    *	\return Right derivative info.
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    */
	virtual KFCurveTangeantInfo KeyGetRightDerivativeInfo(int pIndex) = 0;

    /** Set the right derivative info (of type KFCurveTangeantInfo) of a key.
    *	\param pIndex Index of the key.
    *	\param pValue Right derivative info.
    *   \param pForceDerivative If \c true, assign the tangent info's derivative value to the key derivative.
	* If \c false, use the tangent info's auto parametric value to recompute the key derivative.
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    * This function is only relevant if previous key interpolation
    * type is eINTERPOLATION_CUBIC and tangent mode is
    * KFbxAnimCurveDef::eTANGENT_USER or KFbxAnimCurveDef::eTANGENT_BREAK.
    */
    virtual void KeySetRightDerivativeInfo(kFCurveIndex pIndex, KFCurveTangeantInfo pValue, bool pForceDerivative = false) = 0;

    /** Get the right tangent weight mode of a key.
    *	\param pIndex Index of queried key.
    *	\return \c true if the key is right weighted (Weight mode is eWEIGHT_WEIGHTED_RIGHT or eWEIGHTED_ALL). \c false otherwise.
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    */
    virtual bool KeyIsLeftTangentWeighted(int pIndex) const = 0;

    /** Get the right tangent weight mode of a key.
    *	\param pIndex Index of queried key.
    *	\return \c true if the key is right weighted (Weight mode is eWEIGHTED_RIGHT or eWEIGHTED_ALL). \c false otherwise.
    * Result is undetermined if animation curve has no key or if index 
    * is out of bounds.
    */
    virtual bool KeyIsRightTangentWeighted(int pIndex) const = 0;

    /** Get the weight value component of the left tangent of a key.
    *	\param pIndex Index of the key.
    *	\return Left tangent weight, or eDEFAULT_WEIGHT (0.333...) if left tangent is not weighted.
    * This function is only relevant if key interpolation
    * type is eINTERPOLATION_CUBIC.
    */
    virtual float KeyGetLeftTangentWeight(int pIndex) const = 0;

    /** Get the weight value component of the right tangent of a key.
    *	\param pIndex Index of the key.
    *	\return Right tangent weight, or eDEFAULT_WEIGHT (0.333...) if right tangent is not weighted.
    * This function is only relevant if key interpolation
    * type is eINTERPOLATION_CUBIC.
    */	
    virtual float KeyGetRightTangentWeight(int pIndex) const = 0;

    /** Set the left tangent weight of a key
    *	\param pIndex Index of the key.
    *	\param pWeight Weight to set on the left tangent.
    *   \param pAdjustTan If true, recompute the tangent height to compensate for very small weights.
    * This function is only relevant if previous key interpolation
    * type is eINTERPOLATION_CUBIC and tangent mode is
    * KFbxAnimCurveDef::eTANGENT_USER or KFbxAnimCurveDef::eTANGENT_BREAK. The tangent is 
    * automatically set in weighted mode.
    * \remarks The pAdjustTan option will only produce correct results provided that the tangent has already been
    * set before calling this function.
    */
    virtual void   KeySetLeftTangentWeight( int pIndex, float pWeight, bool pAdjustTan = false ) = 0;

    /** Set the right tangent weight of a key
    *	\param pIndex Index of the key.
    *	\param pWeight Weight to set on the right tangent.
    *   \param pAdjustTan If true, recompute the tangent height to compensate for very small weights.
    * This function is only relevant if key interpolation
    * type is eINTERPOLATION_CUBIC and tangent mode is
    * KFbxAnimCurveDef::eTANGENT_USER or KFbxAnimCurveDef::eTANGENT_BREAK. The tangent is 
    * automatically set in weighted mode.
    * \remarks The pAdjustTan option will only produce correct results provided that the tangent has already been
    * set before calling this function.
    */
    virtual void   KeySetRightTangentWeight( int pIndex, float pWeight, bool pAdjustTan = false  ) = 0;

    /** Get the velocity value component of the left tangent of a key
    *	\param pIndex Index of the key.
    *	\return Tangent velocity of the left tangent.
    * This function is only relevant if key interpolation
    * type is eINTERPOLATION_CUBIC
    */
    virtual float KeyGetLeftTangentVelocity( int pIndex) const = 0;

    /** Get the velocity value component of the right tangent of a key
    *	\param pIndex Index of the key.
    *	\return Tangent velocity of the right tangent.
    * This function is only relevant if key interpolation
    * type is eINTERPOLATION_CUBIC
    */			
    virtual float KeyGetRightTangentVelocity( int pIndex) const = 0;

    //@}

    /**
      * \name Evaluation and Analysis.
      */
    //@{
    /**	Evaluate animation curve value at a given time.
    *	\param pTime Time of evaluation.
    * If time falls between two keys, animation curve value is 
    * interpolated according to previous key interpolation type and
    * tangent mode if relevant.
    * \param pLast Index to speed up search. If this 
    * function is called in a loop, initialize this value to 0 and let it 
    * be updated by each call.
    * \return Animation curve value on given time, or animation curve's default value if animation curve
    * has no key.
    * This function takes extrapolation into account.
    */
    virtual float Evaluate (KTime pTime, int* pLast = NULL) = 0;

    /**	Evaluate animation curve value at a given key index.
    *	\param pIndex Any value between 0 and KFbxAnimCurve::KeyGetCount() - 1.
    * If key index is not an integer value, animation curve value is 
    * interpolated according to previous key interpolation type and
    * tangent mode, if relevant.
    * This function does not take extrapolation into account.
    *	\return Animation curve value, or default value if animation curve
    * has no key. Result is undetermined if index is out of bounds.
    */
    virtual float EvaluateIndex( double pIndex) = 0;
        
    /**	Evaluate function left derivative at given time.
    *	\param pTime Time of evaluation.
    * \param pLast Key index to speed up search. If this 
    * function is called in a loop, initialize this value to 0 and let it 
    * be updated by each call.
    * \return Left derivative at given time.
    * This function does not take extrapolation into account. Result is undetermined if index is out of bounds.
    */
    virtual float EvaluateLeftDerivative (KTime pTime, int* pLast = NULL) = 0;
        
    /**	Evaluate function right derivative at given time.
    *	\param pTime Time of evaluation.
    * \param pLast Key index to speed up search. If this 
    * function is called in a loop, initialize this value to 0 and let it 
    * be updated by each call.
    *	\return Right derivative at given time.
    * This function does not take extrapolation into account. Result is undetermined if index is out of bounds.
    */
    virtual float EvaluateRightDerivative (KTime pTime, int* pLast = NULL) = 0;
    //@}

    /**
      * \name Utility functions.
      *
      */
    //@{
    /** Find out start and end time of the animation animation curve.
      * This function retrieves the animation curve's time span.
      * \param pStart Reference to receive start time.
      * \param pStop Reference to receive end time.
      * \return \c true on success, \c false otherwise.
      */
    virtual bool GetTimeInterval(KTime& pStart, KTime& pStop) = 0;

    /** Returns the pointer to KFCurve.
      * \return Pointer to (internal) KFCurve.
	  * \remarks Subject to be deprecated (2010)
      */
    virtual KFCurve* GetKFCurve() = 0;

	/** Copy animation curve content into current animation curve.
      * \param pSource Animation curve to be copied (which will not be modified).
      * \param pWithKeys If \c true, clear keys in current animation curve and copy
      * keys from source animation curve. If \c false, keys in current animation curve
      * are left as is.
    */
    virtual void CopyFrom(KFbxAnimCurve& pSource, bool pWithKeys = true) = 0;
    //@}

#ifndef DOXYGEN_SHOULD_SKIP_THIS
    ///////////////////////////////////////////////////////////////////////////////
    //  WARNING!
    //    Anything beyond these lines may not be documented accurately and is 
    //     subject to change without notice.
    ///////////////////////////////////////////////////////////////////////////////

	virtual bool Store(KFbx* pFileObject) = 0;
    virtual bool Retrieve(KFbx* pFileObject) = 0;

protected:
    KFbxAnimCurve(KFbxSdkManager& pManager, char const* pName);

	virtual void Construct(const KFbxAnimCurveBase* pFrom);
	virtual void Destruct(bool pRecursive, bool pDependents);

#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS
};

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXANIMCURVE_H
