#pragma once

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _MODEL_API_ __declspec(dllexport)
#    else
#        define _MODEL_API_ __declspec(dllimport)
#    endif
#else
#    define _MODEL_API_
#endif

class _MODEL_API_ Model
{
public:
	Model()
	{
	};

	virtual ~Model() = 0;
};
