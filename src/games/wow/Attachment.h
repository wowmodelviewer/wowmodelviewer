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

class Displayable;


class Attachment
{
  public:
    Attachment(Attachment *parent, Displayable *model, int id, int slot);

    ~Attachment();

    void setup();
    void setupParticle();
    Attachment* addChild(std::string fn, int id, int slot);
    Attachment* addChild(Displayable *disp, int id, int slot);
    void delSlot(int slot);
    void delChildren();

    void draw();
    void drawParticles();
    void tick(float dt);

    void setModel(Displayable * newmodel);
    Displayable * model() const { return model_;}

    Attachment *parent;

    std::vector<Attachment*> children;

    int id;
    int slot;

  private:
    Displayable *model_;
};


#endif /* _ATTACHMENT_H_ */
