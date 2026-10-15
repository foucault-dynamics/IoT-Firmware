/**
 * @file
 * DlmsCosemReader, the Reader for DLMS/COSEM meters over HDLC.
 */

#ifndef DLMS_COSEM_H
#define DLMS_COSEM_H

#include "hdlc.h"
#include "module.h"
#include "reader.h"
#include "rs485_config.h"

/** How long to wait for a complete frame, in ms. */
#define DLMS_TIMEOUT 1000
/** Length of the LLC header in front of every APDU. */
#define DLMS_LLC_LEN 3
/** Largest APDU that fits one frame after the LLC header. */
#define DLMS_APDU_MAX (HDLC_INFO_MAX - DLMS_LLC_LEN)
/** COSEM class id of Register. */
#define DLMS_CLASS_REGISTER 3
/** Register attribute holding the value. */
#define DLMS_ATTR_VALUE 2
/** Register attribute holding the scaler and unit. */
#define DLMS_ATTR_SCALER_UNIT 3
/** Length of a get-request-normal APDU. */
#define DLMS_GET_REQUEST_LEN 13
/** Length of a get-response-normal header: tag, type, invoke id, choice. */
#define DLMS_GET_RESPONSE_HEADER_LEN 4
/** COSEM unit code for active energy, Wh. */
#define DLMS_UNIT_WH 30
/** COSEM unit code for voltage, V. */
#define DLMS_UNIT_V 35

/**
 * DLMS/COSEM reader for meters on RS485, framed with HDLC.
 *
 * Connects as a client with no authentication or ciphering, and reads Register
 * objects by their OBIS code.
 */
class DlmsCosemReader : public Reader {
 public:
  /**
   * Stores the config. Nothing is sent until a getter is called.
   *
   * @param[in] config  Addresses, OBIS codes and bus settings, copied.
   */
  DlmsCosemReader(const DlmsCosemConfig &config);

  /**
   * Attaches the bus and encodes the client and server HDLC addresses.
   *
   * @param[in] module  Bus to poll the meter over.
   * @retval EXIT_SUCCESS  Ready to read.
   * @retval EXIT_FAILURE  An address did not fit its size, or the server
   *                       address size was not 1, 2 or 4.
   */
  int init(Module &module) override;

  int get_import(float *val) override;
  int get_export(float *val) override;
  int get_voltage(float *val) override;

 private:
#ifdef PIO_UNIT_TESTING
  friend class DlmsCosemReaderTest;
#endif

  DlmsCosemConfig config;  ///< Addresses, OBIS codes and bus settings.
  HdlcAddress client{};    ///< Encoded client SAP, set by init().
  HdlcAddress server{};    ///< Encoded server address, set by init().
  uint8_t vs = 0;          ///< V(S), N(S) of the next I-frame sent, mod 8.
  uint8_t vr = 0;          ///< V(R), N(S) expected in the next I-frame received, mod 8.

  /**
   * Opens the HDLC link with SNRM and resets both sequence counters.
   *
   * The counters are only reset once the meter answers UA, since a refused
   * SNRM changes nothing on the meter.
   *
   * @retval EXIT_SUCCESS  The link is up.
   * @retval EXIT_FAILURE  The meter answered DM, something else, or nothing.
   */
  int connect();

  /**
   * Closes the HDLC link with DISC.
   *
   * @retval EXIT_SUCCESS  The meter answered UA, or DM because it was already
   *                       disconnected.
   * @retval EXIT_FAILURE  The send failed, or the meter answered something
   *                       else or nothing.
   */
  int disconnect();

  /**
   * Builds a frame from this client to the server and sends it.
   *
   * @param[in] control  Control byte.
   * @param[in] info     Info field, nullptr when @p infoLen is 0.
   * @param[in] infoLen  Length of @p info.
   * @retval EXIT_SUCCESS  The frame was sent.
   * @retval EXIT_FAILURE  The frame could not be built or sent.
   */
  int sendFrame(uint8_t control, const uint8_t *info, size_t infoLen);

  /**
   * Reads one frame, checks it, and checks it is from the server to this
   * client.
   *
   * @param[out] buf    At least HDLC_FRAME_MAX bytes, holds the raw frame.
   * @param[out] frame  Parsed frame, its info pointing into @p buf.
   * @retval EXIT_SUCCESS  @p frame holds a valid frame for this client.
   * @retval EXIT_FAILURE  Timed out, malformed, or addressed elsewhere.
   */
  int receive(uint8_t *buf, HdlcFrame *frame);

  /**
   * Sends one APDU in an I-frame and reads the APDU of the reply.
   *
   * Adds the LLC header E6 E6 00 going out. The reply must be a single
   * I-frame with the final bit set, N(S) equal to V(R), N(R) acknowledging
   * the frame just sent, and the LLC header E6 E7 00. Only then do V(S) and
   * V(R) advance.
   *
   * @param[in]  apdu     APDU to send, without LLC.
   * @param[in]  apduLen  Length of @p apdu, at most DLMS_APDU_MAX.
   * @param[out] resp     At least DLMS_APDU_MAX bytes. The reply's APDU,
   *                      without LLC.
   * @param[out] respLen  Length of @p resp.
   * @retval EXIT_SUCCESS  @p resp holds the reply.
   * @retval EXIT_FAILURE  @p apdu was too long, or the reply was missing,
   *                       out of sequence, segmented or not a DLMS response.
   */
  int exchange(const uint8_t *apdu, size_t apduLen, uint8_t *resp, size_t *respLen);

  /**
   * Builds the AARQ APDU: Logical Name referencing, no ciphering, no
   * authentication, only GET proposed, and DLMS_APDU_MAX as the largest PDU
   * this client receives.
   *
   * @param[out] out  At least DLMS_APDU_MAX bytes.
   * @return Length of the AARQ written to @p out.
   */
  size_t buildAarq(uint8_t *out);

  /**
   * Checks an AARE APDU. Walks its BER fields with every length bounds
   * checked, and logs the result source diagnostic when the association was
   * rejected.
   *
   * @param[in] apdu  AARE APDU, without LLC.
   * @param[in] len   Length of @p apdu.
   * @retval EXIT_SUCCESS  Association accepted with an xDLMS InitiateResponse.
   * @retval EXIT_FAILURE  Malformed, rejected, or the xDLMS initiate failed.
   */
  int checkAare(const uint8_t *apdu, size_t len);

  /**
   * Builds a get-request-normal for one attribute of a Register, with invoke
   * id 1, high priority, confirmed, and no selective access.
   *
   * @param[in]  obis       OBIS code, 6 bytes, A to F.
   * @param[in]  attribute  DLMS_ATTR_VALUE or DLMS_ATTR_SCALER_UNIT.
   * @param[out] out        At least DLMS_GET_REQUEST_LEN bytes.
   */
  void buildGetRequest(const uint8_t *obis, uint8_t attribute, uint8_t *out);

  /**
   * Checks a get-response-normal and finds the Data it carries. Logs the
   * data-access-result when the meter could not serve the attribute.
   *
   * @param[in]  apdu     GET response APDU, without LLC.
   * @param[in]  len      Length of @p apdu.
   * @param[out] data     Points into @p apdu at the Data's type tag.
   * @param[out] dataLen  Length of the Data, tag included.
   * @retval EXIT_SUCCESS  @p data holds at least one byte of Data.
   * @retval EXIT_FAILURE  Malformed, a block transfer, the wrong invoke id,
   *                       or a data-access-result instead of Data.
   */
  int parseGetResponse(const uint8_t *apdu, size_t len, const uint8_t **data, size_t *dataLen);

  /**
   * Decodes an A-XDR integer or float32 Data item into a double, which
   * holds every 32 bit integer exactly.
   *
   * @param[in]  data  Data, starting at its type tag.
   * @param[in]  len   Length of @p data.
   * @param[out] val   Decoded value.
   * @retval EXIT_SUCCESS  @p val holds the value.
   * @retval EXIT_FAILURE  The tag is not a number type, or the value is cut
   *                       short.
   */
  int decodeNumber(const uint8_t *data, size_t len, double *val);

  /**
   * Decodes a Register's scaler_unit, the structure 02 02 0F scaler 16 unit,
   * and checks its unit is the one the getter expects.
   *
   * @param[in]  data    Data, starting at its type tag.
   * @param[in]  len     Length of @p data.
   * @param[in]  unit    Expected unit, DLMS_UNIT_WH or DLMS_UNIT_V.
   * @param[out] scaler  Power of ten the value is multiplied by.
   * @retval EXIT_SUCCESS  @p scaler holds the scaler.
   * @retval EXIT_FAILURE  Not a scaler_unit structure, or the wrong unit.
   */
  int decodeScalerUnit(const uint8_t *data, size_t len, uint8_t unit, int8_t *scaler);

  /**
   * Sends the AARQ and checks the AARE that comes back.
   *
   * @retval EXIT_SUCCESS  The association is open.
   * @retval EXIT_FAILURE  The exchange failed or the meter refused.
   */
  int associate();

  /**
   * GETs one attribute of a Register and finds the Data in the response.
   *
   * @param[in]  obis       OBIS code, 6 bytes.
   * @param[in]  attribute  DLMS_ATTR_VALUE or DLMS_ATTR_SCALER_UNIT.
   * @param[out] resp       At least DLMS_APDU_MAX bytes, holds the response.
   * @param[out] data       Points into @p resp at the Data.
   * @param[out] dataLen    Length of the Data.
   * @retval EXIT_SUCCESS  @p data holds the attribute.
   * @retval EXIT_FAILURE  The exchange failed or the meter could not serve it.
   */
  int getAttribute(const uint8_t *obis, uint8_t attribute, uint8_t *resp,
                   const uint8_t **data, size_t *dataLen);

  /**
   * Reads a Register's scaled value on a link that is already up: associates,
   * then GETs scaler_unit and value.
   *
   * @param[in]  obis  OBIS code, 6 bytes.
   * @param[in]  unit  Expected unit, DLMS_UNIT_WH or DLMS_UNIT_V.
   * @param[out] val   Value in @p unit.
   * @retval EXIT_SUCCESS  @p val holds the value.
   * @retval EXIT_FAILURE  Any step failed.
   */
  int readAssociated(const uint8_t *obis, uint8_t unit, double *val);

  /**
   * Reads a Register in one whole session: SNRM, then readAssociated(), then
   * DISC. DISC is sent whenever the link came up, even after a failed read.
   *
   * @param[in]  obis  OBIS code, 6 bytes.
   * @param[in]  unit  Expected unit, DLMS_UNIT_WH or DLMS_UNIT_V.
   * @param[out] val   Value in @p unit.
   * @retval EXIT_SUCCESS  @p val holds the value.
   * @retval EXIT_FAILURE  The link did not come up, or the read failed.
   */
  int readRegister(const uint8_t *obis, uint8_t unit, double *val);

  /**
   * Reads one HDLC frame, skipping noise and idle flags before it.
   *
   * Hunts for a flag followed by a type A (DLMS flag) format field, then reads as many
   * bytes as its length field gives. A length that cannot be a frame restarts
   * the hunt. The contents are left to hdlcParseFrame() to check.
   *
   * @param[out] buf  At least HDLC_FRAME_MAX bytes. Holds the frame, flags
   *                  included.
   * @param[out] len  Frame length.
   * @retval EXIT_SUCCESS  @p buf holds as many bytes as the length field gave.
   * @retval EXIT_FAILURE  No complete frame within DLMS_TIMEOUT.
   */
  int readFrame(uint8_t *buf, size_t *len);
};

#endif
