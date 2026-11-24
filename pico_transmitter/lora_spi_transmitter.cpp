/*
 LoRa transmitter for a Raspberry Pi Pico 2 + SX1262, using RadioLib
 and its PicoHal.

 The receiver is rpi_receiver/lora_spi_receiver.cpp (Pi Zero W). Both ends
 must use the same radio settings below.

 The payload is a 4-byte little-endian uint32_t holding the RP2350 on-chip
 temperature in hundredths of a degree, which is what the receiver decodes
 (value / 100.0).
*/

#include <stdio.h>
#include <stdint.h>

#include <pico/stdlib.h>
#include "hardware/adc.h"

#include <RadioLib.h>
#include "hal/RPiPico/PicoHal.h"

/*
 Wiring (Pico 2 GPIO numbers, same as the previous sx126x_driver setup:
 PICO_DEFAULT_SPI_* pins of pico2.h and common.h)
   SX1262 MISO -> GP16  SPI0 RX
   SX1262 NSS  -> GP17  SPI0 CSn
   SX1262 SCK  -> GP18  SPI0 SCK
   SX1262 MOSI -> GP19  SPI0 TX
   SX1262 DIO1 -> GP20
   SX1262 BUSY -> GP8
   SX1262 NRST -> GP9
*/
#ifndef LORA_SPI_PORT
#define LORA_SPI_PORT spi0
#endif
#ifndef LORA_SPI_SPEED_HZ
#define LORA_SPI_SPEED_HZ 2000000
#endif
#ifndef LORA_MISO_PIN
#define LORA_MISO_PIN 16
#endif
#ifndef LORA_NSS_PIN
#define LORA_NSS_PIN 17
#endif
#ifndef LORA_SCK_PIN
#define LORA_SCK_PIN 18
#endif
#ifndef LORA_MOSI_PIN
#define LORA_MOSI_PIN 19
#endif
#ifndef LORA_DIO1_PIN
#define LORA_DIO1_PIN 20
#endif
#ifndef LORA_BUSY_PIN
#define LORA_BUSY_PIN 8
#endif
#ifndef LORA_RESET_PIN
#define LORA_RESET_PIN 9
#endif

// Radio configuration, must match the Pi Zero receiver
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
#define LORA_SYNCWORD 0x12 // private network
#endif
#ifndef TX_OUTPUT_POWER_DBM
#define TX_OUTPUT_POWER_DBM 14 // range [-9, +22]
#endif

// Board-specific settings of the SX1262 module
#ifndef LORA_TCXO_VOLTAGE
#define LORA_TCXO_VOLTAGE Voltage3V3 // VoltageNone if the module uses a crystal
#endif
#ifndef LORA_DIO2_AS_RF_SWITCH
#define LORA_DIO2_AS_RF_SWITCH true
#endif

#ifndef PAYLOAD_LENGTH
#define PAYLOAD_LENGTH 4
#endif
#ifndef TX_PERIOD_MS
#define TX_PERIOD_MS 1000
#endif

#define CHECK_RADIOLIB(fc, fname)                                    \
	{                                                                  \
		int16_t state = (fc);                                            \
		if(state != RADIOLIB_ERR_NONE) {                                 \
			printf("%s failed, code %d\n", fname, state);                  \
			return 1;                                                      \
		}                                                                \
	}

PicoHal* hal = new PicoHal(LORA_SPI_PORT, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_SCK_PIN, LORA_SPI_SPEED_HZ);
SX1262 radio = new Module(hal, LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RESET_PIN, LORA_BUSY_PIN);

// Returns the on-chip temperature in hundredths of a degree Celsius
uint32_t read_chip_temperature() {
	const float conversion_factor = 3.3f / (1 << 12);
	float voltage = adc_read() * conversion_factor;
	float celsius = 27.0f - (voltage - 0.706f) / 0.001721f;
	return (uint32_t)(int32_t)(celsius * 100.0f);
}

int main() {
	stdio_init_all();
	sleep_ms(1000); // leave time for the USB serial to come up

	adc_init();
	adc_set_temp_sensor_enabled(true);
	adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);

	// Settings applied by begin(): reset, packet type LoRa, TCXO, DIO2 RF switch
	radio.tcxoVoltage = LORA_TCXO_VOLTAGE;
	ConfigLoRa_t cfg;
	cfg.frequency       = FREQ_HZ;
	cfg.bandwidth       = LORA_BANDWIDTH_HZ;
	cfg.spreadingFactor = LORA_SPREADING_FACTOR;
	cfg.codingRate      = LORA_CODING_RATE;
	cfg.syncWord        = LORA_SYNCWORD;
	cfg.power           = TX_OUTPUT_POWER_DBM;
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
	CHECK_RADIOLIB(radio.setOutputPower(TX_OUTPUT_POWER_DBM), "setOutputPower");

	uint8_t buffer[PAYLOAD_LENGTH];
	uint32_t nb_msg_sent = 0;
	uint32_t temperature;
	while(1) {
		temperature = read_chip_temperature();
		// Little-endian, as decoded by the receiver
		buffer[0] = temperature & 0xFF;
		buffer[1] = (temperature >> 8) & 0xFF;
		buffer[2] = (temperature >> 16) & 0xFF;
		buffer[3] = (temperature >> 24) & 0xFF;

		int16_t state = radio.transmit(buffer, PAYLOAD_LENGTH);
		if(state == RADIOLIB_ERR_NONE) {
			nb_msg_sent += 1;
			printf("sent %.2f, msg_sent: %lu\n", (int32_t)temperature / 100.0, (unsigned long)nb_msg_sent);
		} else {
			printf("transmit failed, code %d\n", state);
		}
		hal->delay(TX_PERIOD_MS);
	}
	return 0;
}
