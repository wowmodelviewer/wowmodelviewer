/*
 * MpqFileProvider.cpp
 *
 * Placeholder. See MpqFileProvider.h.
 */

#include "MpqFileProvider.h"

#include "logger/Logger.h"

void wow::MpqFileProvider::warnOnce() const
{
  static bool warned = false;
  if (!warned)
  {
    warned = true;
    LOG_ERROR << "[fileprovider] MPQ storage is not implemented yet -- old MoPaQ clients "
                 "(Vanilla/TBC/WotLK/Cataclysm) cannot be opened. A MoPaQ backend (StormLib) "
                 "is a later milestone. No files will be served by the MPQ provider.";
  }
}

bool wow::MpqFileProvider::openById(int /*fileDataId*/, void ** /*handle*/)
{
  warnOnce();
  return false;
}

bool wow::MpqFileProvider::openByName(const std::string & /*name*/, void ** /*handle*/)
{
  warnOnce();
  return false;
}

bool wow::MpqFileProvider::closeFile(void * /*handle*/)
{
  // Nothing is ever opened by this placeholder, so there is nothing to close.
  return false;
}
