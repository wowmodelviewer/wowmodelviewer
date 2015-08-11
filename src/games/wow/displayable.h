#ifndef DISPLAYABLE_H
#define DISPLAYABLE_H

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _DISPLAYABLE_API_ __declspec(dllexport)
#    else
#        define _DISPLAYABLE_API_ __declspec(dllimport)
#    endif
#else
#    define _DISPLAYABLE_API_
#endif

class _DISPLAYABLE_API_ Displayable
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

