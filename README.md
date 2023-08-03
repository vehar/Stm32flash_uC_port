# Flasher for stm32

This program runs on stm32f103 and can flash different stm boards (for supported boards check ```dev_table.c``` in stm32flash sources).

> **_IMPORTANT:_** there should be pull-up resistors on i2c pins (PB6 - scl, PB7 - sda) 

## Repository structure:
- flasher_stm32 - main program
- stm32flash_original - original project
- test - test program for stm32f401 board that turns led on
- test_f746 - test program for stm32f746 board that turns leds on
- uart_communication.py - test script that communacates with board via UART and sends firmware file

## Usage

Right now the program does the following things:

- scans for i2c address
- erases flash on the board that should be flashed
- flashes given binary firmware on the board
- starts execution of firmware

To use it you should:
- determine COM port in you system and tune python script (Ex. port='COM20')
- enter the boot mode on the board that should be flashed
- run script Ex. $ python uart_communication.py A:\\LedOn.bin
- send "1" via UART to start address scaning
> **_IMPORTANT:_** check  if board is still in boot mode as after address scan it can leave it
- reset board in boot mode
- then you can send "2" to start flashing
    - after that you should send the size of the binary file that will be flashed (the program will send a message ```Receive size:```, size should be sent as little endian and then it will be reconstructed)
    - then you should send the binary file in chunks of ```124``` bytes (for now this value is hardcoded as with default value it did not work) (the program will send a message ```Ready to receive portion of data```)
        - this step will continue until the end of the file
    - after that the program will start execution 
- type "continue" few times and wait till flashing



# USB-I2C adapter (for stm32f103)

- clone [this repository](https://github.com/daniel-thompson/i2c-star) and follow the steps from its instruction except for last two steps
- connect st-link to the board and run this command from ```i2c-star``` directory or specify the correct path to ```i2c-star/src/bootloader/usbdfu.hex``` :

```{bash}
openocd -f interface/stlink.cfg -f board/stm32f103c8_blue_pill.cfg -c "init" -c "reset init" -c "flash write_image erase src/bootloader/usbdfu.hex" -c "reset" -c "shutdown"
```

This will flash a bootloader on the board that will be used to install main application. 
The difference here from the method given in the original reposotory is in board config file, there a custom config was used, here is the standard openOCD config is used.

- then you can install main application <ins>**using USB socket**</ins> and  by running this command (file ```i2c-stm32f1-usb.bin``` is stored in ```i2c-star/src/i2c-stm32f1-usb```):

```{bash}
dfu-util -a 0 -d 0483:df11 -s 0x08002000:leave -D i2c-stm32f1-usb.bin
```
