/*
 * AnimManager.h
 *
 *  Created on: 19 oct. 2013
 *
 */

#pragma once

#include "modelheaders.h" // ModelAnimation

/// @brief Stores loop count and animation ID for a single animation slot.
struct AnimInfo
{
	short Loops;     ///< Number of loops to play.
	size_t AnimID;   ///< Animation identifier.
};

#define _ANIMMANAGER_API_

class WoWModel;

/// @brief Manages animation playback for a WoWModel, supporting up to 4 queued animations,
///        a secondary (upper-body) animation, and independent mouth movement.
class _ANIMMANAGER_API_ AnimManager
{
	WoWModel& model;

	bool Paused;

	AnimInfo animList[4];

	size_t Frame; // Frame number we're up to in the current animation
	size_t TotalFrames;

	ssize_t AnimIDSecondary;
	size_t FrameSecondary;
	size_t SecondaryCount;

	ssize_t AnimIDMouth;
	size_t FrameMouth;

	short Count; // Total index of animations
	short PlayIndex; // Current animation index we're upto
	short CurLoop; // Current loop that we're upto.

	float Speed; // The speed of which to multiply the time given for Tick();
	float mouthSpeed;

public:
	AnimManager(WoWModel& m);
	~AnimManager();
	void SetCount(int count);
	void AddAnim(unsigned int id, short loop); // Adds an animation to our array.
	void SetAnim(short index, unsigned int id, short loop);
	// sets one of the 4 existing animations and changes it (not really used currently)

	void SetSecondary(int id)
	{
		AnimIDSecondary = id;
		FrameSecondary = 0;
	}

	void ClearSecondary() { AnimIDSecondary = -1; }
	ssize_t GetSecondaryID() { return AnimIDSecondary; }
	size_t GetSecondaryFrame() { return FrameSecondary; }
	void SetSecondaryCount(int count) { SecondaryCount = count; }
	size_t GetSecondaryCount() { return SecondaryCount; }

	// For independent mouth movement.
	void SetMouth(int id)
	{
		AnimIDMouth = id;
		FrameMouth = 0;
	}

	void ClearMouth() { AnimIDMouth = -1; }
	ssize_t GetMouthID() { return AnimIDMouth; }
	size_t GetMouthFrame() { return FrameMouth; }

	void SetMouthSpeed(float speed)
	{
		mouthSpeed = speed;
	}

	void Play(); // Players the animation, and reconfigures if nothing currently inputed
	void Stop(); // Stops and resets the animation
	void Pause(bool force = false);
	// Toggles 'Pause' of the animation, use force to pause the animation no matter what.

	void Next(); // Plays the 'next' animation or loop
	void Prev(); // Plays the 'previous' animation or loop

	int Tick(int time);

	size_t GetFrameCount();
	size_t GetFrame() { return Frame; }
	void SetFrame(size_t f);
	void SetSpeed(float speed) { Speed = speed; }
	float GetSpeed() { return Speed; }

	void PrevFrame();
	void NextFrame();

	void Clear();
	void Reset() { Count = 0; }

	bool IsPaused() { return Paused; }

	size_t GetAnim() { return animList[PlayIndex].AnimID; }

	void ForceModelUpdate(float dt);
};
