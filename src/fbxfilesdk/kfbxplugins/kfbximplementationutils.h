/*!  \file kfbximplementationutils.h
 */
#ifndef FBXFILESDK_KFBXPLUGINS_KFBXIMPLEMENTATIONUTILS_H 
#define FBXFILESDK_KFBXPLUGINS_KFBXIMPLEMENTATIONUTILS_H

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

#include <fbxfilesdk/kfbxplugins/kfbximplementation.h>
#include <fbxfilesdk/kfbxplugins/kfbxbindingoperator.h>
#include <fbxfilesdk/kfbxplugins/kfbxoperatorentryview.h>
#include <fbxfilesdk/kfbxplugins/kfbxpropertyentryview.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

/** GetImplementation               Get KFbxImplementation from KFbxObject
  * \param pObject                  KFbxObject to get KFbxImplementation
  * \param pImplementationTarget    name of the implementation property to get
  * \return KFbxImplementation      Pointer to KFbxImplementation
  */
KFBX_DLL KFbxImplementation const* GetImplementation( KFbxObject const* pObject, char const* pImplementationTarget );

template <typename T> bool GetBoundPropertyValue(KFbxBindingTable const* pBindingTable,
                                                 char const* pEntryName, 
                                                 KFbxImplementation const* pImplementation,
                                                 KFbxObject const* pBoundObject,
                                                 T& pValue)
{
    if ((NULL != pImplementation) && (NULL != pBindingTable) && (NULL != pBoundObject) && (NULL != pEntryName))
    {
        KFbxBindingTableEntry const* lEntry = pBindingTable->GetEntryForDestination(pEntryName);

        if (NULL != lEntry)
        {
            if (strcmp(lEntry->GetEntryType(true), KFbxPropertyEntryView::sEntryType) == 0)
            {
                char const* lPropName = lEntry->GetSource();
                KFbxProperty lProp = pBoundObject->FindPropertyHierarchical(lPropName);
                if (lProp.IsValid())
                {
                    KFbxGet(lProp, pValue);
                    return true;
                }
            }
            else if (strcmp(lEntry->GetEntryType(true), KFbxOperatorEntryView::sEntryType) == 0)
            {
                char const* lOperatorName = lEntry->GetSource();
                KFbxBindingOperator const* lOp = pImplementation->GetOperatorByTargetName(lOperatorName);
                if (lOp)
                {
                    return lOp->Evaluate(pBoundObject, &pValue);
                }
            }
        }
    }

    return false;
}

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXIMPLEMENTATIONUTILS_H

