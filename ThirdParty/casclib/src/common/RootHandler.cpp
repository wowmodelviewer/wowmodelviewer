/*****************************************************************************/
/* RootHandler.cpp                        Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Implementation of root handler                                            */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 09.03.15  1.00  Lad  Created                                              */
/*****************************************************************************/

#define __CASCLIB_SELF__
#include "../CascLib.h"
#include "../CascCommon.h"

//-----------------------------------------------------------------------------
// Constructor and destructor - TFileTreeRoot

TFileTreeRoot::TFileTreeRoot(DWORD FileTreeFlags) : TRootHandler()
{
    // Initialize the file tree
    FileTree.Create(FileTreeFlags);
}

TFileTreeRoot::~TFileTreeRoot()
{
    // Free the file tree
    FileTree.Free();
    dwFeatures = 0;
}

//-----------------------------------------------------------------------------
// Virtual functions - TFileTreeRoot

int TFileTreeRoot::Insert(
    const char * szFileName,
    PCASC_CKEY_ENTRY pCKeyEntry)
{
    PCASC_FILE_NODE pFileNode;
    DWORD FileDataId = FileTree.GetNextFileDataId();

    pFileNode = FileTree.InsertByName(pCKeyEntry, szFileName, FileDataId);

    // WMV: a node this call made (ENCODING, ROOT and the other storage files CascLib names itself, with a FileDataId
    // past every id the ROOT lists) is not a ROOT entry: FindEntry must not answer for it, nor GetNameCount count it.
    // (Copy() also inserts through here, but only when a non-WoW root is parsed again, which WMV never opens.)
    if(pFileNode != NULL)
    {
        DWORD NodeFileDataId = CASC_INVALID_ID;
        DWORD LocaleFlags = CASC_INVALID_ID;
        DWORD ContentFlags = CASC_INVALID_ID;

        FileTree.GetExtras(pFileNode, &NodeFileDataId, &LocaleFlags, &ContentFlags);
        if(NodeFileDataId == FileDataId && (pFileNode->Flags & CFN_FLAG_WMV_NOT_IN_ROOT) == 0)
        {
            pFileNode->Flags |= CFN_FLAG_WMV_NOT_IN_ROOT;
            WmvNotInRootNames++;
        }
    }
    return (pFileNode != NULL) ? ERROR_SUCCESS : ERROR_CAN_NOT_COMPLETE;
}

PCASC_CKEY_ENTRY TFileTreeRoot::GetFile(TCascStorage * /* hs */, const char * szFileName)
{
    PCASC_FILE_NODE pFileNode;
    ULONGLONG FileNameHash = CalcFileNameHash(szFileName);
    
    pFileNode = FileTree.Find(FileNameHash);
    return (pFileNode != NULL) ? pFileNode->pCKeyEntry : NULL;
}

PCASC_CKEY_ENTRY TFileTreeRoot::GetFile(TCascStorage * /* hs */, DWORD FileDataId)
{
    PCASC_FILE_NODE pFileNode;

    pFileNode = FileTree.FindById(FileDataId);
    return (pFileNode != NULL) ? pFileNode->pCKeyEntry : NULL;
}

PCASC_CKEY_ENTRY TFileTreeRoot::GetFile(size_t nFileIndex, char * szFileName, size_t ccFileName)
{
    PCASC_CKEY_ENTRY pCKeyEntry = NULL;
    PCASC_FILE_NODE pFileNode;

    // Perform the search in the underlying file tree
    if((pFileNode = FileTree.PathAt(szFileName, ccFileName, nFileIndex)) != NULL)
        pCKeyEntry = pFileNode->pCKeyEntry;
    return pCKeyEntry;
}

PCASC_CKEY_ENTRY TFileTreeRoot::Search(TCascSearch * pSearch, PCASC_FIND_DATA pFindData)
{
    PCASC_FILE_NODE pFileNode;
    size_t nMaxFileIndex = GetMaxFileIndex();

    // Are we still inside the root directory range?
    while(pSearch->nFileIndex < nMaxFileIndex)
    {
        //BREAKIF(pSearch->nFileIndex >= 2823765);

        // Retrieve the file item
        pFileNode = FileTree.PathAt(pFindData->szFileName, _countof(pFindData->szFileName), pSearch->nFileIndex++);
        if(pFileNode != NULL)
        {
            // Ignore folders, but report mount points. These can and should be able to open and read
            if((pFileNode->Flags & (CFN_FLAG_FOLDER | CFN_FLAG_MOUNT_POINT)) != CFN_FLAG_FOLDER)
            {
                // Check the wildcard
                if(CascCheckWildCard(pFindData->szFileName, pSearch->szMask))
                {
                    // Retrieve the extra values (FileDataId, file size and locale flags)
                    FileTree.GetExtras(pFileNode, &pFindData->dwFileDataId, &pFindData->dwLocaleFlags, &pFindData->dwContentFlags);

                    // Return the found CKey entry
                    return pFileNode->pCKeyEntry;
                }
            }
        }
    }

    // No more entries
    return NULL;
}

bool TFileTreeRoot::GetInfo(PCASC_CKEY_ENTRY pCKeyEntry, PCASC_FILE_FULL_INFO pFileInfo)
{
    PCASC_FILE_NODE pFileNode;

    // Can't do much if the root key is NULL
    if(pCKeyEntry != NULL)
    {
        pFileNode = FileTree.Find(pCKeyEntry);
        if(pFileNode != NULL)
        {
            FileTree.GetExtras(pFileNode, &pFileInfo->FileDataId, &pFileInfo->LocaleFlags, &pFileInfo->ContentFlags);
            pFileInfo->FileNameHash = pFileNode->FileNameHash;
            return true;
        }
    }

    return false;
}

// WMV: the node a name hash or a FileDataId leads to -- the same node CascOpenFile would open -- with the FileDataId
// the root gives it. A name and a FileDataId of one file share one node (InsertByHash inserts by id too).
bool TFileTreeRoot::FindEntry(const char * szFileName, DWORD FileDataId, PDWORD PtrFileDataId, PCASC_CKEY_ENTRY * PtrCKeyEntry)
{
    PCASC_FILE_NODE pFileNode = (szFileName != NULL) ? FileTree.Find(CalcFileNameHash(szFileName)) : FileTree.FindById(FileDataId);
    DWORD NodeFileDataId = CASC_INVALID_ID;
    DWORD LocaleFlags = CASC_INVALID_ID;
    DWORD ContentFlags = CASC_INVALID_ID;

    if(pFileNode == NULL || pFileNode->pCKeyEntry == NULL || (pFileNode->Flags & CFN_FLAG_WMV_NOT_IN_ROOT))
        return false;

    FileTree.GetExtras(pFileNode, &NodeFileDataId, &LocaleFlags, &ContentFlags);
    if(PtrFileDataId != NULL)
        PtrFileDataId[0] = NodeFileDataId;
    if(PtrCKeyEntry != NULL)
        PtrCKeyEntry[0] = pFileNode->pCKeyEntry;
    return true;
}

size_t TFileTreeRoot::Copy(TRootHandler * pRoot)
{
    PCASC_CKEY_ENTRY pCKeyEntry;
    size_t nMaxFileIndex = GetMaxFileIndex();
    size_t nItemsCopied = 0;
    char szFileName[0x200];

    for(size_t nFileIndex = 0; nFileIndex < nMaxFileIndex; nFileIndex++)
    {
        if((pCKeyEntry = pRoot->GetFile(nFileIndex, szFileName, _countof(szFileName))) != NULL)
        {
            if(szFileName[0] != 0)
            {
                Insert(szFileName, pCKeyEntry);
                nItemsCopied++;
            }
        }
    }

    return nItemsCopied;
}

size_t TFileTreeRoot::GetMaxFileIndex()
{
    return FileTree.GetMaxFileIndex();
}
