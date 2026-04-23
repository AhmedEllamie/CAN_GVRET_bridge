/*
 * SDTS V2 Sniffer - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    src/gvret_comm.cpp
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 *
 * Implements the GVRET comm protocol (send + receive). Shared by the USB and
 * WiFi transports; each transport instantiates its own handler.
 */

#include "gvret_comm.h"
#include "SerialConsole.h"
#include "config.h"
#include "can_manager.h"
#include "config_service.h"

GVRET_Comm_Handler::GVRET_Comm_Handler() : out_bus(0), step(0), state(IDLE), build_int(0) {}

void GVRET_Comm_Handler::processIncomingByte(uint8_t in_byte) {
    uint32_t busSpeed = 0;
    uint32_t now = micros();

    uint8_t temp8;
    uint16_t temp16;

    switch (state) {
    case IDLE:
        if (in_byte == 0xF1) {
            state = GET_COMMAND;
        } else if (in_byte == 0xE7) {
            // Host asked the link to switch to binary framing (SavvyCAN handshake).
            settings.serialMode = SERIAL_MODE_BINARY;
        } else {
            console.rcvCharacter(in_byte);
        }
        break;

    case GET_COMMAND:
        switch (in_byte) {
        case PROTO_BUILD_CAN_FRAME:
            state = BUILD_CAN_FRAME;
            buff[0] = 0xF1;
            step = 0;
            break;
        case PROTO_TIME_SYNC:
            state = TIME_SYNC;
            step = 0;
            sendByteToBuffer(0xF1);
            sendByteToBuffer(1);
            sendByteToBuffer((uint8_t)(now & 0xFF));
            sendByteToBuffer((uint8_t)(now >> 8));
            sendByteToBuffer((uint8_t)(now >> 16));
            sendByteToBuffer((uint8_t)(now >> 24));
            break;
        case PROTO_DIG_INPUTS:
            temp8 = 0;
            sendByteToBuffer(0xF1);
            sendByteToBuffer(2);
            sendByteToBuffer(temp8);
            sendByteToBuffer(0);
            state = IDLE;
            break;
        case PROTO_ANA_INPUTS:
            sendByteToBuffer(0xF1);
            sendByteToBuffer(3);
            for (int i = 0; i < 7; i++) {
                temp16 = 0;
                sendByteToBuffer(temp16 & 0xFF);
                sendByteToBuffer((uint8_t)(temp16 >> 8));
            }
            sendByteToBuffer(0);
            state = IDLE;
            break;
        case PROTO_SET_DIG_OUT:
            state = SET_DIG_OUTPUTS;
            buff[0] = 0xF1;
            break;
        case PROTO_SETUP_CANBUS:
            state = SETUP_CANBUS;
            step = 0;
            buff[0] = 0xF1;
            break;
        case PROTO_GET_CANBUS_PARAMS:
            sendByteToBuffer(0xF1);
            sendByteToBuffer(6);
            sendByteToBuffer(settings.CAN0_Enabled + ((uint8_t)settings.CAN0ListenOnly << 4));
            sendByteToBuffer((uint8_t)(settings.CAN0Speed));
            sendByteToBuffer((uint8_t)(settings.CAN0Speed >> 8));
            sendByteToBuffer((uint8_t)(settings.CAN0Speed >> 16));
            sendByteToBuffer((uint8_t)(settings.CAN0Speed >> 24));
            sendByteToBuffer(settings.CAN1_Enabled + ((uint8_t)settings.CAN1ListenOnly << 4));
            sendByteToBuffer((uint8_t)(settings.CAN1Speed));
            sendByteToBuffer((uint8_t)(settings.CAN1Speed >> 8));
            sendByteToBuffer((uint8_t)(settings.CAN1Speed >> 16));
            sendByteToBuffer((uint8_t)(settings.CAN1Speed >> 24));
            state = IDLE;
            break;
        case PROTO_GET_DEV_INFO:
            sendByteToBuffer(0xF1);
            sendByteToBuffer(7);
            sendByteToBuffer(CFG_BUILD_NUM & 0xFF);
            sendByteToBuffer((CFG_BUILD_NUM >> 8));
            sendByteToBuffer(0x20);
            sendByteToBuffer(0);
            sendByteToBuffer(0);
            sendByteToBuffer(0);
            state = IDLE;
            break;
        case PROTO_SET_SW_MODE:
            buff[0] = 0xF1;
            state = SET_SINGLEWIRE_MODE;
            step = 0;
            break;
        case PROTO_KEEPALIVE:
            sendByteToBuffer(0xF1);
            sendByteToBuffer(0x09);
            sendByteToBuffer(0xDE);
            sendByteToBuffer(0xAD);
            state = IDLE;
            break;
        case PROTO_SET_SYSTYPE:
            buff[0] = 0xF1;
            state = SET_SYSTYPE;
            step = 0;
            break;
        case PROTO_ECHO_CAN_FRAME:
            state = ECHO_CAN_FRAME;
            buff[0] = 0xF1;
            step = 0;
            break;
        case PROTO_GET_NUMBUSES:
            sendByteToBuffer(0xF1);
            sendByteToBuffer(12);
            sendByteToBuffer(SysSettings.numBuses);
            state = IDLE;
            break;
        case PROTO_GET_EXT_BUSES:
            sendByteToBuffer(0xF1);
            sendByteToBuffer(13);
            for (int u = 2; u < 17; u++) sendByteToBuffer(0);
            step = 0;
            state = IDLE;
            break;
        case PROTO_SET_EXT_BUSES:
            state = SETUP_EXT_BUSES;
            step = 0;
            buff[0] = 0xF1;
            break;
        }
        break;

    case BUILD_CAN_FRAME:
        buff[1 + step] = in_byte;
        switch (step) {
        case 0: build_out_frame.id = in_byte; break;
        case 1: build_out_frame.id |= in_byte << 8; break;
        case 2: build_out_frame.id |= in_byte << 16; break;
        case 3:
            build_out_frame.id |= in_byte << 24;
            if (build_out_frame.id & (1u << 31)) {
                build_out_frame.id &= 0x7FFFFFFF;
                build_out_frame.extended = true;
            } else {
                build_out_frame.extended = false;
            }
            break;
        case 4: out_bus = in_byte & 3; break;
        case 5:
            build_out_frame.length = in_byte & 0xF;
            if (build_out_frame.length > 8) build_out_frame.length = 8;
            break;
        default:
            if (step < build_out_frame.length + 6) {
                build_out_frame.data.uint8[step - 6] = in_byte;
            } else {
                state = IDLE;
                build_out_frame.rtr = 0;
                if (out_bus == 0) canManager.sendFrame(&CAN0, build_out_frame);
                if (out_bus == 1) canManager.sendFrame(&CAN1, build_out_frame);
            }
            break;
        }
        step++;
        break;

    case TIME_SYNC:
        state = IDLE;
        break;

    case SET_DIG_OUTPUTS:
        buff[1] = in_byte;
        for (int c = 0; c < 8; c++) setOutput(c, (in_byte & (1 << c)) != 0);
        state = IDLE;
        break;

    case SETUP_CANBUS:
        switch (step) {
        case 0: build_int  = in_byte;       break;
        case 1: build_int |= in_byte << 8;  break;
        case 2: build_int |= in_byte << 16; break;
        case 3:
            build_int |= in_byte << 24;
            busSpeed = build_int & 0xFFFFF;
            if (busSpeed > 1000000) busSpeed = 1000000;
            if (build_int > 0) {
                if (build_int & 0x80000000ul) {
                    settings.CAN0_Enabled   = (build_int & 0x40000000ul) != 0;
                    settings.CAN0ListenOnly = (build_int & 0x20000000ul) != 0;
                } else {
                    settings.CAN0_Enabled = true;
                }
                settings.CAN0Speed = busSpeed;
            } else {
                settings.CAN0_Enabled = false;
            }
            // Sync the canSelection enum so web UI stays consistent.
            if (settings.CAN0_Enabled && settings.CAN1_Enabled)      settings.canSelection = CAN_SEL_BOTH;
            else if (settings.CAN0_Enabled)                          settings.canSelection = CAN_SEL_CAN0;
            else if (settings.CAN1_Enabled)                          settings.canSelection = CAN_SEL_CAN1;
            else                                                     settings.canSelection = CAN_SEL_NONE;
            ConfigService::applyRuntime();
            break;
        case 4: build_int  = in_byte;       break;
        case 5: build_int |= in_byte << 8;  break;
        case 6: build_int |= in_byte << 16; break;
        case 7:
            build_int |= in_byte << 24;
            busSpeed = build_int & 0xFFFFF;
            if (busSpeed > 1000000) busSpeed = 1000000;
            if (build_int > 0 && SysSettings.numBuses > 1) {
                if (build_int & 0x80000000ul) {
                    settings.CAN1_Enabled   = (build_int & 0x40000000ul) != 0;
                    settings.CAN1ListenOnly = (build_int & 0x20000000ul) != 0;
                } else {
                    settings.CAN1_Enabled = true;
                }
                settings.CAN1Speed = busSpeed;
            } else {
                settings.CAN1_Enabled = false;
            }
            if (settings.CAN0_Enabled && settings.CAN1_Enabled)      settings.canSelection = CAN_SEL_BOTH;
            else if (settings.CAN0_Enabled)                          settings.canSelection = CAN_SEL_CAN0;
            else if (settings.CAN1_Enabled)                          settings.canSelection = CAN_SEL_CAN1;
            else                                                     settings.canSelection = CAN_SEL_NONE;
            ConfigService::applyRuntime();
            ConfigService::save();
            state = IDLE;
            break;
        }
        step++;
        break;

    case SET_SINGLEWIRE_MODE:
        state = IDLE;
        break;

    case SET_SYSTYPE:
        // Legacy "system type" selector - no-op in the cleaned-up build.
        state = IDLE;
        break;

    case ECHO_CAN_FRAME:
        buff[1 + step] = in_byte;
        switch (step) {
        case 0: build_out_frame.id = in_byte; break;
        case 1: build_out_frame.id |= in_byte << 8; break;
        case 2: build_out_frame.id |= in_byte << 16; break;
        case 3:
            build_out_frame.id |= in_byte << 24;
            if (build_out_frame.id & (1u << 31)) {
                build_out_frame.id &= 0x7FFFFFFF;
                build_out_frame.extended = true;
            } else {
                build_out_frame.extended = false;
            }
            break;
        case 4: out_bus = in_byte & 1; break;
        case 5:
            build_out_frame.length = in_byte & 0xF;
            if (build_out_frame.length > 8) build_out_frame.length = 8;
            break;
        default:
            if (step < build_out_frame.length + 6) {
                build_out_frame.data.bytes[step - 6] = in_byte;
            } else {
                state = IDLE;
                canManager.displayFrame(build_out_frame, 0);
            }
            break;
        }
        step++;
        break;

    case SETUP_EXT_BUSES:
        switch (step) {
        case 0: build_int  = in_byte;       break;
        case 1: build_int |= in_byte << 8;  break;
        case 2: build_int |= in_byte << 16; break;
        case 3: build_int |= in_byte << 24; break;
        case 4: build_int  = in_byte;       break;
        case 5: build_int |= in_byte << 8;  break;
        case 6: build_int |= in_byte << 16; break;
        case 7: build_int |= in_byte << 24; break;
        case 8: build_int  = in_byte;       break;
        case 9: build_int |= in_byte << 8;  break;
        case 10: build_int |= in_byte << 16; break;
        case 11:
            build_int |= in_byte << 24;
            state = IDLE;
            break;
        }
        step++;
        break;

    default:
        state = IDLE;
        break;
    }
}

// XOR checksum - kept for protocol compatibility even though not strictly validated.
uint8_t GVRET_Comm_Handler::checksumCalc(uint8_t *buffer, int length) {
    uint8_t v = 0;
    for (int c = 0; c < length; c++) v ^= buffer[c];
    return v;
}
