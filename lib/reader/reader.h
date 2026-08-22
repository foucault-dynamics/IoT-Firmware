#ifndef READER_H
#define READER_H

#include "module.h"

class Reader {
 protected:
  Module *module = nullptr;

 public:
  virtual ~Reader() = default;

  virtual float get_import() = 0;
  virtual float get_export() = 0;
  virtual float get_voltage() = 0;
};

#endif
