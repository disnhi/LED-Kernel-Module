# LED-Kernel-Module

A simple LED kernel module that lights up an LED connected to our raspberry pi where the LED is a custom device made via the GPIO pins.

Echoing the word “on” to /dev/led should illuminate the LED and echoing the word “off” to /dev/led should turn
the LED off. Reading the file /dev/led using cat should be shown the light on and light off ascii art similarly. 
