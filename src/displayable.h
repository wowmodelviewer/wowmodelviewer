#ifndef DISPLAYABLE_H
#define DISPLAYABLE_H

#if defined(_WINDOWS) && !defined(_MINGW)
    #pragma warning( disable : 4100 )
#endif

class Displayable
{
public:
	virtual ~Displayable() {};

	virtual void setupAtt(int) {};
	virtual void setupAtt2(int) {};
	virtual void draw() {};
	virtual void reset() {};
	virtual void update(int) {};
};

#endif

