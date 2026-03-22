#pragma once

#define _DISPLAYABLE_API_

class Attachment;

/// @brief Interface for objects that can be drawn and animated in the scene.
///
/// Provides virtual hooks for setup, drawing, resetting, and per-frame updates.
class _DISPLAYABLE_API_ Displayable
{
public:
	virtual ~Displayable()
	{
	};

	virtual void setupAtt(int)
	{
	};

	virtual void setupAtt2(int)
	{
	};

	virtual void draw()
	{
	};

	virtual void reset()
	{
	};

	virtual void update(int)
	{
	};
	Attachment* attachment;
};
