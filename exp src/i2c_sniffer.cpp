// i2c_sniffer.cpp

#include "i2c_sniffer.h"
#include <Arduino.h>
#include <queue>

static uint8_t SDA_PIN = 21, SCL_PIN = 22; // Defaults, will be set in begin()
static std::queue<I2C_Event> eventQueue;

void I2C_Sniffer::begin(uint8_t sdaPin, uint8_t sclPin) {
    SDA_PIN = sdaPin;
    SCL_PIN = sclPin;
    pinMode(SDA_PIN, INPUT);
    pinMode(SCL_PIN, INPUT);
}

// Internal state for passive decode
static int prevSDA = 1, prevSCL = 1;
static bool started = false;
static int bitCount = 0;
static uint8_t byteVal = 0;
static bool expectingAck = false;
static bool expectingAddress = false;  // <--- NEW

static void decodeI2C() {
    int sda = digitalRead(SDA_PIN);
    int scl = digitalRead(SCL_PIN);

    // START condition
    if (prevSDA == 1 && sda == 0 && scl == 1) {
        I2C_Event evt = {I2C_Event::START, 0, false};
        eventQueue.push(evt);
        started = true;
        bitCount = 0;
        byteVal = 0;
        expectingAck = false;
        expectingAddress = true; // <--- ADDRESS is next byte after START
    }
    // STOP condition
    if (prevSDA == 0 && sda == 1 && scl == 1) {
        I2C_Event evt = {I2C_Event::STOP, 0, false};
        eventQueue.push(evt);
        started = false;
        bitCount = 0;
        byteVal = 0;
        expectingAck = false;
        expectingAddress = false;
    }
    // Bit clock
    if (started && prevSCL == 0 && scl == 1) {
        if (expectingAck) {
            I2C_Event evt;
            if (sda == 0) {
                evt = {I2C_Event::ACK, 0, false};
            } else {
                evt = {I2C_Event::NACK, 0, false};
            }
            eventQueue.push(evt);
            expectingAck = false;
            bitCount = 0;
            byteVal = 0;
        } else {
            byteVal = (byteVal << 1) | (sda & 1);
            bitCount++;
            if (bitCount == 8) {
                I2C_Event evt;
                if (expectingAddress) {
                    evt.type = I2C_Event::ADDRESS; // <--- Properly mark address byte
                    evt.value = byteVal;
                    evt.isRead = (byteVal & 1);
                    expectingAddress = false;
                } else {
                    evt.type = I2C_Event::DATA;
                    evt.value = byteVal;
                    evt.isRead = false;
                }
                eventQueue.push(evt);
                expectingAck = true;
                bitCount = 0;
                byteVal = 0;
            }
        }
    }

    prevSDA = sda;
    prevSCL = scl;
}

bool I2C_Sniffer::getNextEvent(I2C_Event &evt) {
    // Run the decoder a few times per call to keep up
    for (int i = 0; i < 64; ++i) decodeI2C();

    if (!eventQueue.empty()) {
        evt = eventQueue.front();
        eventQueue.pop();
        return true;
    }
    return false;
}
