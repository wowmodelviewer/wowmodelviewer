/*
 * AnimManager.cpp
 *
 *  Created on: 19 oct. 2013
 *
 */
#include "AnimManager.h"
#include "WoWModel.h"
#include "logger/Logger.h"

#include "wow_enums.h"

#include <algorithm>
#include <chrono>

namespace
{
  double steadyMs()
  {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  unsigned s_frameSets = 0;
  double s_tickStartMs = 0.0;
}

void AnimManager::StartTick()
{
  s_tickStartMs = steadyMs();
}

AnimManager::AnimManager(WoWModel & m)
  : model(m)
{
  AnimIDSecondary = -1;
  SecondaryCount = UPPER_BODY_BONES;
  AnimIDMouth = -1;
  Count = 1;
  PlayIndex = 0;
  CurLoop = 0;
  animList[0].AnimID = 0;
  animList[0].Loops = 0;

  if (model.anims.size() > 0)
  {
    Frame = 0;
    TotalFrames = model.anims[0].length;
  }
  else
  {
    Frame = 0;
    TotalFrames = 0;
  }
  Speed = 1.0f;
  mouthSpeed = 1.0f;
  Paused = false;
  FrameTimeMs = steadyMs();
  FrameSet = true;
}

void AnimManager::MarkFrameSet()
{
  FrameTimeMs = steadyMs();
  FrameSet = true;
  s_frameSets++;
}

unsigned AnimManager::FrameSets()
{
  return s_frameSets;
}

AnimManager::~AnimManager()
{
}

void AnimManager::SetCount(int count)
{
  Count = count;
}

void AnimManager::AddAnim(unsigned int id, short loops)
{
  if (Count > 3)
    return;

  animList[Count].AnimID = id;
  animList[Count].Loops = loops;
  Count++;
}

void AnimManager::SetAnim(short index, unsigned int id, short loops)
{
  // error check, we currently only support 4 animations.
  if (index > 3)
    return;

  animList[index].AnimID = id;
  animList[index].Loops = loops;

  // Just an error check for our "auto animate"
  if (index == 0)
  {
    Count = 1;
    PlayIndex = index;
    Frame = 0;
    TotalFrames = model.anims[id].length;
    MarkFrameSet();
  }

  if (index+1 > Count)
    Count = index+1;
}

void AnimManager::Play()
{
  if (!Paused)
    return;
  Paused = false;
  TotalFrames = GetFrameCount();
  Paused = false;
}

void AnimManager::Stop()
{
  Paused = true;
  PlayIndex = 0;
  SetFrame(0);
  CurLoop = animList[0].Loops;
  TotalFrames = GetFrameCount();
}

void AnimManager::Pause(bool force)
{
  if (Paused && force == false)
    Paused = false;
  else
    Paused = true;
}

void AnimManager::Next()
{
  if(CurLoop == 1)
  {
    PlayIndex++;
    if (PlayIndex >= Count)
    {
      Stop();
      return;
    }
    CurLoop = animList[PlayIndex].Loops;
  }
  else if(CurLoop > 1)
  {
    CurLoop--;
  }
  else if(CurLoop == 0)
  {
    PlayIndex++;
    if (PlayIndex >= Count)
      PlayIndex = 0;
  }

  Frame = 0;
  TotalFrames = GetFrameCount();
}

void AnimManager::Prev()
{
  if(CurLoop >= animList[PlayIndex].Loops)
  {
    PlayIndex--;

    if (PlayIndex < 0)
    {
      Stop();
      return;
    }
    CurLoop = animList[PlayIndex].Loops;
  }
  else if(CurLoop < animList[PlayIndex].Loops)
  {
    CurLoop++;
  }

  Frame = model.anims[animList[PlayIndex].AnimID].length;
  TotalFrames = GetFrameCount();
}

int AnimManager::Tick(int time)
{
  if((Count < PlayIndex) )
    return -1;

  // The canvas passes the time since its previous tick, and a busy UI thread can hold the next tick back for a
  // while. When the frame was set outright in that wait, only the time since then belongs to this animation: a
  // mount choice loads the mount's model for a second or more and only then starts the mount and the character's
  // riding animation at frame 0, and the first tick after it put both that whole second into them -- the embedded
  // Unity viewport, which starts them when it is told, was then snapped forward by it at the next heartbeat.
  // Measured from where the canvas measured that time (StartTick), not from here: the tick sends the character's
  // scene before it advances the clocks, and the time that takes is in the next tick's count already.
  const double now = s_tickStartMs > 0.0 ? s_tickStartMs : steadyMs();
  int advance = time;
  if (FrameSet)
  {
    advance = std::min(time, std::max(0, (int)(now - FrameTimeMs)));
    FrameSet = false;
  }
  FrameTimeMs = now;

  Frame += int(advance*Speed);

  // animate our mouth animation
  if (AnimIDMouth > -1)
  {
    FrameMouth += (time*mouthSpeed);

    if (FrameMouth >= model.anims[AnimIDMouth].length)
      FrameMouth -= model.anims[AnimIDMouth].length;
  }

  // animate our second (upper body) animation
  if (AnimIDSecondary > -1)
  {
    FrameSecondary += (time*Speed);

    if (FrameSecondary >= model.anims[AnimIDSecondary].length)
      FrameSecondary -= model.anims[AnimIDSecondary].length;
  }

  if (Frame >= model.anims[animList[PlayIndex].AnimID].length)
  {
    // The time past the end belongs to what plays next, as for the mouth and upper-body clocks above.
    // Starting it at frame 0 dropped that time: a tick is normally a few ms past the end, but the first
    // tick after the UI thread was busy carries the whole wait, and a looping animation fell back by
    // up to that much at every such wrap -- the embedded Unity viewport, whose own clock wraps without
    // losing time, was then snapped back to it at the next heartbeat. Not when Next() ends the queue
    // (its Stop() holds frame 0); a rider's manager, stopped by the mount choice while the tick still
    // runs it, loops on and carries the time like any other.
    const size_t over = Frame - model.anims[animList[PlayIndex].AnimID].length;
    const bool queueEnds = CurLoop == 1 && PlayIndex + 1 >= Count;
    Next();
    const size_t length = GetFrameCount();
    if (!queueEnds && length > 0)
      Frame = over % length;
    return 1;
  }

  return 0;
}

size_t AnimManager::GetFrameCount()
{
  return model.anims[animList[PlayIndex].AnimID].length;
}

size_t AnimManager::GetFrameNow(bool running)
{
  // Frame moves only when the canvas ticks. Read while the UI thread holds the ticks back -- the mount choice sends
  // the character's playback while the mount's model is still loading -- it is the frame of the last tick, and a
  // viewport told so as the current position jumped back by the wait. It is carried on by the time since that tick,
  // as the next tick will carry it. A frame set outright since then is current as set: the next tick counts from
  // that moment (Tick), and the first tick after a load counts nothing at all.
  if (!running || FrameSet || Speed <= 0.0f)
    return Frame;
  const size_t frame = Frame + (size_t)(std::max(0.0, steadyMs() - FrameTimeMs) * Speed);
  const size_t length = GetFrameCount();
  return length > 0 ? frame % length : frame;
}


void AnimManager::NextFrame()  // Only called by the animation controls
{
  ssize_t TimeDiff;
  ssize_t id = animList[PlayIndex].AnimID;
  TimeDiff = (model.anims[id].length / 60);
  Frame += TimeDiff;
  ForceModelUpdate(TimeDiff);
}

void AnimManager::PrevFrame()  // Only called by the animation controls
{
  ssize_t TimeDiff;
  ssize_t id = animList[PlayIndex].AnimID;
  TimeDiff = (model.anims[id].length / 60) * -1;
  Frame += TimeDiff;
  ForceModelUpdate(TimeDiff);
}

void AnimManager::SetFrame(size_t f)  // Only called by the animation slider, or if Stop is called
{
  ssize_t TimeDiff;
  TimeDiff = f - Frame;
  ssize_t id = animList[PlayIndex].AnimID;

  // ideal frame interval:
  int frameInterval = model.anims[id].length / 60;

  uint i = 1;

  // Update the model approx. ideal frame intervals, or rendering may not be smooth if slider is moved quickly...
  if (frameInterval != 0)
    i = round(abs(TimeDiff / frameInterval));  // Number of chunks to break it up into

  if (i > 1)
  {
    for (uint j = 0; j < i; j++)
    {
      ForceModelUpdate((float)TimeDiff / i);
    }
  }
  else
  {
    ForceModelUpdate(TimeDiff);
  }

  Frame = f;
  MarkFrameSet();
}

void AnimManager::Clear()
{
  Stop();
  Paused = true;
  PlayIndex = 0;
  Count = 0;
  CurLoop = 0;
  Frame = 0;
}

void AnimManager::ForceModelUpdate(float dt)
{
  // Advance the model and recompute its pose for the new frame. This used to draw the model and its
  // particles as well, into whatever GL context was current, on every frame step and scrub -- the
  // OpenGL viewport's way of getting the pose computed. That viewport is archived and nothing reads
  // those pixels; the pose is what a frame step needs, and updatePose computes it without drawing.
  // A forced step moves the frame by exactly dt: it is not a canvas tick, so a frame set just before it is not
  // held back (Tick), and is still held back at the next canvas tick.
  const bool frameSet = FrameSet;
  FrameSet = false;
  model.update(dt);
  model.updatePose();
  FrameSet = frameSet;
}

