/*
 * LogStackWalker.h
 *
 *  Created on: 30 dec. 2015
 *      Author: Jeromnimo
 */

#ifndef _LOGSTACKWALKER_H_
#define _LOGSTACKWALKER_H_

#ifndef WX_PRECOMP
  #include <wx/wx.h>
#endif

#include <wx/stackwalk.h>

class LogStackWalker : public wxStackWalker
{
  protected:
    void OnStackFrame(const wxStackFrame& frame);
};




#endif /* _LOGSTACKWALKER_H_ */
