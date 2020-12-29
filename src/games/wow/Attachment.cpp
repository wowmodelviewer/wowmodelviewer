/*
 * Attachment.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "Attachment.h"

#include <string>
#include "GL/glew.h"

#include "displayable.h"
#include "Game.h"
#include "video.h"
#include "WoWModel.h"

#include "logger/Logger.h"



Attachment::Attachment(Attachment *parent, Displayable *model, int id, int slot)
  : parent(parent), id(id), slot(slot), model_(nullptr)
{
  setModel(model);
}

Attachment::~Attachment()
{
  delChildren();

  parent = nullptr;

  if (model_)
    model_->attachment = nullptr;

}

void Attachment::draw()
{
  glPushMatrix();
  if (model_)
  {
    setup();
    model_->draw();
  }

  // children
  for (auto& c : children)
    c->draw();

  glPopMatrix();
}

void Attachment::drawParticles()
{
  glPushMatrix();

  if (model_)
  {
    auto *m = dynamic_cast<WoWModel*>(model_);
    if (!m)
    {
      glPopMatrix();
      return;
    }

    model_->reset();
    setupParticle();

    m->drawParticles();
  }

  // children:
  for (size_t i = 0; i < children.size(); i++)
    children[i]->drawParticles();

  glPopMatrix();
}



void Attachment::tick(float dt)
{
  if (model_)
    model_->update(dt);
  for (size_t i = 0; i < children.size(); i++)
    children[i]->tick(dt);
}

void Attachment::setup()
{
  if (parent == nullptr)
    return;
  if (parent->model_)
    parent->model_->setupAtt(id);
}

void Attachment::setupParticle()
{
  if (parent == nullptr)
    return;
  if (parent->model_)
    parent->model_->setupAtt2(id);
}

Attachment* Attachment::addChild(std::string modelfn, int id, int slot)
{
  if (modelfn.length() == 0 || id < 0)
    return nullptr;

  auto *m = new WoWModel(GAMEDIRECTORY.getFile(modelfn.c_str()), true);

  if (m->ok)
    return addChild(m, id, slot);
 
  delete m;
  return nullptr;
}

Attachment* Attachment::addChild(Displayable *disp, int id, int slot)
{
  LOG_INFO << "Attach on id" << id << "slot" << slot;
  auto *att = new Attachment(this, disp, id, slot);
  children.push_back(att);
  return att;
}

void Attachment::delChildren()
{
  for (size_t i = 0; i < children.size(); i++)
  {
    children[i]->delChildren();
    delete children[i];
    children[i] = nullptr;
  }

  children.clear();
}

void Attachment::delSlot(int slot)
{
  for (size_t i = 0; i < children.size(); i++)
  {
    if (children[i]->slot == slot)
      children.erase(children.begin() + i);
  }
}

void Attachment::setModel(Displayable * newmodel)
{
  if (model_)
    model_->attachment = nullptr;

  model_ = newmodel;

  if (model_)
    model_->attachment = this;
}

