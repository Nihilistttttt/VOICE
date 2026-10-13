#ifndef __AOA_HOST_H__
#define __AOA_HOST_H__

#include <stdint.h>

typedef enum {
    AOA_STATE_DISCONNECTED = 0,
    AOA_STATE_CONNECTED,
    AOA_STATE_READY,
} AOA_State_t;

typedef enum {
    AOA_OK = 0,
    AOA_ERR_TX = -1,
    AOA_ERR_RX = -2,
    AOA_ERR_TIMEOUT = -3,
    AOA_ERR_PROTOCOL = -4,
    AOA_ERR_NOT_INIT = -5,
    AOA_ERR_NO_AOA = -6,
} AOA_Err_t;

AOA_Err_t AOA_Host_Init(void);
AOA_Err_t AOA_Host_Connect(void);
AOA_Err_t AOA_Host_Disconnect(void);
AOA_State_t AOA_Host_GetState(void);
AOA_Err_t AOA_Host_Send(const uint8_t *data, uint16_t len);
AOA_Err_t AOA_Host_Recv(uint8_t *buf, uint16_t buf_size, uint16_t *actual_len);
void AOA_Host_Poll(void);

#endif