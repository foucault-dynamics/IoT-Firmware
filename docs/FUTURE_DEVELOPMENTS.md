# Future developments

## Hardware resolution:
As of now the type of end node is resolved by serial input, In the real board the voltage difference between GPIO3 and GPIO4 determines which end node head is connected to the ESP32-C3. Substation and Gateway have no actual way of knowing which node it is.

## Sleep cycle:
As of now there is no deep sleep cycle, this was to make it easier to test boards without having to wait 5 minutes between readings. An effective setup for deep sleeping and wake sources (internal or external) should be implemented

## End node time stamping:
Right now the gateway timestamps packets when they receive them. There is a factor of delay that comes with a reading going from end node through wifi and LoRa so timestamps are not accurate on the meter side.  There should be a more effective system for time stamping

## Meter Resolution RS485:
As of now RS485 has only applied 2 protocols: DLMS/COSEM and ModbusRTU. However there is no fitting for other protocols, or knowing what protocol the connected meter is using. Additionally, even if a RS485 node knows the protocol a meter is using, each meter make would have different register addresses and value encodings in the case of Modbus, or different OBIS codes, HDLC addressing (client SAP, server logical and physical address, address length) and security settings (authentication and ciphering, which the reader currently assumes are off), for DLMS/COSEM.

## Encryption:
As of now there is no method of encryption implemented in any of the links between any node except for TLS in the publishing to MQTT Brokers.

## End node ID:
Currently the ESP Efuse unique id is used for Identifying end-nodes, and community code is hardcoded. This can be kept but a lot of ESP32 chips don’t have this fused, so there should be a system of assigning IDs to end_nodes and communities.

## Confidentiality, Integrity, Availability in LoRa and ESP-NOW links:
As of now there is very basic application of Integrity checks and Availability checks. More exhaustive implementation for this should be applied in all links: ESP-NOW from end-nodes to substation, LoRa between substation and gateway and MQTT publishing

## MQTT Broker:
Currently the project is using a public MQTT broker which anyone can connect to, ideally there should be a private server that would act as an MQTT broker or the use of a service for this.



