/*
 * BaseCanvas.h
 *
 *  Created on: 8 Aug. 2015
 *      Author: Jeromnimo
 */

#ifndef _BASECANVAS_H_
#define _BASECANVAS_H_

#include "WoWModel.h"

class BaseCanvas
{
  public:
    BaseCanvas(): m_p_model(0) {}

    WoWModel const * model() const { return m_p_model; }
    void setModel(WoWModel * m) 
    {
      delete m_p_model;
      m_p_model = m;
    }

	private:
		WoWModel *m_p_model;
};



#endif /* _BASECANVAS_H_ */
