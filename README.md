# AC Daily Timer
https://github.com/JChristensen/dehumid (or bit.ly/3yiNCEZ)  
README file  

## License
AC Daily Timer sketch Copyright (C) 2023-2024 Jack Christensen GNU GPL v3.0

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License v3.0 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/gpl.html>

## Overview
Sketch to control a 120VAC appliance via a solid-state relay, according to a daily schedule. My utility company charges higher electric rates in the summer, Jun-Sep, from 14:00 to 19:00 on weekdays. I use this controller to turn my dehumidifier off during the peak rate time.

## Operation
The user interface consists of three LEDs and a push button.

The green LED (RUN) blinks once per second to show the unit is operating.

The yellow LED (MANUAL) illuminates when the unit is in manual mode. When not illuminated, the unit is in automatic (schedule) mode.

The red LED (ON) illuminates when power is being delivered to the outlet.

The pushbutton (ON/OFF/MANUAL) can be pressed at any time to turn outlet power off or on. Pressing and holding it for one second changes between automatic and manual mode.

In automatic mode, the outlet is controlled by the programmed schedule. Still, the pushbutton can be used to override the current output state. When the next schedule time arrives, the outlet power will again be determined by the programmed schedule.

In manual mode, the outlet is controlled only by the pushbutton. When changing to manual mode, the outlet will be initially turned off. When changing to automatic mode, the current schedule will determine the output state.

## Technical stuff
Developed with Arduino 1.8.19 for Arduino Uno or equivalent. Debug information is written to the serial port.

For time keeping, an MCP7941x RTC is used. The RTC can be calibrated automatically during setup from a value stored in its EEPROM. A calibration value is assumed to be present at address 0x7F if addresses 0x7D and 0x7E contain 0xAA and 0x55 respectively. An MCP9802 temperature sensor can optionally be present on the I2C bus. The code will automatically detect whether it is installed, and, if so, will report temperature to the serial port.

[See the hardware design here](https://github.com/JChristensen/ac-timer-hw).

## Credits
Thanks to Tom Hagen for design input and for testing.
