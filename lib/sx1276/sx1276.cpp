#include "sx1276.h"

// TODO: implement over the sandeepmistry LoRa driver.
//
// setup()     = SPI.begin(SCK, MISO, MOSI, SS);
//               LoRa.setPins(SS, RST, DIO0);
//               LoRa.begin(band); then setSignalBandwidth /
//               setSpreadingFactor / setSyncWord / setTxPower.
//
// send()      = LoRa.beginPacket(); LoRa.write(data, len); LoRa.endPacket();
// available() = LoRa.parsePacket() > 0;
// receive()   = LoRa.readBytes(buf, min(maxLen, LoRa.available()));
