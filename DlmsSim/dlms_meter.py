"""
Fake DLMS/COSEM meter. The protocol logic shared by DlmsSimTCP.py and
DlmsSimSerial.py: HDLC bytes in from the reader, HDLC bytes out from the "meter".

The DLMS side is Gurux.DLMS.Python's GXDLMSServer, an implementation written
independently of the firmware, so a session that works against it shows the
firmware interoperates rather than agreeing with its own test vectors.

It answers what DlmsCosemReader asks for, with the defaults rs485_loader.cpp
loads from NVS:

  link     HDLC, server logical 1, 1 byte server address, client SAP 16
  context  LN referencing, no authentication, no ciphering
  objects  three Registers

  OBIS             value                        type                 scaler  unit
  1.0.1.8.0.255    import, +100 Wh per read     double-long-unsigned 0       Wh (30)
  1.0.2.8.0.255    export, +100 Wh per read     double-long-unsigned 0       Wh (30)
  1.0.32.7.0.255   230 + 5 sin(t/10) V          long-unsigned        -1      V (35)

Each firmware getter is one whole session: SNRM, AARQ, GET scaler_unit,
GET value, DISC. A value changes on every GET of it, like ModbusSim's, so a
stale or cached reading in the firmware is obvious.

Written against gurux-dlms 1.0.203, whose server side has bugs this file works
around. Each workaround says which bug it covers, so it can be dropped once
Gurux fixes it.
"""
import logging
import math
import time

from gurux_dlms import GXDLMSServer, GXServerReply
from gurux_dlms.ConnectionState import ConnectionState
from gurux_dlms.GXDLMS import GXDLMS
from gurux_dlms.GXHdlcSettings import GXHdlcSettings
from gurux_dlms._HDLCInfo import _HDLCInfo
from gurux_dlms.enums import AccessMode, Authentication, DataType, InterfaceType, SourceDiagnostic, Unit
from gurux_dlms.objects import GXDLMSAssociationLogicalName, GXDLMSHdlcSetup, GXDLMSRegister

_logger = logging.getLogger(__name__)

CLIENT_SAP = 16      # public client, the dlms_client default
SERVER_LOGICAL = 1   # management logical device, the dlms_logical default

IMPORT_OBIS = "1.0.1.8.0.255"
EXPORT_OBIS = "1.0.2.8.0.255"
VOLTAGE_OBIS = "1.0.32.7.0.255"

IMPORT_START_WH = 100000
EXPORT_START_WH = 1000000
WH_STEP = 100  # Wh added to a counter per read of its value


def voltage_decivolts() -> int:
    """Same sine as ModbusSim, sent as tenths of a volt so the scaler is exercised."""
    return round(10 * (230.0 + 5.0 * math.sin(time.time() / 10)))


def _register(obis, data_type, scaler, unit, value):
    reg = GXDLMSRegister(obis)
    reg.setDataType(2, data_type)
    # Gurux keeps the scaler as the multiplier, 10^scaler, and sends log10 of it.
    reg.scaler = 10 ** scaler
    reg.unit = unit
    reg.value = value
    return reg


class _ServerReply(GXServerReply):
    """
    GXDLMSServer.handleRequest() calls setters that GXServerReply doesn't
    define, so a stock GXServerReply crashes on the first frame.
    """

    def setReply(self, value):
        self.reply = value

    def setCount(self, value):
        self.count = value

    def getConnectionInfo(self):
        return self.connectionInfo


class SimMeter(GXDLMSServer):
    def __init__(self):
        super().__init__(True, InterfaceType.HDLC)

        # The meter's HDLC limits, the IEC HDLC setup object. The defaults are
        # 128 byte info field and window 1 both ways, the same as an SNRM with
        # no parameters, which is what the firmware sends.
        self.hdlc = GXDLMSHdlcSetup()
        # handleRequest() measures inactivity with int() of a timedelta, which
        # raises. 0 turns that check off. The sim has no need to drop idle links.
        self.hdlc.inactivityTimeout = 0

        self.import_reg = _register(IMPORT_OBIS, DataType.UINT32, 0, Unit.ACTIVE_ENERGY, IMPORT_START_WH)
        self.export_reg = _register(EXPORT_OBIS, DataType.UINT32, 0, Unit.ACTIVE_ENERGY, EXPORT_START_WH)
        self.voltage_reg = _register(VOLTAGE_OBIS, DataType.UINT16, -1, Unit.VOLTAGE, voltage_decivolts())

        # initialize() builds a default association by appending the whole
        # object collection as if it were one object, and raises. Supplying
        # our own association, its object list already filled, skips that.
        association = GXDLMSAssociationLogicalName()
        for obj in (self.import_reg, self.export_reg, self.voltage_reg, association):
            self.items.append(obj)
            association.objectList.append(obj)
        self.initialize()

    def _advance(self, reg):
        if reg is self.import_reg or reg is self.export_reg:
            reg.value += WH_STEP
        elif reg is self.voltage_reg:
            reg.value = voltage_decivolts()

    def _append_agreed_parameters(self):
        """HDLC parameter negotiation group with the agreed values, the body of a UA."""
        agreed = self.settings.hdlc
        self.replyData.setUInt8(0x81)  # format identifier
        self.replyData.setUInt8(0x80)  # group identifier
        self.replyData.setUInt8(0)     # group length, filled in below
        self.replyData.setUInt8(_HDLCInfo.MAX_INFO_TX)
        GXDLMS.appendHdlcParameter(self.replyData, agreed.maxInfoTX)
        self.replyData.setUInt8(_HDLCInfo.MAX_INFO_RX)
        GXDLMS.appendHdlcParameter(self.replyData, agreed.maxInfoRX)
        self.replyData.setUInt8(_HDLCInfo.WINDOW_SIZE_TX)
        self.replyData.setUInt8(4)
        self.replyData.setUInt32(agreed.windowSizeTX)
        self.replyData.setUInt8(_HDLCInfo.WINDOW_SIZE_RX)
        self.replyData.setUInt8(4)
        self.replyData.setUInt32(agreed.windowSizeRX)
        # setUInt8 takes (value, index). Gurux passes them the other way round,
        # writing a 2 over the window size and leaving the length 0.
        self.replyData.setUInt8(len(self.replyData) - 3, 2)

    def handleSnrmRequest(self, data):
        """
        Gurux's version reads the agreed values off self.hdlc, the limits
        object, instead of self.settings.hdlc, and calls an update() that
        GXHdlcSettings doesn't have. Same steps, right objects.
        """
        agreed = self.settings.hdlc
        agreed.maxInfoTX = GXHdlcSettings.DEFAULT_MAX_INFO_TX
        agreed.maxInfoRX = GXHdlcSettings.DEFAULT_MAX_INFO_RX
        agreed.windowSizeTX = GXHdlcSettings.DEFAULT_WINDOWS_SIZE_TX
        agreed.windowSizeRX = GXHdlcSettings.DEFAULT_WINDOWS_SIZE_RX
        GXDLMS.parseSnrmUaResponse(data, agreed)
        self.reset(True)

        # Never agree to more than the meter's limits. What the client
        # transmits, the meter receives, and the other way round.
        agreed.maxInfoTX = min(agreed.maxInfoTX, self.hdlc.maximumInfoLengthReceive)
        agreed.maxInfoRX = min(agreed.maxInfoRX, self.hdlc.maximumInfoLengthTransmit)
        agreed.windowSizeTX = min(agreed.windowSizeTX, self.hdlc.windowSizeReceive)
        agreed.windowSizeRX = min(agreed.windowSizeRX, self.hdlc.windowSizeTransmit)

        self._append_agreed_parameters()
        self.settings.connected = ConnectionState.HDLC

    def generateDisconnectRequest(self):
        """Gurux's version has the same self.hdlc mix-up as handleSnrmRequest()."""
        self._append_agreed_parameters()

    def isTarget(self, serverAddress, clientAddress):
        # A real meter stays silent for a frame that isn't addressed to it,
        # so a wrong address in NVS shows up as a timeout, same as here.
        if serverAddress == SERVER_LOGICAL and clientAddress == CLIENT_SAP:
            return True
        _logger.warning("ignored frame for server %d from client %d (expect server %d, client %d)",
                        serverAddress, clientAddress, SERVER_LOGICAL, CLIENT_SAP)
        # Forget the addresses Gurux just latched, so the next frame is checked again.
        self.reset()
        return False

    def onValidateAuthentication(self, authentication, password):
        if authentication == Authentication.NONE:
            return SourceDiagnostic.NONE
        _logger.warning("refused association with authentication %s", authentication)
        return SourceDiagnostic.NOT_RECOGNISED

    def onGetAttributeAccess(self, arg):
        # Gurux calls this once per attribute of every GET, before reading it,
        # which makes it the one hook that sees each read of a value.
        if arg.index == 2:
            self._advance(arg.target)
        return AccessMode.READ

    def onGetMethodAccess(self, arg):
        return 0

    def onConnected(self, connectionInfo):
        _logger.info("associated")

    def onDisconnected(self, connectionInfo):
        _logger.info("released")

    def onInvalidConnection(self, connectionInfo):
        _logger.warning("association refused")

    # GET normal calls notifyRead(), which GXDLMSServer doesn't define, and
    # onPostRead() with no arguments.
    def notifyRead(self):
        pass

    def onPostRead(self, args=None):
        pass

    def onPreRead(self, args):
        pass

    def onPreGet(self, args):
        pass

    def onPostGet(self, args):
        pass

    def onFindObject(self, objectType, sn, ln):
        return None

    def onPreWrite(self, args):
        pass

    def onPostWrite(self, args):
        pass

    def onPreAction(self, args):
        pass

    def onPostAction(self, args):
        pass

    def feed(self, data: bytes) -> bytes | None:
        """
        Takes bytes as they arrive, any split. Returns the reply once a whole
        frame is in, or None while Gurux is still buffering a partial one.
        """
        _logger.debug("<- %s", data.hex(" "))
        sr = _ServerReply(data)
        self.handleRequest(sr)
        if not sr.reply:
            return None
        reply = bytes(sr.reply)
        _logger.debug("-> %s", reply.hex(" "))
        return reply
