#include "RadioControl.h"
#include "UART.h"
#include "PwmOutput.h"
#include <string.h>

#define DEBOUNCE_MS 30U
#define TEST_INTERVAL_MS 500U
#define ACK_TIMEOUT_MS 500U
#define ACK_DELAY_MS 10U
#define SEND_TIMEOUT_MS 1000U

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    ControlCommand command;
    GPIO_PinState raw, stable;
    uint32_t changed_at;
} Button;

static Button buttons[] = {
    {BTN_START_GPIO_Port, BTN_START_Pin, CONTROL_START, GPIO_PIN_SET, GPIO_PIN_SET, 0},
    {BTN_STOP_GPIO_Port, BTN_STOP_Pin, CONTROL_STOP, GPIO_PIN_SET, GPIO_PIN_SET, 0},
    {BTN_LEFT_GPIO_Port, BTN_LEFT_Pin, CONTROL_LEFT, GPIO_PIN_SET, GPIO_PIN_SET, 0},
    {BTN_RIGHT_GPIO_Port, BTN_RIGHT_Pin, CONTROL_RIGHT, GPIO_PIN_SET, GPIO_PIN_SET, 0}
};

static bool ready, ack_enabled, waiting_ack, ack_pending;
static SystemMode_t role;
static ControlTracker rx;
static uint32_t session, next_sequence;
static uint32_t tx_attempts, tx_ok, tx_error, tx_last_ms, tx_min_ms, tx_max_ms, tx_sum_ms;
static uint32_t rx_invalid, rx_other_session, ack_ok, ack_timeout, ack_tx_error;
static uint32_t ack_started, ack_due, test_remaining, test_last;
static ControlPacket expected_ack, pending_ack;
static bool metrics_valid;
static int16_t last_rssi;
static int last_snr100;

static void reset_buttons(void)
{
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        buttons[i].raw = HAL_GPIO_ReadPin(buttons[i].port, buttons[i].pin);
        buttons[i].stable = buttons[i].raw;
        buttons[i].changed_at = HAL_GetTick();
    }
}

static void reset_session(uint32_t value)
{
    PwmOutput_Stop();
    session = value; next_sequence = 1;
    ControlTracker_Reset(&rx, value);
    tx_attempts = tx_ok = tx_error = tx_last_ms = tx_max_ms = tx_sum_ms = 0;
    tx_min_ms = UINT32_MAX;
    rx_invalid = rx_other_session = ack_ok = ack_timeout = ack_tx_error = 0;
    test_remaining = 0; waiting_ack = ack_pending = metrics_valid = false;
    SX1278_ClearStats();
    reset_buttons();
}

static void print_metrics(void)
{
    int magnitude = last_snr100 < 0 ? -last_snr100 : last_snr100;
    DebugUART_Printf("RSSI=%d dBm SNR=%s%d.%02d dB\r\n", (int)last_rssi,
                    last_snr100 < 0 ? "-" : "", magnitude / 100, magnitude % 100);
}

SX1278_Status RadioControl_Init(void)
{
    ready = false; ack_enabled = false; role = MODE_RX;
    reset_session(1);
    SX1278_Status result = SX1278_StartReceive();
    if (result == SX1278_OK) {
        ready = true;
        DebugUART_Print("Mode: RX, listening; session=1; ACK=OFF\r\n");
    }
    return result;
}

SystemMode_t RadioControl_GetMode(void) { return role; }

void RadioControl_SetMode(SystemMode_t mode)
{
    if (!ready) { DebugUART_Print("Radio not ready\r\n"); return; }
    if (waiting_ack || ack_pending) { DebugUART_Print("Busy: ACK exchange\r\n"); return; }
    SX1278_Status result = (mode == MODE_RX || ack_enabled) ?
        SX1278_StartReceive() : SX1278_SetMode(SX1278_MODE_STANDBY);
    if (result != SX1278_OK) {
        DebugUART_Printf("Mode ERROR: %d\r\n", (int)result); return;
    }
    PwmOutput_Stop();
    role = mode; test_remaining = 0;
    reset_buttons();
    DebugUART_Printf("Mode: %s\r\n", role == MODE_TX ? "TX, ready to send" : "RX, listening");
}

void RadioControl_SetSession(uint32_t value)
{
    if (!ready) { DebugUART_Print("Radio not ready\r\n"); return; }
    if (value == 0 || value == session) {
        DebugUART_Print("Choose a NEW nonzero session number on BOTH boards\r\n"); return;
    }
    if (waiting_ack || ack_pending) { DebugUART_Print("Busy: ACK exchange\r\n"); return; }
    reset_session(value);
    DebugUART_Printf("Session=%lu; counters reset; sequence starts at 1\r\n", (unsigned long)session);
}

void RadioControl_SetAck(bool enabled)
{
    if (!ready) { DebugUART_Print("Radio not ready\r\n"); return; }
    if (waiting_ack || ack_pending || test_remaining) {
        DebugUART_Print("Stop the test and wait for ACK completion first\r\n"); return;
    }
    SX1278_Status result = (role == MODE_RX || enabled) ?
        SX1278_StartReceive() : SX1278_SetMode(SX1278_MODE_STANDBY);
    if (result != SX1278_OK) { DebugUART_Printf("ACK mode ERROR: %d\r\n", (int)result); return; }
    ack_enabled = enabled;
    DebugUART_Printf("ACK=%s; set the SAME option on both boards\r\n", enabled ? "ON" : "OFF");
}

static bool send_command(ControlCommand command)
{
    if (!ready || role != MODE_TX) { DebugUART_Print("First enter: mode tx\r\n"); return false; }
    if (waiting_ack) { DebugUART_Print("Busy: waiting for ACK\r\n"); return false; }
    if (next_sequence > CONTROL_MAX_PACKETS) {
        DebugUART_Print("Session full: choose a new session on both boards\r\n"); return false;
    }
    ControlPacket p = {session, next_sequence, command, false};
    uint8_t bytes[CONTROL_PACKET_SIZE];
    if (!Control_Encode(&p, bytes)) return false;
    next_sequence++; tx_attempts++;
    uint32_t start = HAL_GetTick();
    SX1278_Status result = SX1278_Send(bytes, sizeof(bytes), SEND_TIMEOUT_MS);
    tx_last_ms = HAL_GetTick() - start;
    if (result == SX1278_OK) {
        tx_ok++; tx_sum_ms += tx_last_ms;
        if (tx_last_ms < tx_min_ms) tx_min_ms = tx_last_ms;
        if (tx_last_ms > tx_max_ms) tx_max_ms = tx_last_ms;
        if (ack_enabled) { waiting_ack = true; expected_ack = p; ack_started = HAL_GetTick(); }
    } else { tx_error++; }
    DebugUART_Printf("TX session=%lu seq=%lu cmd=%s result=%d send_ms=%lu\r\n",
                    (unsigned long)session, (unsigned long)p.sequence,
                    Control_CommandName(command), (int)result, (unsigned long)tx_last_ms);
    return true; /* An attempt was made, including a driver failure. */
}

void RadioControl_Send(ControlCommand command)
{
    if (test_remaining) { DebugUART_Print("Test running; enter: test stop\r\n"); return; }
    (void)send_command(command);
}

void RadioControl_StartTest(uint32_t count)
{
    if (!ready || role != MODE_TX) { DebugUART_Print("First enter: mode tx\r\n"); return; }
    if (test_remaining || waiting_ack) { DebugUART_Print("Busy\r\n"); return; }
    if (count == 0 || count > CONTROL_MAX_PACKETS ||
        count > CONTROL_MAX_PACKETS - (next_sequence - 1U)) {
        DebugUART_Print("Invalid count or session capacity exceeded\r\n"); return;
    }
    test_remaining = count;
    test_last = HAL_GetTick() - TEST_INTERVAL_MS;
    DebugUART_Printf("Test started: %lu packets, minimum interval 500 ms\r\n", (unsigned long)count);
}

void RadioControl_StopTest(void)
{
    test_remaining = 0;
    DebugUART_Print("Test stopped; an outstanding ACK can still complete\r\n");
}

static void receive_packet(const SX1278_Packet *radio_packet)
{
    ControlPacket p;
    if (!Control_Decode(radio_packet->data, radio_packet->length, &p)) {
        rx_invalid++; DebugUART_Print("RX invalid packet (length/header/CRC/command)\r\n"); return;
    }
    if (p.session != session) { rx_other_session++; return; }
    if (p.ack) {
        if (role == MODE_TX && waiting_ack && p.sequence == expected_ack.sequence &&
            p.command == expected_ack.command) {
            waiting_ack = false; ack_ok++;
            DebugUART_Printf("ACK seq=%lu wait_ms=%lu\r\n", (unsigned long)p.sequence,
                            (unsigned long)(HAL_GetTick() - ack_started));
        }
        return;
    }
    if (role != MODE_RX) return;
    ControlTrackResult tracked = ControlTracker_Add(&rx, &p);
    if (tracked != CONTROL_NEW && tracked != CONTROL_DUPLICATE) { rx_invalid++; return; }
    /* ACK acknowledges receipt, never causes another ACK. No automatic retries. */
    if (ack_enabled && !ack_pending) {
        pending_ack = p; pending_ack.ack = true;
        ack_pending = true; ack_due = HAL_GetTick();
    }
    if (tracked == CONTROL_DUPLICATE) {
        DebugUART_Printf("RX duplicate seq=%lu (not applied again)\r\n", (unsigned long)p.sequence);
        return;
    }
    last_rssi = radio_packet->rssi_dbm;
    last_snr100 = (int)(radio_packet->snr_db * 100.0f);
    metrics_valid = true;
    DebugUART_Printf("RX session=%lu seq=%lu cmd=%s\r\n", (unsigned long)p.session,
                    (unsigned long)p.sequence, Control_CommandName(p.command));
    print_metrics();
    DebugUART_Printf("RX unique=%lu missing_so_far=%lu\r\n", (unsigned long)rx.unique,
                    (unsigned long)(rx.highest - rx.unique));
    /* Late packets still count as received, but must not overwrite a newer output. */
    if (p.sequence == rx.highest) {
        if (PwmOutput_ApplyCommand(p.command))
            DebugUART_Printf("PWM PA0: %u%% (timer base 1000 Hz)\r\n",
                            (unsigned)PwmOutput_GetDuty());
    } else {
        DebugUART_Print("PWM unchanged: older sequence\r\n");
    }
}

static void process_buttons(void)
{
    /* Sample ALL buttons before a blocking radio transmission. */
    uint8_t pressed = 0;
    uint32_t now = HAL_GetTick();
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        Button *b = &buttons[i];
        GPIO_PinState value = HAL_GPIO_ReadPin(b->port, b->pin);
        if (value != b->raw) { b->raw = value; b->changed_at = now; }
        if (b->raw != b->stable && (uint32_t)(now - b->changed_at) >= DEBOUNCE_MS) {
            b->stable = b->raw;
            if (b->stable == GPIO_PIN_RESET) pressed |= (uint8_t)(1U << i);
        }
    }
    if (role != MODE_TX || pressed == 0) return;
    /* Multiple simultaneous commands are ambiguous: STOP wins, else ignore. */
    if ((pressed & 2U) != 0) { RadioControl_Send(CONTROL_STOP); return; }
    if ((pressed & (pressed - 1U)) != 0) {
        DebugUART_Print("Press one button at a time\r\n"); return;
    }
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        if (pressed & (1U << i)) RadioControl_Send(buttons[i].command);
}

void RadioControl_Process(void)
{
    if (!ready) return;
    SX1278_Process();
    SX1278_Packet packet;
    if (SX1278_ReadPacket(&packet) == SX1278_OK) receive_packet(&packet);
    uint32_t now = HAL_GetTick();
    if (waiting_ack && (uint32_t)(now - ack_started) >= ACK_TIMEOUT_MS) {
        waiting_ack = false; ack_timeout++;
        DebugUART_Printf("ACK timeout seq=%lu (delivery unknown)\r\n", (unsigned long)expected_ack.sequence);
    }
    if (ack_pending && (uint32_t)(now - ack_due) >= ACK_DELAY_MS) {
        uint8_t bytes[CONTROL_PACKET_SIZE];
        if (Control_Encode(&pending_ack, bytes) &&
            SX1278_Send(bytes, sizeof(bytes), SEND_TIMEOUT_MS) != SX1278_OK) ack_tx_error++;
        ack_pending = false;
    }
    process_buttons();
    now = HAL_GetTick();
    if (test_remaining && !waiting_ack && (uint32_t)(now - test_last) >= TEST_INTERVAL_MS) {
        test_last = now;
        if (send_command(CONTROL_TEST)) {
            test_remaining--;
            if (!test_remaining) DebugUART_Print("Test transmissions finished; enter stats\r\n");
        } else test_remaining = 0;
    }
}

void RadioControl_PrintStats(void)
{
    DebugUART_Printf("ready=%u role=%s session=%lu ACK=%s uptime_ms=%lu\r\n", (unsigned)ready,
                    role == MODE_TX ? "TX" : "RX", (unsigned long)session,
                    ack_enabled ? "ON" : "OFF", (unsigned long)HAL_GetTick());
    DebugUART_Printf("TX attempts=%lu done=%lu errors=%lu last_seq=%lu\r\n",
                    (unsigned long)tx_attempts, (unsigned long)tx_ok,
                    (unsigned long)tx_error, (unsigned long)(next_sequence ? next_sequence - 1U : 0));
    if (tx_ok) DebugUART_Printf("Successful send call ms: min=%lu max=%lu avg=%lu\r\n",
                              (unsigned long)tx_min_ms, (unsigned long)tx_max_ms,
                              (unsigned long)(tx_sum_ms / tx_ok));
    DebugUART_Printf("RX unique=%lu highest=%lu missing_so_far=%lu\r\n",
                    (unsigned long)rx.unique, (unsigned long)rx.highest,
                    (unsigned long)(rx.highest - rx.unique));
    DebugUART_Printf("RX duplicate=%lu out_of_order=%lu invalid=%lu other_session=%lu\r\n",
                    (unsigned long)rx.duplicates, (unsigned long)rx.out_of_order,
                    (unsigned long)rx_invalid, (unsigned long)rx_other_session);
    DebugUART_Printf("ACK received=%lu timeout=%lu reply_errors=%lu waiting=%u test_remaining=%lu\r\n",
                    (unsigned long)ack_ok, (unsigned long)ack_timeout, (unsigned long)ack_tx_error,
                    (unsigned)waiting_ack, (unsigned long)test_remaining);
    SX1278_Stats s = SX1278_GetStats();
    DebugUART_Printf("Driver CRC_errors=%lu RX_dropped=%lu SPI_errors=%lu\r\n",
                    (unsigned long)s.rx_crc_error, (unsigned long)s.rx_dropped, (unsigned long)s.spi_error);
    DebugUART_Printf("PWM PA0: %u%% (timer base 1000 Hz)\r\n", (unsigned)PwmOutput_GetDuty());
    if (metrics_valid) print_metrics();
}

void RadioControl_Report(uint32_t total)
{
    uint32_t received, missing;
    if (!ControlTracker_Report(&rx, total, &received, &missing)) {
        DebugUART_Print("Total must be 1..10000 and >= highest received sequence\r\n"); return;
    }
    uint32_t percent100 = missing * 10000U / total;
    DebugUART_Printf("REPORT session=%lu total=%lu received=%lu missing=%lu loss=%lu.%02lu%%\r\n",
                    (unsigned long)session, (unsigned long)total, (unsigned long)received,
                    (unsigned long)missing, (unsigned long)(percent100 / 100U),
                    (unsigned long)(percent100 % 100U));
}
