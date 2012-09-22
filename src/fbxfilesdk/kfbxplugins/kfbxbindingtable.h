/*!  \file kfbxbindingtable.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXBINDINGTABLE_H 
#define FBXFILESDK_KFBXPLUGINS_KFBXBINDINGTABLE_H

/**************************************************************************************

 Copyright (C) 2001 - 2009 Autodesk, Inc. and/or its licensors.
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

// FBX SDK includes
#include <fbxfilesdk/kfbxplugins/kfbxbindingtablebase.h>

// FBX namespace
#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

/** \brief A binding table represents a collection of bindings
  * from source types such as KFbxObjects, or KFbxLayerElements
  * to corresponding destinations, usually a third party shader parameters.
  * Binding represents a link between internal object(e.g. KFbxObject) and 
  * external object(e.g. HLSL shader parameters).
  * \nosubgrouping
  * \see KFbxBindingOperator, KFbxBindingTableBase
  */
class KFBX_DLL KFbxBindingTable : public KFbxBindingTableBase
{
    KFBXOBJECT_DECLARE(KFbxBindingTable,KFbxBindingTableBase);

public:
    /** This property stores the name of target.
      *
      * Default value is ""
      */
    KFbxTypedProperty<fbxString>            TargetName;

    /** This property stores the type of target.
      *
      * Default value is ""
      */
    KFbxTypedProperty<fbxString>            TargetType;

    /** Relative URL of file containing the shader implementation description. 
      * e.g.: ./shader.mi
      * Default value is ""
      */
    KFbxTypedProperty<fbxString>            DescRelativeURL;

    /** Absolute URL of file containing the shader implementation description.
      * e.g.: file:///usr/tmp/shader.mi
      * Default value is ""
      */
    KFbxTypedProperty<fbxString>            DescAbsoluteURL;

    /** Identify the shader to use in previous description's URL.
      * e.g.: MyOwnShader
      * Default value is ""
      */
    KFbxTypedProperty<fbxString>            DescTAG;        

    /** Relative URL of file containing the shader implementation code.
      * e.g.: ./bin/shader.dll
      * Default value is ""
      */
    KFbxTypedProperty<fbxString>            CodeRelativeURL;

    /** Absolute URL of file containing the shader implementation code.
      * e.g.: file:///usr/tmp/bin/shader.dll
      * Default value is ""
      */
    KFbxTypedProperty<fbxString>            CodeAbsoluteURL;

    /** Identify the shader function entry to use in previous code's URL.
      * e.g.: MyOwnShaderFunc
      * Default value is ""
      */
    KFbxTypedProperty<fbxString>            CodeTAG;

    //////////////////////////////////////////////////////////////////////////
    // Static values
    //////////////////////////////////////////////////////////////////////////

    //! Target name.
    static const char* sTargetName;

    //! Target type.
    static const char* sTargetType;

    //! Relative URL for shader description. 
    static const char* sDescRelativeURL;

    //! Absolute URL for shader description.
    static const char* sDescAbsoluteURL;

    //! Identify the shader to use in previous description's URL.
    static const char* sDescTAG;

    //! Relative URL for shader code. 
    static const char* sCodeRelativeURL;

    //! Absolute URL for shader code.
    static const char* sCodeAbsoluteURL;

    //! Identify the shader function entry to use in previous code's URL.
    static const char* sCodeTAG;


    //! Default value for  target name.
    static const char* sDefaultTargetName;

    //! Default value for  target type.
    static const char* sDefaultTargetType;

    //! Default value for relative URL for shader description. 
    static const char* sDefaultDescRelativeURL;

    //! Default value for absolute URL for shader description. 
    static const char* sDefaultDescAbsoluteURL;

    //! Default value for identifying the shader to use in previous description's URL.
    static const char* sDefaultDescTAG;

    //! Default value for relative URL for shader code.
    static const char* sDefaultCodeRelativeURL;

    //! Default value for absolute URL for shader code. 
    static const char* sDefaultCodeAbsoluteURL;

    //! Default value for identifying the shader function entry to use in previous code's URL.
    static const char* sDefaultCodeTAG;

///////////////////////////////////////////////////////////////////////////////
//
//  WARNING!
//
//    Anything beyond these lines may not be documented accurately and is 
//     subject to change without notice.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef DOXYGEN_SHOULD_SKIP_THIS

private:
    KFbxObject* Clone(KFbxObject* pContainer, KFbxObject::ECloneType pCloneType) const;
    KFbxBindingTable(KFbxSdkManager& pManager, char const* pName);

    virtual bool ConstructProperties(bool pForceSet);

#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS

};

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXBINDINGTABLE_H

