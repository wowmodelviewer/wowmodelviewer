#pragma once

#define _DISPLAYABLE_API_

class Attachment;

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
