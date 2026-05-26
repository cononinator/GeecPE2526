/*
 * CANtest.ino
 * ESP32 TWAI (CAN bus) receive/transmit example.
 *
 * Wiring:
 *   ESP32 GPIO5  -> CAN TX pin of transceiver (e.g. SN65HVD233)
 *   ESP32 GPIO4  -> CAN RX pin of transceiver
 *   Transceiver CANH/CANL -> CAN bus
 *
 * Adjust TX_PIN, RX_PIN, and BITRATE to match your setup.
 */

#include "driver/twai.h"

// ── Pin & bus config ────────────────────────────────────────────────────────
#define TX_PIN   D9
#define RX_PIN   D8
#define BITRATE  TWAI_TIMING_CONFIG_500KBITS()   // change as needed

// ── Optional: only accept specific IDs ──────────────────────────────────────
// Use TWAI_FILTER_CONFIG_ACCEPT_ALL() to receive everything
#define FILTER   TWAI_FILTER_CONFIG_ACCEPT_ALL()

// ── Transmit interval (ms) – set 0 to disable TX ───────────────────────────
#define TX_INTERVAL_MS  1000

static unsigned long lastTx = 0;

// ────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("ESP32 TWAI CAN test starting...");

    // Driver configuration
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_PIN, (gpio_num_t)RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t  t_config = BITRATE;
    twai_filter_config_t  f_config = FILTER;

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        Serial.println("ERROR: TWAI driver install failed");
        while (1);
    }

    if (twai_start() != ESP_OK) {
        Serial.println("ERROR: TWAI start failed");
        while (1);
    }

    Serial.println("TWAI driver started. Listening on CAN bus...");
}

void loop() {
    // ── Receive ──────────────────────────────────────────────────────────────
    twai_message_t rx;
    // Non-blocking: timeout = 0.  Use portMAX_DELAY to block until a frame arrives.
    if (twai_receive(&rx, pdMS_TO_TICKS(0)) == ESP_OK) {
        printMessage(rx);
    }

    // ── Transmit (optional) ──────────────────────────────────────────────────
#if TX_INTERVAL_MS > 0
    unsigned long now = millis();
    if (now - lastTx >= TX_INTERVAL_MS) {
        lastTx = now;
        sendTestFrame();
    }
#endif
}

// ── Helpers ──────────────────────────────────────────────────────────────────

void printMessage(const twai_message_t &msg) {
    if (msg.extd) {
        Serial.printf("RX  EXT  ID: 0x%08X  DLC: %d  Data:", msg.identifier, msg.data_length_code);
    } else {
        Serial.printf("RX  STD  ID: 0x%03X      DLC: %d  Data:", msg.identifier, msg.data_length_code);
    }

    if (msg.rtr) {
        Serial.print("  [RTR frame]");
    } else {
        for (int i = 0; i < msg.data_length_code; i++) {
            Serial.printf(" %02X", msg.data[i]);
        }
    }
    Serial.println();
}

void sendTestFrame() {
    twai_message_t tx = {};
    tx.identifier       = 0x032;   // 11-bit standard ID
    tx.extd             = 0;       // 0 = standard frame, 1 = extended
    tx.rtr              = 0;
    tx.data_length_code = 8;
    for (int i = 0; i < 8; i++) tx.data[i] = i;

    esp_err_t result = twai_transmit(&tx, pdMS_TO_TICKS(100));
    if (result == ESP_OK) {
        Serial.println("TX  sent  ID: 0x100");
    } else {
        Serial.printf("TX  failed  err: %d\n", result);
    }
}
