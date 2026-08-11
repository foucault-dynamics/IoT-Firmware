#ifndef MODULE_H
#define MODULE_H

class Module{

 public:

  volatile bool active;  
  int setup();
  virtual int receive();
  virtual int send();
  
};



#endif
