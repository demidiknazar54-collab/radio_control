#include "CommandParser.h"
#include "UART.h"
#include <string.h>

static bool number(const char *text, uint32_t *value)
{
    uint32_t n = 0;
    if (*text == '\0') return false;
    while (*text) {
        if (*text < '0' || *text > '9') return false;
        unsigned digit = (unsigned)(*text++ - '0');
        if (n > (UINT32_MAX - digit) / 10U) return false;
        n = n * 10U + digit;
    }
    if (n == 0) return false;
    *value = n;
    return true;
}

void CommandParser_Init(void)
{
    DebugUART_Print("\r\nCommand Parser initialized\r\nType 'help' for commands\r\n");
}

SystemMode_t CommandParser_GetMode(void) { return RadioControl_GetMode(); }

void CommandParser_Process(const char *command)
{
    if (command == NULL) return;
    while (*command == ' ') command++;
    char line[DEBUG_UART_LINE_SIZE];
    size_t length = strlen(command);
    if (length >= sizeof(line)) { DebugUART_Print("Command too long\r\n"); return; }
    memcpy(line, command, length + 1U);
    while (length && line[length - 1U] == ' ') line[--length] = '\0';
    if (!length) return;
    uint32_t n;
    if (strcmp(line, "help") == 0) {
        DebugUART_Print(
            "help | status | stats\r\n"
            "mode tx | mode rx\r\n"
            "START | STOP | LEFT | RIGHT (lowercase also accepted)\r\n"
            "test       - send one TEST\r\n"
            "test N     - send N TEST packets, minimum interval 500 ms\r\n"
            "test stop  - stop automatic test\r\n"
            "session N  - NEW nonzero session on BOTH boards; reset counters\r\n"
            "report N   - receiver report for N actual TX attempts\r\n"
            "ack on | ack off (same on both boards; default off)\r\n"
            "Buttons: PB0 START, PB1 STOP, PB10 LEFT, PB11 RIGHT\r\n"
        );
    } else if (strcmp(line, "status") == 0 || strcmp(line, "stats") == 0) {
        RadioControl_PrintStats();
    } else if (strcmp(line, "mode tx") == 0) RadioControl_SetMode(MODE_TX);
    else if (strcmp(line, "mode rx") == 0) RadioControl_SetMode(MODE_RX);
    else if (strcmp(line, "START") == 0 || strcmp(line, "start") == 0) RadioControl_Send(CONTROL_START);
    else if (strcmp(line, "STOP") == 0 || strcmp(line, "stop") == 0) RadioControl_Send(CONTROL_STOP);
    else if (strcmp(line, "LEFT") == 0 || strcmp(line, "left") == 0) RadioControl_Send(CONTROL_LEFT);
    else if (strcmp(line, "RIGHT") == 0 || strcmp(line, "right") == 0) RadioControl_Send(CONTROL_RIGHT);
    else if (strcmp(line, "test") == 0) RadioControl_Send(CONTROL_TEST);
    else if (strcmp(line, "test stop") == 0) RadioControl_StopTest();
    else if (strncmp(line, "test ", 5) == 0 && number(line + 5, &n)) RadioControl_StartTest(n);
    else if (strncmp(line, "session ", 8) == 0 && number(line + 8, &n)) RadioControl_SetSession(n);
    else if (strncmp(line, "report ", 7) == 0 && number(line + 7, &n)) RadioControl_Report(n);
    else if (strcmp(line, "ack on") == 0) RadioControl_SetAck(true);
    else if (strcmp(line, "ack off") == 0) RadioControl_SetAck(false);
    else DebugUART_Print("Unknown command or invalid number. Type help\r\n");
}
