#ifndef METER_H
#define METER_H

class Meter {
 public:
  virtual ~Meter() = default;

  virtual float readVoltage() = 0;
  virtual float readImportEnergy() = 0;
  virtual float readExportEnergy() = 0;
};

#endif
