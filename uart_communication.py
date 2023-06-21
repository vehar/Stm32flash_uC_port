import serial
import time

wlen = 124  # max write length

# open file to flush and get its size
with open("test.bin", "rb") as to_flash:
    content = to_flash.read()

file_size = len(content)
counter_l = 0
counter_r = wlen

# open serial
ser = serial.Serial(
    port='/dev/ttyUSB0',
    baudrate=115200  #,
)

if(not ser.isOpen()):
    ser.open()


while True:
    time.sleep(2)
    output_dev = ""
    
    # read initial data from board
    while ser.inWaiting() > 0:
        output_dev += str(ser.read(1))[2]
        
    if output_dev != '':
        print(output_dev.replace("\\", "\n"))

    # check if boards waits for size, if yes - send it 
    if "Receive size:" in output_dev:
        print(f">> {file_size}")
        bytes_arr = str(file_size).encode()
        ser.write(bytes_arr)

        continue

    # check of board waits for data, if yes - send it 
    elif "Ready to receive portion of data" in output_dev:
        print(f">> send portion of size {counter_r - counter_l} ({100/file_size * counter_r})")
        ser.write(content[counter_l:counter_r])
        counter_l = counter_r
        counter_r = counter_r + wlen if counter_r + wlen < file_size else file_size
        continue


    # wait for command from stdin
    # 1 - start scanning i2c address
    # 2 - start flashing
    # if stops you can enter continue - then it will read from uart again
    in_command = input(">> ")
    if in_command == 'exit':
        ser.close()
        exit()
    if in_command == "continue":
        continue

    bytes_arr = in_command.encode()
    ser.write(bytes_arr)

