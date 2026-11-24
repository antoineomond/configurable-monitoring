/*
 LoRa transmitter for a Raspberry Pi Pico 2 + SX1262, using RadioLib
 and its PicoHal. The transmitted data is read from a BME680 connected in SPI,
 using the Bosch BME68x_SensorAPI.

 The receiver is rpi_receiver/lora_spi_receiver.cpp (Pi Zero W). Both ends
 must use the same radio settings below.

 The payload is a 4-byte little-endian uint32_t holding the BME680 temperature
 in hundredths of a degree, which is what the receiver decodes (value / 100.0).
*/

#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include <pico/stdlib.h>
#include "hardware/spi.h"

#include <RadioLib.h>
#include "hal/RPiPico/PicoHal.h"

#include "bme68x.h"

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

 The BME680 shares SPI0 with the SX1262, with its own chip select:
   BME680 SDO  -> GP16  SPI0 RX
   BME680 CSB  -> GP5
   BME680 SCK  -> GP18  SPI0 SCK
   BME680 SDI  -> GP19  SPI0 TX
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

// When the bus is shared with the radio, RadioLib's speed applies once it is initialized
#ifndef BME_SPI_PORT
#define BME_SPI_PORT LORA_SPI_PORT
#endif
#ifndef BME_SPI_SPEED_HZ
#define BME_SPI_SPEED_HZ LORA_SPI_SPEED_HZ // max 10 MHz
#endif
#ifndef BME_MISO_PIN
#define BME_MISO_PIN LORA_MISO_PIN
#endif
#ifndef BME_CS_PIN
#define BME_CS_PIN 5
#endif
#ifndef BME_SCK_PIN
#define BME_SCK_PIN LORA_SCK_PIN
#endif
#ifndef BME_MOSI_PIN
#define BME_MOSI_PIN LORA_MOSI_PIN
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

#define CHECK_BME(fc, fname)                                         \
	{                                                                  \
		int8_t rslt = (fc);                                              \
		if(rslt != BME68X_OK) {                                          \
			printf("%s failed, code %d\n", fname, rslt);                   \
			return rslt;                                                   \
		}                                                                \
	}

PicoHal* hal = new PicoHal(LORA_SPI_PORT, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_SCK_PIN, LORA_SPI_SPEED_HZ);
SX1262 radio = new Module(hal, LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RESET_PIN, LORA_BUSY_PIN);

struct bme68x_dev bme;
struct bme68x_conf bme_conf;

// SPI callbacks for the BME68x_SensorAPI, which already sets the read/write bit of
// reg_addr and, for writes, interleaves the next register addresses in data
int8_t bme_spi_read(uint8_t reg_addr, uint8_t* data, uint32_t len, void* intf_ptr) {
	gpio_put(BME_CS_PIN, 0);
	int nb_written = spi_write_blocking(BME_SPI_PORT, &reg_addr, 1);
	int nb_read = spi_read_blocking(BME_SPI_PORT, 0x00, data, len);
	gpio_put(BME_CS_PIN, 1);
	return (nb_written == 1 && nb_read == (int)len) ? BME68X_INTF_RET_SUCCESS : BME68X_E_COM_FAIL;
}

int8_t bme_spi_write(uint8_t reg_addr, const uint8_t* data, uint32_t len, void* intf_ptr) {
	gpio_put(BME_CS_PIN, 0);
	int nb_written = spi_write_blocking(BME_SPI_PORT, &reg_addr, 1);
	nb_written += spi_write_blocking(BME_SPI_PORT, data, len);
	gpio_put(BME_CS_PIN, 1);
	return (nb_written == (int)len + 1) ? BME68X_INTF_RET_SUCCESS : BME68X_E_COM_FAIL;
}

void bme_delay_us(uint32_t period, void* intf_ptr) {
	sleep_us(period);
}

// Configures a chip select pin as an output, deselected
void init_cs_pin(uint pin) {
	gpio_init(pin);
	gpio_put(pin, 1); // before setting the direction, to not select the device
	gpio_set_dir(pin, GPIO_OUT);
}

// Must be called before radio.begin(): the BME680 listens in I2C until its CSB is first
// pulled down, so it could take the SPI traffic of the radio as I2C (datasheet, 6.1)
int8_t bme_setup() {
	init_cs_pin(LORA_NSS_PIN); // keep the radio off the shared bus
	init_cs_pin(BME_CS_PIN);
	spi_init(BME_SPI_PORT, BME_SPI_SPEED_HZ); // SPI mode 0, MSB first
	gpio_set_function(BME_MISO_PIN, GPIO_FUNC_SPI);
	gpio_set_function(BME_SCK_PIN, GPIO_FUNC_SPI);
	gpio_set_function(BME_MOSI_PIN, GPIO_FUNC_SPI);

	bme.intf     = BME68X_SPI_INTF;
	bme.read     = bme_spi_read;
	bme.write    = bme_spi_write;
	bme.delay_us = bme_delay_us;
	bme.amb_temp = 25; // only used to compute the heater resistance
	CHECK_BME(bme68x_init(&bme), "bme68x_init");

	// Only measure temperature
	bme_conf.os_temp = BME68X_OS_2X;
	bme_conf.os_pres = BME68X_OS_NONE;
	bme_conf.os_hum  = BME68X_OS_NONE;
	bme_conf.filter  = BME68X_FILTER_OFF;
	bme_conf.odr     = BME68X_ODR_NONE;
	CHECK_BME(bme68x_set_conf(&bme_conf, &bme), "bme68x_set_conf");

	// No gas measurement, heater off
	struct bme68x_heatr_conf heatr_conf = {};
	heatr_conf.enable = BME68X_DISABLE;
	CHECK_BME(bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf, &bme), "bme68x_set_heatr_conf");
	return BME68X_OK;
}

// Triggers a measurement and gets the temperature in hundredths of a degree Celsius
int8_t read_bme_temperature(int32_t* temperature) {
	struct bme68x_data data;
	uint8_t n_fields;
	// The sensor goes back to sleep by itself after a forced mode measurement
	CHECK_BME(bme68x_set_op_mode(BME68X_FORCED_MODE, &bme), "bme68x_set_op_mode");
	bme.delay_us(bme68x_get_meas_dur(BME68X_FORCED_MODE, &bme_conf, &bme), bme.intf_ptr);
	CHECK_BME(bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &bme), "bme68x_get_data");
	*temperature = lroundf(data.temperature * 100.0f);
	return BME68X_OK;
}

int main() {
	stdio_init_all();
	sleep_ms(1000); // leave time for the USB serial to come up

	printf("[BME680] Initializing ... ");
	if(bme_setup() != BME68X_OK) {
		return 1;
	}
	printf("success!\n");

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
	int32_t temperature;
	while(1) {
		if(read_bme_temperature(&temperature) != BME68X_OK) {
			hal->delay(TX_PERIOD_MS);
			continue;
		}
		// Little-endian, as decoded by the receiver
		buffer[0] = temperature & 0xFF;
		buffer[1] = (temperature >> 8) & 0xFF;
		buffer[2] = (temperature >> 16) & 0xFF;
		buffer[3] = (temperature >> 24) & 0xFF;

		int16_t state = radio.transmit(buffer, PAYLOAD_LENGTH);
		if(state == RADIOLIB_ERR_NONE) {
			nb_msg_sent += 1;
			printf("sent %.2f, msg_sent: %lu\n", temperature / 100.0, (unsigned long)nb_msg_sent);
		} else {
			printf("transmit failed, code %d\n", state);
		}
		hal->delay(TX_PERIOD_MS);
	}
	return 0;
}
