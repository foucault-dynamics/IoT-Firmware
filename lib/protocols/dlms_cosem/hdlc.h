/**
 * @file
 * HDLC functions (help the DLMS/COSEM Reader)
 */
#ifndef HDLC_H
#define HDLC_H

#include <cstdint>
#include <cstddef>
/** Max length for an HDLC/DLMS address. */
#define HDLC_ADDRESS_MAX 4
/** Info field limit with default (non negotiated) HDLC parameters. */
#define HDLC_INFO_MAX 128
/** Largest frame: flags, format, two 4 byte addresses, control, HCS, info, FCS. */
#define HDLC_FRAME_MAX (HDLC_INFO_MAX + 19)
/** Opening and closing flag of every frame. */
#define HDLC_FLAG 0x7E
/** High byte of the format field: type A, no segmentation, before the length. */
#define HDLC_FORMAT_TYPE_A 0xA0

/**
 * An encoded HDLC address field, ready to copy into a frame. Every byte holds
 * 7 address bits shifted left by one, and bit 0 is set only on the last byte.
 */
struct HdlcAddress {
  uint8_t bytes[HDLC_ADDRESS_MAX];  ///< Wire bytes, the first @c len are valid
  size_t len;                       ///< Number of bytes used, 1, 2 or 4
};

/**
 * A parsed HDLC frame. @c info points into the buffer that was parsed, so that
 * buffer must outlive the struct.
 */
struct HdlcFrame {
  HdlcAddress dest;      ///< Destination address, as on the wire
  HdlcAddress src;       ///< Source address, as on the wire
  uint8_t control;       ///< Control byte
  const uint8_t *info;   ///< Info field inside the parsed buffer, nullptr if none
  size_t infoLen;        ///< Length of @c info, 0 if none
};

/**
 * Computes the FCS (Frame Check Sequence) for a HDLC buffer.
 *
 * @param[in] data  Bytes to checksum
 * @param[in] len  Length of data
 * @return The CRC, to be sent low byte first
 */
uint16_t hdlcFcs(const uint8_t *data, size_t len);

/**
 * Encodes an HDLC address field. The upper address is the logical device
 * (1 for the management logical device) or the client SAP. The lower address
 * is the physical device on the bus, and is left out when @p size is 1.
 *
 * @param[in] upper  Upper address, at most 0x7F (0x3FFF when @p size is 4)
 * @param[in] lower  Lower address, same limit, ignored when @p size is 1
 * @param[in] size   Address length in bytes, 1, 2 or 4
 * @param[out] out   Encoded address
 * @retval EXIT_SUCCESS  @p out holds the address
 * @retval EXIT_FAILURE  @p size is not 1, 2 or 4, or an address does not fit
 */
int hdlcAddress(uint16_t upper, uint16_t lower, uint8_t size, HdlcAddress *out);

/**
 * Builds a complete HDLC frame, flags included. The HCS is only written when
 * there is an info field, since without one the FCS already covers the header.
 *
 * @param[in] dest     Destination address (the server, for a client frame)
 * @param[in] src      Source address
 * @param[in] control  Control byte, e.g. 0x93 for SNRM
 * @param[in] info     Info field, may be nullptr when @p infoLen is 0
 * @param[in] infoLen  Length of @p info
 * @param[out] out     Frame bytes, must hold HDLC_FRAME_MAX
 * @param[out] outLen  Frame length including both flags
 * @retval EXIT_SUCCESS  @p out holds the frame
 * @retval EXIT_FAILURE  @p infoLen is over HDLC_INFO_MAX
 */
int hdlcBuildFrame(const HdlcAddress *dest, const HdlcAddress *src,
                   uint8_t control, const uint8_t *info, size_t infoLen,
                   uint8_t *out, size_t *outLen);

/**
 * Parses and checks a complete HDLC frame, flags included. Checks the flags,
 * the format type, the length field, both addresses, the HCS when there is an
 * info field, and the FCS. Does not check who the frame is addressed to.
 *
 * @param[in] buf   Frame bytes
 * @param[in] len   Length of @p buf
 * @param[out] out  Parsed frame, its info pointing into @p buf
 * @retval EXIT_SUCCESS  @p out holds the frame
 * @retval EXIT_FAILURE  The frame is malformed, segmented, or fails a check
 */
int hdlcParseFrame(const uint8_t *buf, size_t len, HdlcFrame *out);

#endif
