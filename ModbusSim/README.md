# Modbus Simulation scripts

## Description

Includes python scripts that simulate a server (meter) using the Modbus protocol. `ModbusSimTCP.py` simulates RTU framing over TCP, `ModbusSimSerial.py` simulates RTU framing over an actual serial port (RS485).

## Setup

1. In your terminal run `python -m venv .venv`  or  `python3 -m venv .venv`
2. run `source .venv/bin/activate`
3. Install pymodbus `python3 -m pip install pymodbus`

## Testing ModbusSimTCP.py

1. Run `python3 ModbusSimTCP.py`, server listens on `0.0.0.0:5020`
2. From another terminal (venv activated, pymodbus installed), poll it with the pymodbus console client:
   `python3 -m pymodbus.console tcp --host 127.0.0.1 --port 5020`
3. In the console, read voltage (float32 across 2 regs at addr 0):
   `client.read_holding_registers address=0 count=2 slave=1`
4. Read kWh export/import counters (uint32 across 2 regs) at addr 2 and 4 the same way. Values update each poll: voltage oscillates, counters increment.

## Testing ModbusSimSerial.py

1. Requires two ends of a serial link, either a real RS485/USB-serial adapter pair or a virtual null-modem pair (e.g. `socat` on mac/Linux: `socat -d -d pty,raw,echo=0 pty,raw,echo=0` gives you two linked `/dev/ttys00X` paths).
2. Edit `PORT` in `ModbusSimSerial.py` to match the server's end of the link.
3. Run `python3 ModbusSimSerial.py`
4. From another terminal, poll with the pymodbus console client against the other end of the link:
   `python3 -m pymodbus.console serial --port /dev/ttyXXXX --baudrate 9600 --framer rtu`
5. Read registers same as TCP test above (`client.read_holding_registers address=0 count=2 slave=1`, etc).


