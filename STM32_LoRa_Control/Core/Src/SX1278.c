#include "SX1278.h"
#include <string.h>

/* SX127x LoRa registers */
#define REG_FIFO                 0x00
#define REG_OP_MODE              0x01
#define REG_FRF_MSB              0x06
#define REG_PA_CONFIG            0x09
#define REG_LNA                  0x0C
#define REG_FIFO_ADDR_PTR        0x0D
#define REG_FIFO_TX_BASE_ADDR    0x0E
#define REG_FIFO_RX_BASE_ADDR    0x0F
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS_MASK       0x11
#define REG_IRQ_FLAGS            0x12
#define REG_RX_NB_BYTES          0x13
#define REG_PKT_SNR_VALUE        0x19
#define REG_PKT_RSSI_VALUE       0x1A
#define REG_MODEM_CONFIG_1       0x1D
#define REG_MODEM_CONFIG_2       0x1E
#define REG_PREAMBLE_MSB         0x20
#define REG_PREAMBLE_LSB         0x21
#define REG_PAYLOAD_LENGTH       0x22
#define REG_MODEM_CONFIG_3       0x26
#define REG_DETECTION_OPTIMIZE   0x31
#define REG_DETECTION_THRESHOLD  0x37
#define REG_SYNC_WORD            0x39
#define REG_DIO_MAPPING_1        0x40
#define REG_VERSION              0x42
#define REG_PA_DAC               0x4D

#define IRQ_RX_TIMEOUT           0x80
#define IRQ_RX_DONE              0x40
#define IRQ_PAYLOAD_CRC_ERROR    0x20
#define IRQ_TX_DONE              0x08
#define OPMODE_LONG_RANGE        0x80
#define OPMODE_LOW_FREQUENCY     0x08

static SPI_HandleTypeDef *radio_spi;
static bool initialized;
static volatile bool dio0_pending;
static bool packet_pending;
static bool tx_done;
static SX1278_Mode radio_mode = SX1278_MODE_SLEEP;
static SX1278_Config radio_config;
static SX1278_Packet mailbox;
static SX1278_Stats statistics;

static void select_radio(void) { HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET); }
static void deselect_radio(void) { HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET); }

static bool transfer(uint8_t address, uint8_t *data, size_t length, bool write)
{
    if (radio_spi == NULL || data == NULL || length == 0 || length > 255) return false;
    uint8_t cmd = (uint8_t)((address & 0x7F) | (write ? 0x80 : 0));
    HAL_StatusTypeDef status;
    select_radio();
    status = HAL_SPI_Transmit(radio_spi, &cmd, 1, 100);
    if (status == HAL_OK) {
        if (write) status = HAL_SPI_Transmit(radio_spi, data, (uint16_t)length, 100);
        else status = HAL_SPI_Receive(radio_spi, data, (uint16_t)length, 100);
    }
    deselect_radio();
    if (status != HAL_OK) statistics.spi_error++;
    return status == HAL_OK;
}

static bool reg_read(uint8_t address, uint8_t *value) { return transfer(address, value, 1, false); }
static bool reg_write(uint8_t address, uint8_t value) { return transfer(address, &value, 1, true); }
static bool write_u16(uint8_t msb_addr, uint16_t value)
{
    return reg_write(msb_addr, (uint8_t)(value >> 8)) &&
           reg_write((uint8_t)(msb_addr + 1), (uint8_t)value);
}

static bool set_hw_mode(uint8_t bits)
{
    uint8_t op = OPMODE_LONG_RANGE | (radio_config.frequency_hz < 525000000UL ? OPMODE_LOW_FREQUENCY : 0) | bits;
    return reg_write(REG_OP_MODE, op);
}

SX1278_Config SX1278_DefaultConfig(void)
{
    SX1278_Config c = {433000000UL, 7, 7, 5, 0x12, 8, 14, true}; //Конфігурація лори
    return c;
}

uint8_t SX1278_ReadVersion(void)
{
    uint8_t value = 0;
    if (!reg_read(REG_VERSION, &value)) return 0;
    return value;
}

SX1278_Status SX1278_Init(SPI_HandleTypeDef *hspi, const SX1278_Config *config)
{
    if (hspi == NULL || config == NULL || config->frequency_hz < 137000000UL ||
        config->frequency_hz > 525000000UL || config->bandwidth > 9 ||
        config->spreading_factor < 7 || config->spreading_factor > 12 ||
        config->coding_rate < 5 || config->coding_rate > 8 ||
        config->tx_power_dbm < 2 || config->tx_power_dbm > 17 ||
        config->preamble_length == 0) return SX1278_BAD_ARGUMENT;

    initialized = false;
    radio_spi = hspi;
    radio_config = *config;
    memset(&statistics, 0, sizeof(statistics));
    packet_pending = false;
    tx_done = false;
    dio0_pending = false;
    deselect_radio();
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(20);
    if (SX1278_ReadVersion() != 0x12) return SX1278_ERROR;

    /* LoRa mode may only be changed in sleep. */
    if (!set_hw_mode(0x00)) return SX1278_ERROR;
    HAL_Delay(2);
    if (!set_hw_mode(0x01)) return SX1278_ERROR;
    HAL_Delay(2);

    uint64_t frf = ((uint64_t)config->frequency_hz << 19) / 32000000ULL;
    if (!reg_write(REG_FRF_MSB, (uint8_t)(frf >> 16)) ||
        !reg_write(REG_FRF_MSB + 1, (uint8_t)(frf >> 8)) ||
        !reg_write(REG_FRF_MSB + 2, (uint8_t)frf)) return SX1278_ERROR;

    /* PA_BOOST; power = 2 + OutputPower when PaDac=0x84. */
    if (!reg_write(REG_PA_DAC, 0x84) ||
        !reg_write(REG_PA_CONFIG, (uint8_t)(0x80 | (config->tx_power_dbm - 2)))) return SX1278_ERROR;

    uint8_t mc1 = (uint8_t)((config->bandwidth << 4) | ((config->coding_rate - 4) << 1));
    /* Explicit header mode: SF7..SF12. */
    uint8_t mc2 = (uint8_t)((config->spreading_factor << 4) | (config->crc_enabled ? 0x04 : 0));
    /* LowDataRateOptimize for symbol duration >16ms. */
    const uint32_t bw_hz[10] = {7800,10400,15600,20800,31250,41700,62500,125000,250000,500000};
    bool ldro = ((uint32_t)(1UL << config->spreading_factor) * 1000UL) > (16UL * bw_hz[config->bandwidth]);
    if (!reg_write(REG_MODEM_CONFIG_1, mc1) ||
        !reg_write(REG_MODEM_CONFIG_2, mc2) ||
        !reg_write(REG_MODEM_CONFIG_3, (uint8_t)(0x04 | (ldro ? 0x08 : 0)))) return SX1278_ERROR;

    if (!write_u16(REG_PREAMBLE_MSB, config->preamble_length) ||
        !reg_write(REG_SYNC_WORD, config->sync_word) ||
        !reg_write(REG_FIFO_TX_BASE_ADDR, 0x00) ||
        !reg_write(REG_FIFO_RX_BASE_ADDR, 0x00) ||
        !reg_write(REG_LNA, 0x23) ||
        !reg_write(REG_IRQ_FLAGS_MASK, 0x00) ||
        !reg_write(REG_IRQ_FLAGS, 0xFF)) return SX1278_ERROR;

    radio_mode = SX1278_MODE_STANDBY;
    initialized = true;
    return SX1278_OK;
}

SX1278_Status SX1278_SetMode(SX1278_Mode mode)
{
    if (!initialized) return SX1278_NOT_INITIALIZED;
    uint8_t bits;
    switch (mode) {
        case SX1278_MODE_SLEEP: bits = 0x00; break;
        case SX1278_MODE_STANDBY: bits = 0x01; break;
        case SX1278_MODE_RX_CONTINUOUS: bits = 0x05; break;
        case SX1278_MODE_TX: bits = 0x03; break;
        default: return SX1278_BAD_ARGUMENT;
    }
    if (!set_hw_mode(bits)) return SX1278_ERROR;
    radio_mode = mode;
    return SX1278_OK;
}

SX1278_Status SX1278_StartReceive(void)
{
    if (!initialized) return SX1278_NOT_INITIALIZED;
    if (SX1278_SetMode(SX1278_MODE_STANDBY) != SX1278_OK ||
        !reg_write(REG_DIO_MAPPING_1, 0x00) || /* DIO0 = RxDone */
        !reg_write(REG_IRQ_FLAGS, 0xFF) ||
        !reg_write(REG_FIFO_ADDR_PTR, 0x00)) return SX1278_ERROR;
    dio0_pending = false;
    return SX1278_SetMode(SX1278_MODE_RX_CONTINUOUS);
}

void SX1278_Dio0IrqHandler(void)
{
    dio0_pending = true;
}

void SX1278_Process(void)
{
    if (!initialized || radio_mode == SX1278_MODE_SLEEP) return;
    /* Poll IRQ flags too: catches an edge missed during mode transition. */
    uint8_t flags = 0;
    if (!reg_read(REG_IRQ_FLAGS, &flags)) return;
    dio0_pending = false;
    if (flags == 0) return;
    if (flags & IRQ_TX_DONE) {
        tx_done = true;
        statistics.tx_ok++;
    }
    if (flags & IRQ_RX_DONE) {
        if (flags & IRQ_PAYLOAD_CRC_ERROR) {
            statistics.rx_crc_error++;
        } else if (radio_mode == SX1278_MODE_RX_CONTINUOUS) {
            if (packet_pending) {
                statistics.rx_dropped++;
            } else {
                uint8_t addr = 0, length = 0, snr_raw = 0, rssi_raw = 0;
                if (reg_read(REG_FIFO_RX_CURRENT_ADDR, &addr) &&
                    reg_read(REG_RX_NB_BYTES, &length) &&
                    reg_read(REG_PKT_SNR_VALUE, &snr_raw) &&
                    reg_read(REG_PKT_RSSI_VALUE, &rssi_raw) &&
                    reg_write(REG_FIFO_ADDR_PTR, addr) &&
                    length > 0 && transfer(REG_FIFO, mailbox.data, length, false)) {
                    mailbox.length = length;
                    mailbox.snr_db = (float)((int8_t)snr_raw) / 4.0f;
                    mailbox.rssi_dbm = (int16_t)rssi_raw - 164;
                    if (mailbox.snr_db < 0) mailbox.rssi_dbm += (int16_t)mailbox.snr_db;
                    packet_pending = true;
                    statistics.rx_ok++;
                } else {
                    statistics.rx_dropped++;
                }
            }
        }
    }
    (void)reg_write(REG_IRQ_FLAGS, flags); /* Write 1 to clear IRQ flags */
}

SX1278_Status SX1278_Send(const uint8_t *data, uint8_t length, uint32_t timeout_ms)
{
    if (!initialized) return SX1278_NOT_INITIALIZED;
    if (data == NULL || length == 0 || timeout_ms == 0) return SX1278_BAD_ARGUMENT;
    if (radio_mode == SX1278_MODE_TX) return SX1278_BUSY;
    bool resume_rx = (radio_mode == SX1278_MODE_RX_CONTINUOUS);
    if (SX1278_SetMode(SX1278_MODE_STANDBY) != SX1278_OK ||
        !reg_write(REG_DIO_MAPPING_1, 0x40) || /* DIO0 = TxDone */
        !reg_write(REG_IRQ_FLAGS, 0xFF) ||
        !reg_write(REG_FIFO_ADDR_PTR, 0x00) ||
        !reg_write(REG_PAYLOAD_LENGTH, length) ||
        !transfer(REG_FIFO, (uint8_t *)data, length, true)) return SX1278_ERROR;
    tx_done = false;
    dio0_pending = false;
    if (SX1278_SetMode(SX1278_MODE_TX) != SX1278_OK) return SX1278_ERROR;
    uint32_t start = HAL_GetTick();
    while (!tx_done && (uint32_t)(HAL_GetTick() - start) < timeout_ms) SX1278_Process();
    SX1278_Status result = tx_done ? SX1278_OK : SX1278_TIMEOUT;
    if (result == SX1278_TIMEOUT) statistics.tx_timeout++;
    if (resume_rx) {
        if (SX1278_StartReceive() != SX1278_OK) return SX1278_ERROR;
    } else if (SX1278_SetMode(SX1278_MODE_STANDBY) != SX1278_OK) return SX1278_ERROR;
    return result;
}

SX1278_Status SX1278_ReadPacket(SX1278_Packet *packet)
{
    if (packet == NULL) return SX1278_BAD_ARGUMENT;
    if (!initialized) return SX1278_NOT_INITIALIZED;
    if (!packet_pending) return SX1278_NO_PACKET;
    *packet = mailbox;
    packet_pending = false;
    return SX1278_OK;
}

SX1278_Mode SX1278_GetMode(void) { return radio_mode; }
SX1278_Stats SX1278_GetStats(void) { return statistics; }
void SX1278_ClearStats(void) { memset(&statistics, 0, sizeof(statistics)); }
