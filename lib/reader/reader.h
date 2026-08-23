#ifndef READER_H
#define READER_H

#include "module.h"

class Reader {
 protected:
  Module *module = nullptr;

 public:
  virtual ~Reader() = default;

  virtual int get_import(float *val) = 0;
  virtual int get_export(float *val) = 0;
  virtual int get_voltage(float *val) = 0;
};

#endif
