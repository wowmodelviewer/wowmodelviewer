/*
 * AninManager.h
 *
 *  Created on: 19 oct. 2013
 *
 */

#ifndef _ANIMMANAGER_H_
#define _ANIMMANAGER_H_

#include "modelheaders.h" // ModelAnimation

// This will be our animation manager
// instead of using a STL vector or list or table, etc.
// Decided to just limit it upto 4 animations to loop through - for experimental testing.
// The second id and loop count will later be used for being able to have a primary and secondary animation.

// Currently, this is more of a "Wrapper" over the existing code
// but hopefully over time I can remove and re-write it so this is the core.
struct AnimInfo {
	short Loops;
	size_t AnimID;
};


class AnimManager {
	ModelAnimation *anims;

	bool Paused;
	bool AnimParticles;

	AnimInfo animList[4];

	size_t Frame;		// Frame number we're upto in the current animation
	size_t TotalFrames;

	ssize_t AnimIDSecondary;
	size_t FrameSecondary;
	size_t SecondaryCount;

	ssize_t AnimIDMouth;
	size_t FrameMouth;

	short Count;			// Total index of animations
	short PlayIndex;		// Current animation index we're upto
	short CurLoop;			// Current loop that we're upto.

	ssize_t TimeDiff;			// Difference in time between each frame

	float Speed;			// The speed of which to multiply the time given for Tick();
	float mouthSpeed;

public:
	AnimManager(ModelAnimation *anim);
	~AnimManager();

	void SetCount(int count);
	void AddAnim(unsigned int id, short loop); // Adds an animation to our array.
	void SetAnim(short index, unsigned int id, short loop); // sets one of the 4 existing animations and changes it (not really used currently)

	void SetSecondary(int id) {
		AnimIDSecondary = id;
		FrameSecondary = anims[id].timeStart;
	}
	void ClearSecondary() { AnimIDSecondary = -1; }
	ssize_t GetSecondaryID() { return AnimIDSecondary; }
	size_t GetSecondaryFrame() { return FrameSecondary; }
	void SetSecondaryCount(int count) {	SecondaryCount = count; }
	size_t GetSecondaryCount() { return SecondaryCount; }

	// For independent mouth movement.
	void SetMouth(int id) {
		AnimIDMouth = id;
		FrameMouth = anims[id].timeStart;
	}
	void ClearMouth() { AnimIDMouth = -1; }
	ssize_t GetMouthID() { return AnimIDMouth; }
	size_t GetMouthFrame() { return FrameMouth; }
	void SetMouthSpeed(float speed) {
		mouthSpeed = speed;
	}

	void Play(); // Players the animation, and reconfigures if nothing currently inputed
	void Stop(); // Stops and resets the animation
	void Pause(bool force = false); // Toggles 'Pause' of the animation, use force to pause the animation no matter what.

	void Next(); // Plays the 'next' animation or loop
	void Prev(); // Plays the 'previous' animation or loop

	int Tick(int time);

	size_t GetFrameCount();
	size_t GetFrame() {return Frame;}
	void SetFrame(size_t f);
	void SetSpeed(float speed) {Speed = speed;}
	float GetSpeed() {return Speed;}

	void PrevFrame();
	void NextFrame();

	void Clear();
	void Reset() { Count = 0; }

	bool IsPaused() { return Paused; }
	bool IsParticlePaused() { return !AnimParticles; }
	void AnimateParticles() { AnimParticles = true; }

	size_t GetAnim() { return animList[PlayIndex].AnimID; }

	ssize_t GetTimeDiff();
	void SetTimeDiff(ssize_t i);
};



#endif /* _ANIMNMANAGER_H_ */
