#ifndef SX1278_H_
#define SX1278_H_

#include "main.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SX1278_MAX_PACKET_SIZE 255U

typedef enum {
    SX1278_OK = 0,
    SX1278_ERROR,
    SX1278_TIMEOUT,
    SX1278_BAD_ARGUMENT,
    SX1278_NOT_INITIALIZED,
    SX1278_BUSY,
    SX1278_NO_PACKET,
    SX1278_CRC_ERROR
} SX1278_Status;

typedef enum {
    SX1278_MODE_SLEEP = 0,
    SX1278_MODE_STANDBY,
    SX1278_MODE_RX_CONTINUOUS,
    SX1278_MODE_TX
} SX1278_Mode;

typedef struct {
    uint32_t frequency_hz;     /* 137-525 MHz for SX1278; use module's RF range */
    uint8_t bandwidth;         /* 0..9: 7.8,10.4,15.6,20.8,31.25,41.7,62.5,125,250,500 kHz */
    uint8_t spreading_factor;  /* 6..12 */
    uint8_t coding_rate;       /* 5..8, denominator of 4/x */
    uint8_t sync_word;         /* e.g. 0x12 private network */
    uint16_t preamble_length;  /* e.g. 8 */
    int8_t tx_power_dbm;       /* 2..17 dBm, PA_BOOST */
    bool crc_enabled;
} SX1278_Config;

typedef struct {
    uint8_t data[SX1278_MAX_PACKET_SIZE];
    uint8_t length;
    int16_t rssi_dbm;
    float snr_db;
} SX1278_Packet;

typedef struct {
    uint32_t tx_ok;
    uint32_t tx_timeout;
    uint32_t rx_ok;
    uint32_t rx_crc_error;
    uint32_t rx_dropped;
    uint32_t spi_error;
} SX1278_Stats;

/* GPIO labels in CubeMX: LORA_NSS, LORA_RST, LORA_DIO0. */
SX1278_Config SX1278_DefaultConfig(void);
SX1278_Status SX1278_Init(SPI_HandleTypeDef *hspi, const SX1278_Config *config);
SX1278_Status SX1278_SetMode(SX1278_Mode mode);
SX1278_Status SX1278_StartReceive(void);
SX1278_Status SX1278_Send(const uint8_t *data, uint8_t length, uint32_t timeout_ms);
/* Call regularly from main loop; never from an interrupt. */
void SX1278_Process(void);
/* Call from HAL_GPIO_EXTI_Callback() when LORA_DIO0_Pin triggers. */
void SX1278_Dio0IrqHandler(void);
/* One-slot RX mailbox. Read promptly or new packets may be dropped. */
SX1278_Status SX1278_ReadPacket(SX1278_Packet *packet);
SX1278_Mode SX1278_GetMode(void);
SX1278_Stats SX1278_GetStats(void);
void SX1278_ClearStats(void);
uint8_t SX1278_ReadVersion(void);

#endif
