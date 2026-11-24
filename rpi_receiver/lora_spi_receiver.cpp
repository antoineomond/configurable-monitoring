/*
 LoRa receiver for a Raspberry Pi Zero W + SX1262, using RadioLib.

 The sender is a Pico 2 + SX1262 using the Semtech sx126x_driver. The radio
 settings mirror the ones in common.c and
 sx126x_driver/configuration/apps_configuration.h, so both ends can talk
 to each other.

 The payload is decoded as a sequence of 4-byte little-endian uint32_t, each
 holding a temperature in hundredths of a degree (value / 100.0).
*/

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include <RadioLib.h>
#include "hal/RPi/PiHal.h"

/*
 Wiring (BCM GPIO numbers, physical header pin in brackets)
   SX1262 MOSI -> GPIO10 (19)  SPI0 MOSI
   SX1262 MISO -> GPIO9  (21)  SPI0 MISO
   SX1262 SCK  -> GPIO11 (23)  SPI0 SCLK
   SX1262 NSS  -> GPIO8  (24)  SPI0 CE0
   SX1262 DIO1 -> GPIO17 (11)
   SX1262 BUSY -> GPIO27 (13)
   SX1262 NRST -> GPIO22 (15)
   VCC -> 3.3V (1), GND -> GND (6)
*/
#ifndef LORA_SPI_CHANNEL
#define LORA_SPI_CHANNEL 0 // 0 = CE0 (GPIO8), 1 = CE1 (GPIO7)
#endif
#ifndef LORA_SPI_SPEED_HZ
#define LORA_SPI_SPEED_HZ 2000000
#endif
#ifndef LORA_NSS_PIN
#define LORA_NSS_PIN RADIOLIB_NC // must match the CE line of LORA_SPI_CHANNEL
#endif
#ifndef LORA_DIO1_PIN
#define LORA_DIO1_PIN 17
#endif
#ifndef LORA_BUSY_PIN
#define LORA_BUSY_PIN 27
#endif
#ifndef LORA_RESET_PIN
#define LORA_RESET_PIN 22
#endif

// Radio configuration, must match the Pico 2 sender (common.c)
#ifndef FREQ_HZ
#define FREQ_HZ 920000000
#endif
#ifndef LORA_SPREADING_FACTOR
#define LORA_SPREADING_FACTOR 7 // SF7
#endif
#ifndef LORA_BANDWIDTH_HZ
#define LORA_BANDWIDTH_HZ 125000 // BW 125 kHz
#endif
#ifndef LORA_CODING_RATE
#define LORA_CODING_RATE 5 // CR 4/5
#endif
#ifndef LORA_LDRO
#define LORA_LDRO true // low data rate optimization forced to 0x01
#endif
#ifndef LORA_PREAMBLE_LENGTH
#define LORA_PREAMBLE_LENGTH 8
#endif
#ifndef LORA_CRC_LEN
#define LORA_CRC_LEN 0 // CRC disabled
#endif
#ifndef LORA_IQ_INVERTED
#define LORA_IQ_INVERTED false
#endif
#ifndef LORA_SYNCWORD
#define LORA_SYNCWORD 0x12 // private network, same as LORA_SYNCWORD_PRIVATE_NTW
#endif

// Board-specific settings of the SX1262 module
#ifndef LORA_TCXO_VOLTAGE
#define LORA_TCXO_VOLTAGE Voltage3V3 // VoltageNone if the module uses a crystal
#endif
#ifndef LORA_DIO2_AS_RF_SWITCH
#define LORA_DIO2_AS_RF_SWITCH true
#endif

#ifndef MAX_PAYLOAD_LENGTH
#define MAX_PAYLOAD_LENGTH 255
#endif
#ifndef POLL_PERIOD_MS
#define POLL_PERIOD_MS 10
#endif

#define CHECK_RADIOLIB(fc, fname)                                    \
	{                                                                  \
		int16_t state = (fc);                                            \
		if(state != RADIOLIB_ERR_NONE) {                                 \
			fprintf(stderr, "%s failed, code %d\n", fname, state);         \
			return 1;                                                      \
		}                                                                \
	}

PiHal* hal = new PiHal(LORA_SPI_CHANNEL, LORA_SPI_SPEED_HZ);
SX1262 radio = new Module(hal, LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RESET_PIN, LORA_BUSY_PIN);

// Set from the lgpio alert thread when DIO1 rises (RxDone)
volatile bool received_msg = false;
void dio1_callback(void) {
	received_msg = true;
}

int main() {
	// Settings applied by begin(): reset, packet type LoRa, TCXO, DIO2 RF switch
	radio.tcxoVoltage = LORA_TCXO_VOLTAGE;
	ConfigLoRa_t cfg;
	cfg.frequency       = FREQ_HZ;
	cfg.bandwidth       = LORA_BANDWIDTH_HZ;
	cfg.spreadingFactor = LORA_SPREADING_FACTOR;
	cfg.codingRate      = LORA_CODING_RATE;
	cfg.syncWord        = LORA_SYNCWORD;
	cfg.preambleLength  = LORA_PREAMBLE_LENGTH;
	printf("[SX1262] Initializing ... ");
	CHECK_RADIOLIB(radio.begin(cfg), "begin");
	printf("success!\n");

	// Same sequence as sx1262_lora_init() in common.c
	CHECK_RADIOLIB(radio.standby(RADIOLIB_SX126X_STANDBY_RC), "standby");
	CHECK_RADIOLIB(radio.setFrequency(FREQ_HZ), "setFrequency");
	CHECK_RADIOLIB(radio.setSpreadingFactor(LORA_SPREADING_FACTOR), "setSpreadingFactor");
	CHECK_RADIOLIB(radio.setBandwidth(LORA_BANDWIDTH_HZ), "setBandwidth");
	CHECK_RADIOLIB(radio.setCodingRate(LORA_CODING_RATE), "setCodingRate");
	CHECK_RADIOLIB(radio.forceLDRO(LORA_LDRO), "forceLDRO"); // after SF/BW, disables auto LDRO
	CHECK_RADIOLIB(radio.setPreambleLength(LORA_PREAMBLE_LENGTH), "setPreambleLength");
	CHECK_RADIOLIB(radio.explicitHeader(), "explicitHeader");
	CHECK_RADIOLIB(radio.setCRC(LORA_CRC_LEN), "setCRC");
	CHECK_RADIOLIB(radio.invertIQ(LORA_IQ_INVERTED), "invertIQ");
	CHECK_RADIOLIB(radio.setSyncWord(LORA_SYNCWORD), "setSyncWord");
	CHECK_RADIOLIB(radio.setDio2AsRfSwitch(LORA_DIO2_AS_RF_SWITCH), "setDio2AsRfSwitch");

	// Continuous RX: the radio stays in RX after each RxDone
	radio.setDio1Action(dio1_callback);
	CHECK_RADIOLIB(radio.startReceive(), "startReceive");
	printf("[SX1262] Listening on %.1f MHz ...\n", FREQ_HZ / 1e6);

	uint8_t buffer[MAX_PAYLOAD_LENGTH];
	uint32_t nb_msg_received = 0;
	uint32_t temperature;
	while(1) {
		if(received_msg) {
			received_msg = false;
			size_t len = radio.getPacketLength();
			int16_t state = radio.readData(buffer, len);
			if(state == RADIOLIB_ERR_NONE) {
				nb_msg_received += 1;
				// Little-endian, as written by the sender
				for(size_t i = 0; i + 4 <= len; i += 4) {
					temperature = buffer[i] | (buffer[i + 1] << 8) | (buffer[i + 2] << 16) | ((uint32_t)buffer[i + 3] << 24);
					printf("%.2f,", temperature / 100.0);
				}
				printf("%" PRIu32 ",len=%zu,rssi=%.1f,snr=%.1f\n",
				       nb_msg_received, len, radio.getRSSI(), radio.getSNR());
			} else {
				fprintf(stderr, "readData failed, code %d\n", state);
			}
		}
		hal->delay(POLL_PERIOD_MS);
	}
	return 0;
}
