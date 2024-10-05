# USB_Knob
A USB knob that can scroll, pan, adjust the volume, and more. Uses the CH552 USB microcontroller.

## What does it do?
This device is a computer peripheral, it can scroll, pan (scroll horizontally), and adjust the volume. It also has simple media controls such as play/pause, next, and prev.  

## How does it work?
The device has 3 buttons: left, center, and right. These 3 buttons are used to set the "mode" of the device, and when they are held down they act as function modifiers. The device also has a knob which has a fourth button, and is the most frequently used control on the device. Three indicator LEDs indicate the current "mode" setting of the device.  

## Device features
### Scroll mode
- Press the left button to enter scroll mode.  
- The knob button functions as the middle mouse button.  
- While the left button is released spinning the knob scrolls vertically (like a mouse wheel).  
- While the left button is pressed spinning the knob adjusts the scrolling speed (the speed range is 1 to 10).  
### Pan mode
- Press the center button to enter pan mode.  
- If the mouse wiggler feature is enabled (option set at compile time) the knob button toggles the mouse wiggler on/off. The mouse wiggler only acts when the knob is in pan mode, but the on/off state is maintained even when the device is in a different mode.  
- If the mouse wiggler feature is not enabled then the knob button functions as the middle mouse button.  
- While the center button is released spinning the knob scrolls horizontally.  
- While the center button is pressed spinning the knob adjusts the scrolling (pan) speed (the range is 1 to 10).  
### Volume mode
- Press the right button to enter volume mode.  
- While the right button is released the knob button acts as a mute button.  
- While the right button is pressed the knob button acts as a play/pause button.  
- While the right button is released spinning the knob adjusts the volume.  
- While the right button is pressed spinning the knob functions as media next/prev buttons.  
