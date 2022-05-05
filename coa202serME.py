from random import randint
import serial
import time
import argparse
import sys

# Call this program with
# python coa202ser.py portdevice
# e.g. on windows:
#      python coa202ser.py COM2
# or macos/linux:
#      python coa202ser.py /dev/ttyACM0
#
# Remember you cannot leave this running while uploading
# Check lab worksheet 5 to set up your environment

# The code below parses the command line arguments and leaves the
# variable args.port containing the device name.

parser = argparse.ArgumentParser(
    description='Talk to Ardunio over serial')
parser.add_argument(
    'port',
    metavar='device',
    type=str,
    nargs='?',
    help='The port to connect to')
args = parser.parse_args()

# Open the serial port (this will reset your Arduino)
print('connecting to port', args.port)
ser = serial.Serial(args.port, 9600, timeout=1,
                    rtscts=False, xonxoff=False, dsrdtr=False)

print("waiting for sync")
going = True
while going:
    s = ser.read(1)  # Read just one byte
    # print(s)       # Print it for debugging
    if s == b'Q':
        going = False
ser.write(b'X')
print("Sync")

line = ser.readline()
print(line)  # This should print BASIC or your extension list

initial = ser.readlines()
print('Init: ', initial)
for line in initial:
    print()

# Build a list of messages to send
# the b'' notation creates byte arrays suitable for
# passing to ser.write().  ser.write() will not accept
# str variables.

msgs = [
    # 'CAMain',
    # 'CBSecondary',
    # 'VA100',
    # 'VB100',
    # 'XB50',
    # 'NB25',
]

for id in [chr(x) for x in range(ord('A'), ord('Z')+1)]:
    # msgs.append(f'C{id}{id}{id}')
    msgs.append(f'C{id}123456789012345')
    for i in range(0, 10):
        # msgs.append(f'V{id}5')
        ...
    # msgs.append(f'V{id}' + str(randint(0, 255)))

def sendMsgs(delay: float = 0.1):
    for x in msgs:
        print('Write:', str(x))
        ser.write(x + b'\n')

        # Check for message back. This will timeout after a second
        line = ser.readline()
        print('Read: ', line)

        # time.sleep(1)
        time.sleep(delay)

msgs = map(lambda x: bytes(x, 'ascii'), msgs)

sendMsgs()

# Simply write these messages out once per second
# Customise above and below as you see fit.

input('press enter to send Vs')
msgs = []
for id in [chr(x) for x in range(ord('A'), ord('Z')+1)]:
    # msgs.append(f'V{id}' + str(randint(0, 255)))
    # msgs.append(f'V{id}1')
    ...

# msgs = map(lambda x: bytes(x, 'ascii'), msgs)

for x in msgs:
    if (x[1] == 'Y' or x[1] == 'Z'):
        input(f'press enter to send {x}')

    x += '\n'
    x = bytes(x, 'ascii')

    print('Write:', str(x))
    ser.write(x)

    # Check for message back. This will timeout after a second
    line = ser.readline()
    print('Read: ', line)

    time.sleep(0.3)
