#ifndef LUNIRIS_RPC_IDS_H
#define LUNIRIS_RPC_IDS_H

/* =============================================================================
 * Luniris RPC IDs
 * Shared between client and server
 * ============================================================================= */

/* Eye Position API */
#define LUNIRIS_RPC_SEND_EYE_COORDINATES    0x0001
#define LUNIRIS_RPC_SEND_EYELID_STATE       0x0002
#define LUNIRIS_RPC_GET_EYE_COORDINATES     0x0003
#define LUNIRIS_RPC_GET_EYELID_STATE        0x0004

/* Display API */
#define LUNIRIS_RPC_GET_BRIGHTNESS_LEVEL    0x0010
#define LUNIRIS_RPC_SEND_BRIGHTNESS_LEVEL   0x0011

/* IMU API */
#define LUNIRIS_RPC_GET_GYROSCOPE_VALUES      0x0020
#define LUNIRIS_RPC_GET_ACCELEROMETER_VALUES  0x0021
#define LUNIRIS_RPC_GET_TEMPERATURE_VALUE     0x0022

/* Actions API */
#define LUNIRIS_RPC_GET_REGISTERED_ACTIONS    0x0030
#define LUNIRIS_RPC_GET_ACTION                0x0031
#define LUNIRIS_RPC_SEND_ACTION               0x0032

#endif /* LUNIRIS_RPC_IDS_H */
