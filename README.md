# LoRa receiver on Pi Zero W
On the Pi Zero (Raspbian Bookworm):
- `sudo raspi-config nonint do_spi 0`
- `cd rpi_receiver && mkdir -p build && cd build`
- `cmake .. && make`

# LoRa transmitter on Pico 2
On the host:
- `export PICO_SDK_PATH=<path>`
- ``
