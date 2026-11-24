# Setup
- `git submodules update --init`

# LoRa receiver on Pi Zero W
On the Pi Zero (Raspbian Bookworm), assuming the repo has been cloned there and cmake is installed:
- `sudo raspi-config nonint do_spi 0`
- `cd rpi_receiver && mkdir -p build && cd build`
- `cmake .. && make -j4`
- `./lora_spi_receiver`

# LoRa transmitter on Pico 2
## Install picotool
The picotool version should be 2.1.1 as specified by the 2.2.0 version of the pico-sdk. Install latest 2.1.1 version: `https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.1.1-3/picotool-2.1.1-x86_64-lin.tar.gz` (if on another platform than Linux choose the corresponding binary).

## Compile and flash
On the host, assuming cmake is installed:
- `cd pico_transmitter && mkdir -p build && cd build`
- `cmake .. -DPICO_SDK_PATH=../../external/pico-sdk && make -j4`

Get the serial number of the connected Pico 2 (idSerial):
- `sudo lsusb -v | grep -A12 -E "Pico|RP2350"`
- `export SERIAL_NUMBER=<idSerial>`

Flash (replace picotool by where you installed it on the machine):
- `sudo ~/.local/bin/picotool-2.1.1-x86_64-lin/picotool/picotool load --ser $SERIAL_NUMBER -f -x lora_spi_transmitter.uf2`

