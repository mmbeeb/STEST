# STEST
BeebEm serial tester (Linux).

Tester for BeebEm's IP232 serial port option, including handshake.
I have been running STEST in Cygwin64, using a Windows 10 machine.

I've modified the following BeebEm files with the hope of fixing some issues I found:

serial.cpp

serialdevices.cpp

serialdevices.h

***

Some of the issues are:

serialdevices.cpp
=================
1)Conflict between threads writing to/reading from the data buffers leading to corruption of data pointers.
Fix: Use Critical Sections.

2)Infrequent updates to other side of the nRTS flag state.
Fix: Setting new IP232reset flag can force update.  This is done on Break/Reset.

serial.cpp
==========
1)Interrupt behaviour,  mainly the lack of a TDRE interrupt.
This means transmission relies on the fact the OS polls the ACIA 100 times per second.
Consequently maximum transmission rate is 100 bytes per second (roughly 1000baud).
Fix: New routine for handling interrupts.  Also implements the movement of data from the TDR to the TDSR.

2)Data being lost when nRTS is set high.
There is a lag between the flag being set, and this being transmitted to the other side.
Any data already sent can be lost.
Fix:Data not read from buffer if nRTS is high.

_ serial test.ssd
================
Disc image containing some simple test routines.

Example: Compile & run STEST (in say a Cygwin64 window).

Open BeebEm.

In Comms\RS423 Destination menu select IP:localhost25232 & IP:IP232 Handshake (IP:Raw comms should be unselected).

In Comms menu enable RS423.

Load disc and run RX1, and bang some data to it by pressing X in STEST.

