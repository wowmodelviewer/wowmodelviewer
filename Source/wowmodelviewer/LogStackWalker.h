#pragma once

#ifndef WX_PRECOMP
// ReSharper disable once CppUnusedIncludeDirective
#include <wx/wx.h>
#endif

#include <wx/stackwalk.h>

class LogStackWalker : public wxStackWalker
{
protected:
	void OnStackFrame(const wxStackFrame& frame);
};
