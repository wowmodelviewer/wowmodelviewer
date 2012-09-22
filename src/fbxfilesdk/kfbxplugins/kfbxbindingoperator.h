/*!  \file kfbxbindingoperator.h
*/

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXBINDINGOPERATOR_H
#define FBXFILESDK_KFBXPLUGINS_KFBXBINDINGOPERATOR_H

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

#include <fbxfilesdk/kfbxplugins/kfbxbindingtablebase.h>

#include <fbxfilesdk/components/kbaselib/klib/kmap.h>
#include <fbxfilesdk/components/kbaselib/klib/kstring.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

/** This object represents a binding operation on a KFbxObject or KFbxProperty.
  * For example, KFbxBindingOperator can be used to bind a light object
  * to a parameter of shader via KFbxNodeDirectionBOF or KFbxNodePositionBOF.
  * \code
  *  // create entry
  *  KFbxBindingTableEntry& lEntry = lTable->AddNewEntry();
  *  
  *  //Create NodePosition binding operator function
  *  KFbxOperatorEntryView lSrc(&lEntry, true, true);
  *  lSrc.SetOperatorName( "NodePositionConvert");
  *  KFbxBindingOperator* lOp = pImpl.AddNewBindingOperator( "NodePositionConvert", KFbxNodePositionBOF::FunctionName);
  *  
  *  //Add property entry to operator to remember the property's name
  *  KFbxBindingTableEntry& lEntryPropParam = lOp->AddNewEntry();
  *  KFbxPropertyEntryView lPropSrc(&lEntryPropParam, true, true);
  *  lPropSrc.SetProperty(lProp.GetHierarchicalName());
  *  lEntryPropParam.SetDestination( KFbxNodePositionBOF::FunctionName );
  *  
  *  // set dst to the shader parameter
  *  KFbxSemanticEntryView lDst( &lEntry, false, true );
  *  lDst.SetSemantic( lProp.GetName() );
  * \endcode
  * \nosubgrouping
  * \see KFbxOperatorEntryView, KFbxBindingTableEntry, KFbxPropertyEntryView
  */
class KFBX_DLL KFbxBindingOperator : public KFbxBindingTableBase
{
    KFBXOBJECT_DECLARE(KFbxBindingOperator,KFbxBindingTableBase);

public:
    /** Run the operator on the given object.
      * \param pObject The object that will be evaluated.
      * \param pResult A pointer to a buffer to hold the result.
      * \return true on success, false otherwise.
      */
    template <class FBXTYPE>
    bool Evaluate(KFbxObject const* pObject, FBXTYPE* pResult) const
    {
        EFbxType lResultType;
        void* lResult = NULL;

        bool lSuccess = Evaluate(pObject, &lResultType, &lResult);

        if (lSuccess)
        {
            fbxCopy(*pResult, lResult, lResultType);
        }

        FreeEvaluationResult(lResultType, lResult);

        return lSuccess;
    }
    
    /** Run the 'inverse' operator on the given object,
      * assigning the result directly to the object.
      * \param pObject The object that will be evaluated.
      * \param pInOut Type of value being reversed.
      * \param setObj Control to set the property (default only queries).
      * \param index Used only in KFbxMultiplyDistBOF ('1' get's realWorldScale).
      * \return true on success, false otherwise.
      */
    template <class FBXTYPE>
    bool ReverseEvaluation(KFbxObject const* pObject, FBXTYPE * pInOut, 
                            bool setObj=false, int index=0) const
    {

        void const* lIn = pInOut;
        void* lOut = NULL;
        EFbxType lOutType;

        bool lSuccess = ReverseEvaluate(pObject, lIn, &lOut, &lOutType, setObj, index);

        if (lSuccess)
        {
            fbxCopy(*pInOut, lOut, lOutType);
        }

        FreeEvaluationResult(lOutType, lOut);

        return lSuccess;
    }

    /** Evaluate the value of an operator parameter.
      * \param pObject The object that will be evaluated.
      * \param pEntryDestinationName The name of the parameter.
      * \param pResult A pointer to the result.
      * \return true on success, false otherwise.
      */
    template <class FBXTYPE>
    bool EvaluateEntry(KFbxObject const* pObject, char const* pEntryDestinationName, FBXTYPE* pResult) const
    {
        EFbxType lResultType;
        void* lResult = NULL;

        bool lSuccess = EvaluateEntry(pObject, pEntryDestinationName, &lResultType, &lResult);

        if (lSuccess)
        {
            fbxCopy(*pResult, lResult, lResultType);
        }

        FreeEvaluationResult(lResultType, lResult);

        return lSuccess;
    }

    // Properties
    KFbxTypedProperty<fbxString> FunctionName;
    KFbxTypedProperty<fbxString> TargetName;

    //////////////////////////////////////////////////////////////////////////
    // Static values
    //////////////////////////////////////////////////////////////////////////

    // property names
    static const char* sFunctionName;
    static const char* sTargetName;

    // property default values
    static const char* sDefaultFunctionName;
    static const char* sDefaultTargetName;


    //////////////////////////////////////////////////////////////////////////
    // Functions
    //////////////////////////////////////////////////////////////////////////

    /** \internal
      * 
      */
    static void RegisterFunctions();

    /** \internal 
      * 
      */
    static void UnregisterFunctions();


    /** It represents a binding relationship between current object and the target.
      * Any binding operation need to specify a certain kind of binding function.
      * \nosubgrouping
      */
    class KFBX_DLL Function
    {
    public:
        virtual ~Function() {}
        virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const = 0;
        virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const = 0;
    };

    /** The abstract factory class for binding function.
      * \nosubgrouping
      */
    class KFBX_DLL FunctionCreatorBase
    {
    public:
        virtual ~FunctionCreatorBase() {}
        virtual char const* GetFunctionName() const = 0;
        virtual Function* CreateFunction() const = 0;
    };

    /** The concrete factory class for binding function.
      * \nosubgrouping
      */
    template <class FUNCTION>
    class FunctionCreator : public FunctionCreatorBase
    {
    public:
        virtual char const* GetFunctionName() const
        {
            return FUNCTION::FunctionName;
        }

        virtual Function* CreateFunction() const
        {
            return FbxSdkNew< FUNCTION >();
        }
    };

    /** This utility class is used for fast accessing the binding function creators by name.
      * \nosubgrouping
      */
    class KFBX_DLL FunctionRegistry
    {
    public:
        static void RegisterFunctionCreator(FunctionCreatorBase const& pCreator)
        {
            sRegistry.Insert(pCreator.GetFunctionName(), &pCreator);
        }

        static void UnregisterFunctionCreator(FunctionCreatorBase const& pCreator)
        {
            sRegistry.Remove(pCreator.GetFunctionName());
        }

        static FunctionCreatorBase const* FindCreator(char const* pName)
        {
            RegistryType::RecordType* lRecord = sRegistry.Find(pName);

            if (lRecord)
            {
                return lRecord->GetValue();
            }
            else
            {
                return NULL;
            }
        }

    private:
        typedef KMap<char const*, FunctionCreatorBase const*, KCharCompare> RegistryType;
        static RegistryType sRegistry;
    };


    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    bool EvaluateEntry(KFbxObject const* pObject, char const* pEntryDestinationName, EFbxType* pResultType, void** pResult) const;

    //[mt]Get property from operator entry
    bool GetEntryProperty(KFbxObject const* pObject, char const* pEntryDestinationName, KFbxProperty & pProp) const;

protected:
    // construction must be done through Create()
    KFbxBindingOperator(KFbxSdkManager& pManager, char const* pName);
    virtual void Destruct(bool pRecursive, bool pDependents);

    virtual bool ConstructProperties(bool pForceSet);
    KFbxObject* Clone(KFbxObject* pContainer, KFbxObject::ECloneType pCloneType) const;

    void InstantiateFunction();

    /** Run the operator on the given object.
    * \param pObject The object that will be evaluated.
    * \param pResultType Will be filled by the type of the result.
    * \param pResult Will be filled by a pointer to a buffer that hold the result.
    * The caller must call FreeEvaluationResult() when its done with this pointer.
    * \return true on success, false otherwise.
    */
    bool Evaluate(KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;

    /** Apply the 'reverse' operator
    */
    //bool ReverseEvaluate(KFbxObject const* pTarget, void const *pResult) const;
    bool ReverseEvaluate(KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;

    /** Free a buffer allocated by Evaluate().
    * \param pResult Type The type of data held by pResult.
    * \param pResult The pointer to free.
    */
    void FreeEvaluationResult(EFbxType pResultType, void* pResult) const;

    Function* mFunction;

#endif //DOXYGEN_SHOULD_SKIP_THIS
};


/** A pass through operator used to assign constants to parameters.
*/
class KFbxAssignBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object property specified by "X" and returns it.
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType Will be filled by the type of the result.
    * \param pResult Will be filled by a pointer to a buffer that hold the result.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;

    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxAssignBOF();
    virtual ~KFbxAssignBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};


/** A conditional operator that outputs one out of two properties, based on
  * the value of a predicate property.
  */
class KFbxConditionalBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object property specified by "predicate".
    * If the property value is true (!= 0, != ""), returns the value of the
    * property specified by "ifTrue", else returns the value of the property
    * specified by "ifFalse".
    *
    * Currently the data types supported for the input property are
    * limited to "integer", "boolean", "float", "double" and "string".
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;

    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxConditionalBOF();
    virtual ~KFbxConditionalBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};


/** A switch operator that outputs one out of n properties, based on
  * the value of a predicate property.
  */
class KFbxSwitchBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object property specified by "predicate".
    * Returns the value of the property specified by "case_n", where n
    * is the value of "predicate". If there is no case_n entry, returns
    * the value of the property specified by "default".
    *
    * Currently the data types supported for the predicate property are
    * limited to "integer" and "boolean".
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;

    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxSwitchBOF();
    virtual ~KFbxSwitchBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};


class KFbxTRSToMatrixBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object properties specified by "T", "R" and "S" and
    *   return a transform matrix.
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;

    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxTRSToMatrixBOF();
    virtual ~KFbxTRSToMatrixBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};


class KFbxAddBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object properties specified by "X" and "Y"
    *   return X+Y as a float.
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;

    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxAddBOF();
    virtual ~KFbxAddBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};


class KFbxSubstractBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object properties specified by "X" and "Y"
    *   return X-Y as a float.
    *
   * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;
    
    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxSubstractBOF();
    virtual ~KFbxSubstractBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};


class KFbxMultiplyBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object properties specified by "X" and "Y"
    *   return X*Y as a float.
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;
    
    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxMultiplyBOF();
    virtual ~KFbxMultiplyBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};


class KFbxMultiplyDistBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object properties specified by "X" and "Y"
    *   return X*Y as a float.
    *
   * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;
    
    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxMultiplyDistBOF();
    virtual ~KFbxMultiplyDistBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};

class KFbxOneOverXBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object properties specified by "X"
    *   return 1/X as a float.
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;

    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxOneOverXBOF();
    virtual ~KFbxOneOverXBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};

class KFbxPowerBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object properties specified by "X" and "Y"
    *   return X^Y as a float.
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;

    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxPowerBOF();
    virtual ~KFbxPowerBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};

class KFbxDegreeToRadian : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object property specified by "X"
    *   return X converted to radian as a float.
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;
    
    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxDegreeToRadian();
    virtual ~KFbxDegreeToRadian();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};


class KFbxVectorDegreeToVectorRadian : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object property specified by "X"
    *   return X converted to radian as a vector3.
    *
   * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;
    
    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxVectorDegreeToVectorRadian();
    virtual ~KFbxVectorDegreeToVectorRadian();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};



class KFbxSphericalToCartesianBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluates the object property specified by "rho", "theta" and "phi"
    *   return the converted Cartesian coordinates as a double3.
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;
    
    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxSphericalToCartesianBOF();
    virtual ~KFbxSphericalToCartesianBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};



class KFbxIsYup : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Check if the scene coordinate system is y-up
    *   return a bool.
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;
    
    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxIsYup();
    virtual ~KFbxIsYup();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};



/** A symbol(string) operator that search the string table and return its corresponding unique id, based on
  * the value of a predicate property.
  */
class KFbxSymbolIDBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Check in the symbol table the string and returns its unique ID as an integer
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;
    
    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxSymbolIDBOF();
    virtual ~KFbxSymbolIDBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};


/**  A chooser operator that check spot distribution and returns the correct value, based on
  *  the value of a predicate property.
  */
class KFbxSpotDistributionChooserBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Check the enum of the spot distribution and returns the correct value
    *   as an int.
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;
    
    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxSpotDistributionChooserBOF();
    virtual ~KFbxSpotDistributionChooserBOF();


#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};


/**  A evaluation operator that get the node position and returns transform value, based on
  *  the value of a predicate property.
  */
class KFbxNodePositionBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluate the parent's position,
    *    return the transform node translation.
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;
    
    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxNodePositionBOF();
    virtual ~KFbxNodePositionBOF();

#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};


/**  A evaluation operator that get the node direction and returns transform value, based on
  *  the value of a predicate property.
  */
class KFbxNodeDirectionBOF : public KFbxBindingOperator::Function
{
public:
    static char const* FunctionName;

    /** Evaluate the parent's direction,
    *    return the transform node direction based on rotation.
    *
    * \param pOperator Operator running on the object.
    * \param pObject The object that will be evaluated.
    * \param pResultType The type of the result to be returned.
    * \param pResult A pointer to a buffer that can hold the result.
    * \return true on success, false otherwise.
    */
    virtual bool Evaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pObject, EFbxType* pResultType, void** pResult) const;
    virtual bool ReverseEvaluate(KFbxBindingOperator const* pOperator, KFbxObject const* pTarget, void const* pIn, void** pOut, EFbxType* pOutType, bool setObj, int index) const;
    
    ///////////////////////////////////////////////////////////////////////////////
    //
    //  WARNING!
    //
    //  Anything beyond these lines may not be documented accurately and is
    //  subject to change without notice.
    //
    ///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    KFbxNodeDirectionBOF();
    virtual ~KFbxNodeDirectionBOF();

#endif //#ifndef DOXYGEN_SHOULD_SKIP_THIS
};

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXBINDINGOPERATOR_H

