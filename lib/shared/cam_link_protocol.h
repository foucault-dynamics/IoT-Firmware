#ifndef CAM_LINK_PROTOCOL_H
#define CAM_LINK_PROTOCOL_H
#include <stdint.h>

/*
 * Wire contract for the UART link between the ESP32-CAM board and the
 * ESP32-C3 base. Included by BOTH firmwares (cam_link reader on the C3
 * side, the CAM board main on the other) so the message format is
 * defined exactly once.
 *
 * TODO: define start byte, message IDs, reading message struct
 * use (#pragma pack to fix alignment of payload), and checksum function.
 */

#endif
