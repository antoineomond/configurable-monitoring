/*
 LoRa transmitter for a Raspberry Pi Zero W + SX1262, using RadioLib.

 The radio settings mirror the ones used by lora_spi_receiver.c
 (see common.c and sx126x_driver/configuration/apps_configuration.h),
 so both ends can talk to each other.

 The payload is a 4-byte little-endian uint32_t holding a temperature in
 hundredths of a degree, which is what the receiver decodes
 (value / 100.0). The Pi CPU temperature is used as the data source.
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
#define LORA_NSS_PIN 8 // must match the CE line of LORA_SPI_CHANNEL
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

// Radio configuration, must match lora_spi_receiver.c
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
#ifndef CPU_TEMP_PATH
#define CPU_TEMP_PATH "/sys/class/thermal/thermal_zone0/temp"
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

// Returns the CPU temperature in hundredths of a degree Celsius
bool read_cpu_temperature(uint32_t* temperature) {
	FILE* f = fopen(CPU_TEMP_PATH, "r");
	if(!f) {
		return false;
	}
	long millideg;
	bool ok = fscanf(f, "%ld", &millideg) == 1;
	fclose(f);
	if(ok) {
		*temperature = (uint32_t)(millideg / 10);
	}
	return ok;
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
		if(!read_cpu_temperature(&temperature)) {
			fprintf(stderr, "could not read %s\n", CPU_TEMP_PATH);
			temperature = 0;
		}
		// Little-endian, as decoded by the receiver
		buffer[0] = temperature & 0xFF;
		buffer[1] = (temperature >> 8) & 0xFF;
		buffer[2] = (temperature >> 16) & 0xFF;
		buffer[3] = (temperature >> 24) & 0xFF;

		int16_t state = radio.transmit(buffer, PAYLOAD_LENGTH);
		if(state == RADIOLIB_ERR_NONE) {
			nb_msg_sent += 1;
			printf("sent %.2f, msg_sent: %" PRIu32 "\n", temperature / 100.0, nb_msg_sent);
		} else {
			fprintf(stderr, "transmit failed, code %d\n", state);
		}
		hal->delay(TX_PERIOD_MS);
	}
	return 0;
}
