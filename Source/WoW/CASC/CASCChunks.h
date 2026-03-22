/*
 * CASCChunks.h
 *
 *  Created on: 25 nov. 2017
 *
 */

#pragma once

#include "types.h"

/// @brief Animation file ID entry mapping anim/sub-anim to a CASC file ID (AFID chunk).
struct AFID
{
	uint16 animId;     ///< Animation ID.
	uint16 subAnimId;  ///< Sub-animation ID.
	uint32 fileId;     ///< CASC file data ID.
};

/// @brief Skeleton sequence data header (SKS1 chunk).
struct SKS1
{
	uint32 nGlobalSequences;     ///< Number of global sequences.
	uint32 ofsGlobalSequences;   ///< Offset to global sequences.
	uint32 nAnimations;          ///< Number of animations.
	uint32 ofsAnimations;        ///< Offset to animations.
	uint32 nAnimationLookup;     ///< Number of animation lookup entries.
	uint32 ofsAnimationLookup;   ///< Offset to animation lookup table.
};

/// @brief Skeleton attachment data header (SKA1 chunk).
struct SKA1
{
	uint32 nAttachments;    ///< Number of attachments.
	uint32 ofsAttachments;  ///< Offset to attachments.
	uint32 nAttachLookup;   ///< Number of attachment lookup entries.
	uint32 ofsAttachLookup; ///< Offset to attachment lookup table.
};

/// @brief Skeleton bone data header (SKB1 chunk).
struct SKB1
{
	uint32 nBones;           ///< Number of bones.
	uint32 ofsBones;         ///< Offset to bones.
	uint32 nKeyBoneLookup;   ///< Number of key bone lookup entries.
	uint32 ofsKeyBoneLookup; ///< Offset to key bone lookup table.
};

/// @brief Skeleton parent data chunk (SKPD).
struct SKPD
{
	uint8 unknown00[8];     ///< Unknown padding bytes.
	uint32 parentFileId;    ///< File data ID of the parent skeleton.
};

/// @brief Texture file ID entry (TXID chunk).
struct TXID
{
	uint32 fileDataId;  ///< CASC file data ID for the texture.
};
