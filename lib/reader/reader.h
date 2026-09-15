#ifndef READER_H
#define READER_H

#include "module.h"

/*
 * @brief Protocol specific functions above the bus
 *
 * Handles the protocol specific functionality of each one of the protocols
 * utilised throughout different modules
 */
class Reader {
 protected:
  Module *module = nullptr;

 public:
  virtual ~Reader() = default;
  // Config is passed to the concrete reader's constructor, not here.
  virtual int init(Module &module) = 0;
  virtual int get_import(float *val) = 0;
  virtual int get_export(float *val) = 0;
  virtual int get_voltage(float *val) = 0;
};

#endif
