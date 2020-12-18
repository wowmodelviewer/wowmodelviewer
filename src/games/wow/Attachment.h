/*
 * Attachment.h
 *
 *  Created on: 26 oct. 2013
 *
 */

#ifndef _ATTACHMENT_H_
#define _ATTACHMENT_H_

#include <string>
#include <vector>

#include "glm/glm.hpp"

class Displayable;
class WoWModel;
class BaseCanvas;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _ATTACHMENT_API_ __declspec(dllexport)
#    else
#        define _ATTACHMENT_API_ __declspec(dllimport)
#    endif
#else
#    define _ATTACHMENT_API_
#endif

class _ATTACHMENT_API_ Attachment
{
  public:
    Attachment(Attachment *parent, Displayable *model, int id, int slot, float scale = 1.0f, glm::vec3 rot = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 pos = glm::vec3(0.0f, 0.0f, 0.0f), bool mirror = false);

    ~Attachment();

    void setup();
    void setupParticle();
    Attachment* addChild(std::string fn, int id, int slot, float scale = 1.0f, glm::vec3 rot = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 pos = glm::vec3(0.0f, 0.0f, 0.0f), bool mirror = false);
    Attachment* addChild(Displayable *disp, int id, int slot, float scale = 1.0f, glm::vec3 rot = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 pos = glm::vec3(0.0f, 0.0f, 0.0f), bool mirror = false);
    void delSlot(int slot);
    void delChildren();
    WoWModel* getModelFromSlot(int slot);

    void draw(BaseCanvas *c);
    void drawParticles(bool force=false);
    void tick(float dt);

    void setModel(Displayable * newmodel);
    Displayable * model() { return m_model;}

    Attachment *parent;

    std::vector<Attachment*> children;

    int id;
    int slot;
    float scale;
    glm::vec3 rot;
    glm::vec3 pos;
    bool mirror;

  private:
    Displayable *m_model;
};


#endif /* _ATTACHMENT_H_ */
