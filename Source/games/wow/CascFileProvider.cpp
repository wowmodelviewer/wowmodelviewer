/*
 * CascFileProvider.cpp
 */

#include "CascFileProvider.h"

#include "CASCFolder.h"

bool wow::CascFileProvider::openById(int fileDataId, void ** handle)
{
  if (!m_casc)
    return false;
  // HANDLE is void*, so HANDLE* and void** are the same type -- forward verbatim. This is the
  // exact call WoWFolder::openFile used to make directly, so behaviour is unchanged.
  return m_casc->openFile(fileDataId, handle);
}

bool wow::CascFileProvider::openByName(const std::string & /*name*/, void ** /*handle*/)
{
  // CASC on modern roots carries no filename hashes; opening by name is not supported here.
  // WoWFolder maps the name to a FileDataID (via the listfile) and calls openById instead.
  return false;
}

bool wow::CascFileProvider::closeFile(void * handle)
{
  if (!m_casc)
    return false;
  return m_casc->closeFile(handle);
}
