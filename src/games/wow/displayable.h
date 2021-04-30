#ifndef DISPLAYABLE_H
#define DISPLAYABLE_H


class Attachment;

class Displayable
{
public:
  virtual ~Displayable() {};

  virtual void setupAtt(int) {};
  virtual void setupAtt2(int) {};
  virtual void draw() {};
  virtual void reset() {};
  virtual void update(int) {};
  Attachment * attachment;
};

#endif

