/*
 * CASCChunks.h
 *
 *  Created on: 25 nov. 2017
 *
 */

#ifndef _CASCCHUNKS_H_
#define _CASCCHUNKS_H_

#include "types.h"

struct AFID
{
  uint16 animId;
  uint16 subAnimId;
  uint32 fileId;
};

struct SKS1
{
  uint32 nGlobalSequences;
  uint32 ofsGlobalSequences;
  uint32 nAnimations;
  uint32 ofsAnimations;
  uint32 nAnimationLookup;
  uint32 ofsAnimationLookup;
};

struct SKA1
{
  uint32 nAttachments;
  uint32 ofsAttachments;
  uint32 nAttachLookup;
  uint32 ofsAttachLookup;
};

struct SKB1
{
  uint32 nBones;
  uint32 ofsBones;
  uint32 nKeyBoneLookup;
  uint32 ofsKeyBoneLookup;
};

struct SKPD
{
  uint8 unknown00[8];
  uint32 parentFileId;
};


#endif /* _CASCCHUNKS_H_ */
