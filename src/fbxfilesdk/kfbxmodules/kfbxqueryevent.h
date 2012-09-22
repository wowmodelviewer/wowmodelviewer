/*!  \file kfbxqueryevent.h
 */

#ifndef FBXFILESDK_KFBXMODULES_KFBXQUERYEVENT_H
#define FBXFILESDK_KFBXMODULES_KFBXQUERYEVENT_H

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

// FBX includes
#include <fbxfilesdk/kfbxevents/kfbxevents.h>

// FBX begin namespace
#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

// FIXME: Why is this class defined twice? 
// See notes in kfbxplugins/kfbxqueryevent.h

    /** \class KFbxQueryEvent
    *
    * \brief A query event is something that is emitted by an entity, with the goal of being filled by someone that listen to it. 
    *        You can see that like a form that you send to some people. If those people know how to fill the form, they fill it and return
    *        it to you with the right information in it.  A query event is emitted, and plug-in who are listening to that type of query, 
    *        fill the data that can be accessed by the query emitter.
    */
    template <typename QueryT>
    class KFbxQueryEvent : public kfbxevents::KFbxEvent<KFbxQueryEvent<QueryT> >
    {
    public:
        /**
        *\name Public interface
        */
        //@{
        /** Constructor
		  * \param pData  
          */
        explicit KFbxQueryEvent(QueryT* pData):mData(pData){}

        /** Accessor to a mutable reference to the data. Event are usually const and can't be modified by listener. This
          * special type of event can have is content modified via this accessor.
         * \return A mutable reference the requested data.
        */
        QueryT& GetData()const { return *mData; }
        //@}

    private:
        mutable QueryT* mData;

    private:
        virtual const char* GetEventName() const { K_ASSERT(false); return ""; }
        static const char* FbxEventName() { K_ASSERT(false); return ""; }
        friend class KFbxEvent< KFbxQueryEvent<QueryT> >;
    };
#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXMODULES_KFBXQUERYEVENT_H

