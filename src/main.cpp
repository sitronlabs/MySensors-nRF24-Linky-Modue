/* Config */
#include "../cfg/config.h"

/* Project code */
#include "linky_tic.h"

/* Arduino Libraries */
#include <Arduino.h>
#include <MySensors.h>

/* C/C++ libraries */
#include <ctype.h>
#include <stdlib.h>

/* Working variables */
static linky_tic m_linky;

/* Wireless messages */
static MyMessage m_message_info_serial(0, V_TEXT);
static MyMessage m_message_power_kwh(1, V_KWH);
static MyMessage m_message_power_watt(2, V_WATT);
static MyMessage m_message_phase1_voltage(3, V_VOLTAGE);
static MyMessage m_message_phase1_current(3, V_CURRENT);
static MyMessage m_message_phase2_voltage(4, V_VOLTAGE);
static MyMessage m_message_phase2_current(4, V_CURRENT);
static MyMessage m_message_phase3_voltage(5, V_VOLTAGE);
static MyMessage m_message_phase3_current(5, V_CURRENT);

/**
 * Setup function.
 * Called before MySensors does anything.
 */
void preHwInit() {

    /* Setup leds
     * Ensures tic link led is off at startup */
    pinMode(CONFIG_LED_LINKY_GREEN_PIN, OUTPUT);
    pinMode(CONFIG_LED_LINKY_RED_PIN, OUTPUT);
    digitalWrite(CONFIG_LED_LINKY_GREEN_PIN, LOW);
    digitalWrite(CONFIG_LED_LINKY_RED_PIN, LOW);
}

/**
 *
 */
void setup_failed(const char *const reason) {
    Serial.println(reason);
    Serial.flush();
    while (1) {
        // sleep(0, false);
    }
}

/**
 * Setup function.
 * Called once MySensors has successfully initialized.
 */
void setup() {
    int res;

    /* Setup serial */
    Serial.begin(115200);
    Serial.println(" [i] Hello world.");

    /* Setup linky */
    res = 0;
    res |= m_linky.setup(CONFIG_LINKY_DATA_PIN, CONFIG_LINKY_DUMMY_PIN);
    res |= m_linky.begin();
    if (res < 0) {
        setup_failed(" [e] Failed to communicate with linky!");
    }
}

/**
 * MySensors function called to describe this sensor and its capabilites.
 */
void presentation() {
    int res = 1;
    do {
        res &= sendSketchInfo("SLHA00011 Linky", "0.1.1");
        res &= present(0, S_INFO, "Numéro de Série");       // V_TEXT (ADCO, ADSC)
        res &= present(1, S_POWER, "Index Base");           // V_KWH (BASE)
        res &= present(2, S_POWER, "Puissance Apparente");  // V_WATT (PAPP)
        res &= present(3, S_MULTIMETER, "Phase 1");         // V_VOLTAGE (URMS1) and V_CURRENT (IINST, IINST1, IRMS1)
        res &= present(4, S_MULTIMETER, "Phase 2");         // V_VOLTAGE (URMS2) and V_CURRENT (IINST2, IRMS2)
        res &= present(5, S_MULTIMETER, "Phase 3");         // V_VOLTAGE (URMS3) and V_CURRENT (IINST3, IRMS3)
    } while (res == 0);
}

/**
 * MySensors function called when a message is received.
 */
void receive(const MyMessage &message) {
}

/**
 * Main loop.
 */
void loop() {
    int res;

    /* Led task */
    static bool linky_valid = false;
    static uint32_t m_led_timestamp = 0;
    static enum {
        STATE_0,
        STATE_1,
        STATE_2,
        STATE_3,
        STATE_4,
        STATE_5,
        STATE_6,
    } m_led_sm;
    switch (m_led_sm) {
        case STATE_0: {
            if (linky_valid) {
                m_led_sm = STATE_1;
            } else {
                m_led_sm = STATE_4;
            }
            break;
        }
        case STATE_1: {
            digitalWrite(CONFIG_LED_LINKY_RED_PIN, LOW);
            digitalWrite(CONFIG_LED_LINKY_GREEN_PIN, HIGH);
            m_led_timestamp = millis();
            m_led_sm = STATE_2;
            break;
        }
        case STATE_2: {
            if (millis() - m_led_timestamp >= 100) {
                digitalWrite(CONFIG_LED_LINKY_GREEN_PIN, LOW);
                m_led_sm = STATE_3;
            }
            break;
        }
        case STATE_3: {
            if (millis() - m_led_timestamp >= 3000) {
                m_led_sm = STATE_0;
            }
            break;
        }
        case STATE_4: {
            digitalWrite(CONFIG_LED_LINKY_GREEN_PIN, LOW);
            digitalWrite(CONFIG_LED_LINKY_RED_PIN, HIGH);
            m_led_timestamp = millis();
            m_led_sm = STATE_5;
            break;
        }
        case STATE_5: {
            if (millis() - m_led_timestamp >= 100) {
                digitalWrite(CONFIG_LED_LINKY_RED_PIN, LOW);
                m_led_sm = STATE_6;
            }
            break;
        }
        case STATE_6: {
            if (millis() - m_led_timestamp >= 1000) {
                m_led_sm = STATE_0;
            }
            break;
        }
    }

    /* Linky task */
    struct linky_tic::dataset dataset = {0};
    res = m_linky.dataset_get(dataset);
    if (res < 0) {
        Serial.println(" [e] Linky error!");
        linky_valid = false;
    } else if (res == 1) {
        Serial.printf(" [d] Received dataset %s = %s\r\n", dataset.name, dataset.data);
        linky_valid = true;

        /* Serial number */
        if (strcmp(dataset.name, "ADCO") == 0 || strcmp(dataset.name, "ADSC") == 0) {
            static bool initial_sent = false;
            if (initial_sent == false) {
                char linky_serial[12 + 1] = "";
                strncpy(linky_serial, dataset.data, 12);
                if (send(m_message_info_serial.set(linky_serial)) == true) {
                    initial_sent = true;
                }
            }
        }

        /* Index for base */
        else if (strcmp(dataset.name, "BASE") == 0) {
            static bool initial_sent = false;
            static uint32_t base_wh_last = 0;
            uint32_t base_wh = 0;
            for (size_t i = 0; i < 9; i++) {
                if (dataset.data[i] >= '0' && dataset.data[i] <= '9') {
                    base_wh *= 10;
                    base_wh += dataset.data[i] - '0';
                } else {
                    break;
                }
            }
            if (base_wh > base_wh_last || initial_sent == false) {
                if (send(m_message_power_kwh.set(base_wh / 1000.0, 3)) == true) {
                    initial_sent = true;
                    base_wh_last = base_wh;
                }
            }
        }

        /* Puissance apparente */
        else if (strcmp(dataset.name, "PAPP") == 0) {
            static bool initial_sent = false;
            static uint32_t power_va_last = 0;
            uint32_t power_va = 0;
            for (size_t i = 0; i < 5; i++) {
                if (dataset.data[i] >= '0' && dataset.data[i] <= '9') {
                    power_va *= 10;
                    power_va += dataset.data[i] - '0';
                } else {
                    break;
                }
            }
            if (power_va != power_va_last || initial_sent == false) {
                if (send(m_message_power_watt.set(power_va)) == true) {
                    initial_sent = true;
                    power_va_last = power_va;
                }
            }
        }

        /* Intensité Phase 1 */
        else if (strcmp(dataset.name, "IINST") == 0 || strcmp(dataset.name, "IINST1") == 0 || strcmp(dataset.name, "IRMS1") == 0) {
            static bool initial_sent = false;
            static uint32_t current_a_last = 0;
            uint32_t current_a = 0;
            for (size_t i = 0; i < 3; i++) {
                if (dataset.data[i] >= '0' && dataset.data[i] <= '9') {
                    current_a *= 10;
                    current_a += dataset.data[i] - '0';
                } else {
                    break;
                }
            }
            if (current_a != current_a_last || initial_sent == false) {
                if (send(m_message_phase1_current.set(current_a)) == true) {
                    initial_sent = true;
                    current_a_last = current_a;
                }
            }
        }

        /* Tension Phase 1 */
        else if (strcmp(dataset.name, "URMS1") == 0) {
            static bool initial_sent = false;
            static uint16_t voltage_v_last = 0;
            uint16_t voltage_a = 0;
            for (size_t i = 0; i < 3; i++) {
                if (dataset.data[i] >= '0' && dataset.data[i] <= '9') {
                    voltage_a *= 10;
                    voltage_a += dataset.data[i] - '0';
                } else {
                    break;
                }
            }
            if (voltage_a != voltage_v_last || initial_sent == false) {
                if (send(m_message_phase1_voltage.set(voltage_a)) == true) {
                    initial_sent = true;
                    voltage_v_last = voltage_a;
                }
            }
        }

        /* Intensité Phase 2 */
        else if (strcmp(dataset.name, "IINST2") == 0 || strcmp(dataset.name, "IRMS2") == 0) {
            static bool initial_sent = false;
            static uint32_t current_a_last = 0;
            uint32_t current_a = 0;
            for (size_t i = 0; i < 3; i++) {
                if (dataset.data[i] >= '0' && dataset.data[i] <= '9') {
                    current_a *= 10;
                    current_a += dataset.data[i] - '0';
                } else {
                    break;
                }
            }
            if (current_a != current_a_last || initial_sent == false) {
                if (send(m_message_phase2_current.set(current_a)) == true) {
                    initial_sent = true;
                    current_a_last = current_a;
                }
            }
        }

        /* Tension Phase 2 */
        else if (strcmp(dataset.name, "URMS2") == 0) {
            static bool initial_sent = false;
            static uint16_t voltage_v_last = 0;
            uint16_t voltage_a = 0;
            for (size_t i = 0; i < 3; i++) {
                if (dataset.data[i] >= '0' && dataset.data[i] <= '9') {
                    voltage_a *= 10;
                    voltage_a += dataset.data[i] - '0';
                } else {
                    break;
                }
            }
            if (voltage_a != voltage_v_last || initial_sent == false) {
                if (send(m_message_phase2_voltage.set(voltage_a)) == true) {
                    initial_sent = true;
                    voltage_v_last = voltage_a;
                }
            }
        }

        /* Intensité Phase 3 */
        else if (strcmp(dataset.name, "IINST3") == 0 || strcmp(dataset.name, "IRMS3") == 0) {
            static bool initial_sent = false;
            static uint32_t current_a_last = 0;
            uint32_t current_a = 0;
            for (size_t i = 0; i < 3; i++) {
                if (dataset.data[i] >= '0' && dataset.data[i] <= '9') {
                    current_a *= 10;
                    current_a += dataset.data[i] - '0';
                } else {
                    break;
                }
            }
            if (current_a != current_a_last || initial_sent == false) {
                if (send(m_message_phase3_current.set(current_a)) == true) {
                    initial_sent = true;
                    current_a_last = current_a;
                }
            }
        }

        /* Tension Phase 3 */
        else if (strcmp(dataset.name, "URMS3") == 0) {
            static bool initial_sent = false;
            static uint16_t voltage_v_last = 0;
            uint16_t voltage_a = 0;
            for (size_t i = 0; i < 3; i++) {
                if (dataset.data[i] >= '0' && dataset.data[i] <= '9') {
                    voltage_a *= 10;
                    voltage_a += dataset.data[i] - '0';
                } else {
                    break;
                }
            }
            if (voltage_a != voltage_v_last || initial_sent == false) {
                if (send(m_message_phase3_voltage.set(voltage_a)) == true) {
                    initial_sent = true;
                    voltage_v_last = voltage_a;
                }
            }
        }
    }
}
