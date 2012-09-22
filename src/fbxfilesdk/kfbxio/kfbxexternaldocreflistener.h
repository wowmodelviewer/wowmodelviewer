/*!  \file kfbxexternaldocreflistener.h
 */

#ifndef FBXFILESDK_KFBXIO_KFBXEXTERNALDOCREFLISTENER_H
#define FBXFILESDK_KFBXIO_KFBXEXTERNALDOCREFLISTENER_H

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

#include <fbxfilesdk/kfbxevents/kfbxevents.h>
#include <fbxfilesdk/kfbxevents/kfbxlistener.h>
#include <fbxfilesdk/components/kbaselib/klib/kstring.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

class KFbxObject;
class KFbxDocument;
class KFbxSdkManager;

/** Holder of data about an external document.
  */
struct KFBX_DLL KFbxExternalDocumentInfo
{
    KString mDocumentName;      // Bare name of external document in document hierarchy
    KString mClassName;         // Class name of the document (KFbxDocument, KFbxLibrary...)
    KString mParentFullName;    // Full name of the parent document in document hierarchy.
    KString mFilePathUrl;       // File path of the external document.
};

/** Event that is emitted on loading document when a referenced document
  * is encountered while loading external references.
  */
class KFBX_DLL KFbxEventReferencedDocument : public KFbxEvent<KFbxEventReferencedDocument>, public KFbxExternalDocumentInfo
{
    KFBXEVENT_DECLARE(KFbxEventReferencedDocument);
public:
    KFbxEventReferencedDocument() {}
};


class KFBX_DLL KFbxExternalDocRefListenerData;

/** Typical handler for the referenced document events.
* 
* Register it like so:
* KFbxExternalDocRefListener lRefDocListener( sdkManager, fileName );
* kfbxevents::KFbxEventHandler * lHandler = lRefDocListener.Bind(scene,
*                                                &KFbxExternalDocRefListener::HandleEvent);  
* 
* And later unregister it like so:
*     lRefDocListener.Unbind(lHandler);
*/
class KFBX_DLL KFbxExternalDocRefListener : public kfbxevents::KFbxListener
{
public:
    /** Constructor. 
      * \param pManager
      * \param pDocFilePath
      * \remarks Keep a reference to the SDK and the path of the document
      * to be able to resolve relative paths. 
      */
    KFbxExternalDocRefListener( KFbxSdkManager & pManager, const KString & pDocFilePath );
    virtual ~KFbxExternalDocRefListener();

    /** Set the document file path used to resolve documents. 
      * \param pDocFilePath
      * \remarks Allows re-using the same instance for multiple document loadings.
      */
    virtual void SetDocumentFilePath( const KString & pDocFilePath );

    /** Verify that all documents that were previously loaded in a previous
      * load session are still valid.
      * \return \c true if all documents are still valid, \c false otherwise.
      */
    virtual bool AreAllExternalDocumentsStillValid() const;

    // 
    /** Verify that all documents that were referred to didn't change. 
      * \return \c true if all documents didn't change, \c false otherwise.
      * \remarks This function should be called if at posteriori check is desired.
      */
    virtual bool WereAllExternalDocumentsValid() const;

    /** Unload all documents that were loaded through this event handler. 
      */
    virtual void UnloadExternalDocuments();

    // External document reference event handler.
    //
    // Operation: calls FindDocument() to find the specified external document
    //            and if not found calls LoadDocument() either directly,
    //            if it has not parent, or via ConnectToParentLibrary().
    //            If its parent cannot be found, it's added to the dangling
    //            document list (and it is not loaded until it's parent is found).
    //            After, it tries to resolve dangling documents by calling
    //            TryConnectingDanglingLibraries().
    /** External document reference event handler. 
      * \param pEvent
      * \remarks    Operation: calls FindDocument() to find the specified external document
      *             and if not found calls LoadDocument() either directly,
      *             if it has not parent, or via ConnectToParentLibrary().
      *             If its parent cannot be found, it's added to the dangling
      *             document list (and it is not loaded until it's parent is found).
      *             After, it tries to resolve dangling documents by calling
      *             TryConnectingDanglingLibraries().
      */
    virtual void HandleEvent(const KFbxEventReferencedDocument * pEvent);

protected:
    // Turn the given possibly relative path to an absolute path using the file
    // path of the original document being loaded.
    virtual KString MakeFilenameAbsolute(const KString & pFilename) const;
    // Locate a document by its document path.
    virtual KFbxDocument * FindDocument( const KString & pPathToDoc );
    // Load a library, potentially under another library.
    virtual KFbxDocument * LoadDocument(KFbxObject * pParent, const KString & pClassName, const KString & pFilename);
    // Try to connect a library to its parent given its document path.
    virtual bool ConnectToParentLibrary(const KFbxExternalDocumentInfo &);
    // Try to reconnect dangling libraries that didn't find their parent.
    virtual void TryConnectingDanglingLibraries();

private:
    KFbxExternalDocRefListenerData * mData;
};

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXIO_KFBXEXTERNALDOCREFLISTENER_H

