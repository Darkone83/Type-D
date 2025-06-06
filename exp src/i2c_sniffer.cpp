// i2c_sniffer.cpp

#include "i2c_sniffer.h"
#include <driver/rmt.h>
#include <queue>
#include <Arduino.h>

#define I2C_SNIF_SDA_PIN 21
#define I2C_SNIF_SCL_PIN 22
#define RMT_RX_CHANNEL   RMT_CHANNEL_0
#define RMT_CLK_DIV      80    // 1 MHz (1us per tick)

static std::queue<I2C_Event> eventQueue;

// --- Helper functions ---
static int readSDA() { return digitalRead(I2C_SNIF_SDA_PIN); }
static int readSCL() { return digitalRead(I2C_SNIF_SCL_PIN); }

// --- I2C decoding state ---
static bool started = false;
static uint8_t bitcount = 0;
static uint8_t byteval = 0;
static bool reading = false;
static bool expectingAck = false;

// --- Setup I2C pin and RMT on SCL only ---
void I2C_Sniffer::begin(uint8_t sdaPin, uint8_t sclPin) {
    pinMode(sdaPin, INPUT_PULLUP);
    pinMode(sclPin, INPUT_PULLUP);

    // RMT only needed for advanced timing; here we use direct polling for simplicity.
    // For high speeds, consider using RMT as in delboy1978uk's project.
}

// --- Poll for I2C edges and decode protocol ---
bool I2C_Sniffer::getNextEvent(I2C_Event &evt) {
    static int prevSCL = 1;
    static int prevSDA = 1;
    static unsigned long lastCheck = 0;

    // Poll as fast as we can (can move to timer or RMT for speed)
    unsigned long now = micros();
    if (now - lastCheck < 10) return false; // ~100kHz sample
    lastCheck = now;

    int scl = readSCL();
    int sda = readSDA();

    // Detect START (SCL high, SDA falling)
    if ((scl == 1) && (prevSCL == 1) && (sda == 0) && (prevSDA == 1)) {
        started = true;
        bitcount = 0;
        byteval = 0;
        reading = false;
        expectingAck = false;

        evt.type = I2C_Event::START;
        evt.value = 0;
        evt.address = 0;
        evt.isRead = false;
        eventQueue.push(evt);
    }

    // Detect STOP (SCL high, SDA rising)
    if ((scl == 1) && (prevSCL == 1) && (sda == 1) && (prevSDA == 0)) {
        started = false;
        evt.type = I2C_Event::STOP;
        evt.value = 0;
        evt.address = 0;
        evt.isRead = false;
        eventQueue.push(evt);
    }

    // Only decode if started
    if (started) {
        // Sample on SCL rising edge
        if ((scl == 1) && (prevSCL == 0)) {
            // If expecting ACK/NACK
            if (expectingAck) {
                evt.type = (sda == 0) ? I2C_Event::ACK : I2C_Event::NACK;
                evt.value = 0;
                evt.address = 0;
                evt.isRead = false;
                eventQueue.push(evt);
                expectingAck = false;
                bitcount = 0;
                byteval = 0;
                reading = false;
            } else {
                // Shift in bit
                byteval = (byteval << 1) | (sda & 1);
                bitcount++;
                if (bitcount == 8) {
                    // First byte after START = address
                    if (!reading) {
                        evt.type = I2C_Event::ADDRESS;
                        evt.value = byteval;
                        evt.address = (byteval >> 1) & 0x7F;
                        evt.isRead = (byteval & 1);
                        eventQueue.push(evt);
                        reading = true;
                    } else {
                        evt.type = I2C_Event::DATA;
                        evt.value = byteval;
                        evt.address = 0;
                        evt.isRead = false;
                        eventQueue.push(evt);
                    }
                    expectingAck = true;
                    bitcount = 0;
                    byteval = 0;
                }
            }
        }
    }

    prevSCL = scl;
    prevSDA = sda;

    if (!eventQueue.empty()) {
        evt = eventQueue.front();
        eventQueue.pop();
        return true;
    }
    return false;
}
