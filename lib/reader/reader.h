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
  // Initialiser, takes in a config pointer (can be any specified config from the node_config.h)
  virtual int init(Module &module, const void *config) = 0;
  virtual int get_import(float *val) = 0;
  virtual int get_export(float *val) = 0;
  virtual int get_voltage(float *val) = 0;
};

#endif
