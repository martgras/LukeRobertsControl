# Remote Control for Luke Roberts Model F Lamp

## A MQTT and HTTP gateway to control a Luke Roberts Model F Lamp

> **⚠ WARNING: Work in progress neither the docs nor the code is complete yet*

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)


## Introduction



The Luke Roberts lamp can be controlled over Bluetooth BLE or using a [cloud based API](https://cloud.luke-roberts.com/api/v1/documentation)

To enable integration into smarthome systems like Iobroker, Home Assistant or Openhab the Cloud REST API can be used. 
However I also want to control my lamp the traditional way (Wall switches, rotary dimmer..).  This was my main motivation - I want to control the lamp using a rotary dimmer not from my smartphone. 

Therefore, I implemented a ESP32 based bridge that talks to the lamp using BLE. 
The gateway uses MQTT for control and status messages. 
A extremly simply REST API is also included but currently only supports sending commands and not querying the current status. 

The lamp doesn't offer a public API to query the current values of brightness, colortemperatur or other parameters. Only the currently selected scene can be queried over BLE
Therefore, the state is kept the gateways memory. ~~Because every scene has different brightness levels I created a translation map for the 7 Built-in scenes (just by trial and error) to know the initial brightness when switching to a new scene.~~. 
There is an undocumented API to query the current brightness and color temperature of the the downlight. Since it's an undocumented API things may break after a firmware update.  

## Hardware

Only an ESP32 is required to control the lamp using MQTT or HTTP

## Controlling the Lamp using buttons, rotary  and switches 

All configuration settings are done in **platformio_usersettings.ini**. Make sure to rename the sample and modify the values for your environment



#### Rotary

If you attach a rotary switch to the controller you need to define GPIO pins used

````
;Set pin A  for the CLK  signal
-DROTARY_PIN_A=GPIO_NUM_26
;Set pin B for the DT
-DROTARY_PIN_A=GPIO_NUM_26
````

If the rotary has a switch define the pin  for the switch (of course it can be a seperate switch as well)
````
-DROTARY_BUTTON_PIN=GPIO_NUM_25
````

The brightness is controlled by turning the rotary encoder. 
The switch toggles power. Keeping the switch pressed cycles through the scenes

#### Buttons
##### Three Button mode

The brightness can also be controlled using buttons
BUTTON_UP_PIN defines which pin is used increase the brightness and BUTTON_DOWN_PIN defines the pin to decrease the brightness.
Also define ROTARY_BUTTON_PIN if you want a power toggle button.

Example : 
````
-DBUTTON_DOWN_PIN=GPIO_NUM_15
-DBUTTON_UP_PIN=GPIO_NUM_17                  
````

##### Single Button mode
If you attach a single button define the PIN to use (SINGLE_BUTTON_PIN)

This button toggles the power (actually when released).

The brightness can be decreased using a long press. Another long press within 10 seconds switches the direction and the brightness will be increased. 

A double-click will switch to the next scene



**Note:  The GPIO Pins are used in the INPUT_PULLUP configuration the avoid the hassle of external pullups.  Make sure you use a GPIO that supports pullup-mode.  (All  GPIOS except 35,36 and 39)**

##### Three Button mode using resistor encoding
To use 3 buttons with a single GPIO the buttons need encoding with resistors 


<img src="doc/resistor_buttons.svg">

The buttons create a voltage divider where each button results in a different voltage level at the defined GPIO PIN.   The voltage of each button is defined by the resistor connected to the button. However, the exact values are not important if they a different enough to be clearly distinguishable.
With the values chosen in this example you get 3.3V at the GPIO pin if no button is pressed.

* The first button (5.1k) results in ~1.12 V (ADC Reading ~ 1300)
* The second button (10k) results in 1.65 V (ADC Reading ~2050)
* The third button (47k) results in 2.75 V (ADC Reading ~3400)

To map the buttons the ADC reading values are required. The easiest way to get these values is connecting the to your ESP32 and run at simple sketch to read out the values
````
const int adc_pin= 35;
int adc_value = 0;
void setup() 
{
  Serial.begin(115200);
}
void loop() 
{
  adc_value = analogRead(adc_pin);
  Serial.print("ADC VALUE = ");
  Serial.println(adc_value);
  delay(500);
}
````

Example configuration for with the above resistor values using GPIO 35 to read the button values:
````
-DRESISTOR_BUTTON_PIN=35
-DRESISTOR_BUTTON_UP=1000
-DRESISTOR_BUTTON_DOWN=1700
-DRESISTOR_BUTTON_SWITCH=3000
````
Every ADC reading above 1000 and below 1700 will map to the first button. Readings between 1700 and 3000 to the second button and everything above 3400 and below 4000 to the third button)
(It would probably make sense to use a 22k instead of a 47k resistor for the third button, but I took what I found first 😊)


Because this mode doesn’t need an internal pull-up pins 34,35,36 or 39 can be used 
(see https://github.com/bxparks/AceButton/blob/develop/docs/resistor_ladder/README.md)


##### Customize the button actions 


The timings for long press and double click can be modified 
* **LONG_PRESS_DELAY** Milliseconds that a button needs to be pressed down before the start of the sequence of long press event. The first event will fire as soonas this delay has passed. 
* **LONG_PRESS_INTERVAL**
    Subsequent events will fire after LONG_PRESS_INTERVAL milliseconds 
* **DOUBLE_CLICK_INTERVAL** Milliseconds that a button needs to be pressed down twice to generate a DOUBLE_CLICK action


For each button 3 actions can be defined:
* **CLICK_ACTION** is called when the button is pressed 
* **LONG_PRESS_ACTION** is called when the button is pressed for more than LONG_PRESS_DELAY milliseconds
* **DOUBLE_CLICK_ACTION** is called when the button is clicked twice within DOUBLE_KLICK_INTERVAL


Every action can be mapped to differents functions  (enum button_function_codes)

  * **kNoop = 0**  Do nothing
  * **kPowerOn = 1** Turn the lamp on
  * **kPowerOff = 2** Turn the lamp off
  * **kPowerToggle = 3** Toggle the power
  * **kChangeBrightness = 4** Increase or decrease brightness **STEP_VALUE** defines the amount of the change
  * **kChangeColorTemperature = 5** Increase or decrease color temperature **STEP_VALUE** defines the amount of the change
  * **kNextScene = 6** Switch to the next scene. After the last scene move to the first scene 

* **STEP_VALUE**  To allow modifiying the step value for brightness or
colortemperature change a step value is also assigned to every button
Although STEP_VALUE is ignored for some function codes it must be defined for
every button. Default is 10 for up and -10 for down

Example:  Set the action for a simple click on the rotary button to increase the color temperature by 15 steps (instead of the default action kPowerToggle). 
Change double-click to toggle the power instead and finally set long press to change the brightess (the same step value is used for all button actions )

````
            -DROTARY_BUTTON_PIN=GPIO_NUM_25
            -DROTARY_BUTTON_PIN_CLICK_ACTION=kChangeColorTemperature
            -DROTARY_BUTTON_PIN_STEP_VALUE=15
            -DROTARY_BUTTON_PIN_DOUBLE_CLICK_ACTION=kPowerToggle 
            -DROTARY_BUTTON_PIN_LONG_PRESS_ACTION=4  ; either the numeric value or the constant can be used
````
### Use a switch

A on/off toggle switch can be used to toggle the power state of the lamp. Each switch change toggles power. Enable it by defining SWITCH_PIN
Additional config settings
LONG_PRESS_DELAY defines how long the button must be pressed for the first event. Default value is 1500 ms
LONG_PRESS_INTERVAL defines how long it takes to trigger another long press event. Default is 1500 ms
DOUBLE_KLICK_INTERVAL maximum time between 2 clicks to define them as a double-click. Default is 500 ms


### Use a Relay

By default, the lamp is turned off by setting the scene to 0. 
If you prefer a “real” power off connect a relay to a GPIO PIN and define RELAY_PIN.

All settings can be combined if different GPIO’s are used.

### Complete example with all hardware options connected
<img src="doc/circuit.svg">

````
    # Rotary
    -DROTARY_PIN_A=GPIO_NUM_26
    -DROTARY_PIN_B=GPIO_NUM_27
    -DROTARY_BUTTON_PIN=GPIO_NUM_25
    # Up / down button
    -DBUTTON_DOWN_PIN=GPIO_NUM_16
    -DBUTTON_UP_PIN=GPIO_NUM_18
    # Switch 
    -DSWITCH_PIN=GPIO_NUM_23
    # Single mode button
    -DSINGLE_BUTTON_PIN=GPIO_NUM_19
    # Voltage divider button
    -DRESISTOR_BUTTON_PIN=35
    -DRESISTOR_BUTTON_UP=1000
    -DRESISTOR_BUTTON_DOWN=1700
    -DRESISTOR_BUTTON_SWITCH=3000
    # Relay
    -DRELAY_PIN=GPIO_NUM_32
    # timings
    -DLONG_PRESS_DELAY=1000
    -DLONG_PRESS_INTERVAL=1000
    -DDOUBLE_KLICK_INTERVAL=500

````
###  Using a wireless rotary (or switch). 
The wireless rotary sends the events to a smart home server that will then control the lamp using MQTT or HTTP
In my case I use a [Homematic IP Rotary Button](https://www.homematic-ip.com/en/products/detail/homematic-ip-rotary-button.html). 

It creates 6 different events:  turn left, turn right, fast turn left, fast turn right, button press, long button press. 

* For a turn left event I send "http://gatewayip/cm?cmnd=dimmer down 5"  to decrease the brightness by 5 points (0 - 100)
* For a fast turn left event I send "http://gatewayip/cm?cmnd=dimmer down 10"  to decrease the brightness by 10 points (0 - 100)
* A button press event will toggle the lamp on or of using "http://gatewayip/cm?cmnd=power toggle". 
* A long press will switch to the next scene using "http://gatewayip/cm?cmnd=scene up"

The advantage is that the esp device can be anywhere in the room and even use ethernet instead wifi if you have an esp32 with a ethernetport connected 
(Wifi and BLE can cause issues). 
At least for homematic ip the disadvantage is a higher duty cycle because turning the dimmer sends  many  commands. 
(and I don't like the design of the Homematic IP Rotary Button)



## Building

This project requires PlatformIO to build 
Before compiling and deploying a few settings must be configured in  platformio_usersettings.ini.
A sample is provided : [platformio_usersettings.ini.sample](platformio_usersettings.ini.sample)

You can hardcode the device address of your lamp in platformio_usersettings.ini 
````
-DLR_BLEADDRESS='"c4:b9:71:da:19:c7"' 
````
If device address is not provided the gateway will scan for a device with the BLE Service ID "44092840-0567-11E6-B862-0002A5D5C51B" during startup and use the first address found. 
The address is published to the MQTT topic tele/yourdevicename/BLEADDRESS 


1. Modifiy the settings in platformio_usersettings.ini.sample and save as  platformio_usersettings.ini
2. Open in PlatformIO 
4. Build and upload
3. Updates can be sent using OTA. 


From commandline 
````
python -m pip  install --upgrade pip
pip install --upgrade platformio

git clone https://github.com/martgras/LukeRobertsControl.git
# or 
wget https://github.com/martgras/LukeRobertsControl/archive/master.zip
unzip master.zip

cd LukeRobertsControl-master/
mv platformio_usersettings.ini.sample platformio_usersettings.ini
# Edit the settings to for your environment
# nano platformio_usersettings.ini
pio run
````


## MQTT 
The format is similar to tasmota 


### Commands
all commands use the topic cmnd/devicename>/command



#### POWER
topic: cmnd/\<devicename\>/POWER
payload  ON/OFF 1/0 or TOGGLE 

example:  turn on the light on my device lrdimmer:  
```mosquitto_pub  -h localhost  -t "cmnd/lrdimmer/power" -m "off"```


#### DIMMER
topic: cmnd/\<devicename\>/DIMMER
payload: 
<absolutue value>  sets the brightness to the given value. Range 0 - 100
<up/down [stepvalue]>  change the brightness by stepvalue. if ommited stepvalue is 10


#### CT
topic: cmnd/\<devicename\>/CT
payload: 
<absolutue value>  sets the color temperature to the given value (in mireds). Range 250 - 416
<up/down [stepvalue]>  change the color temperature by stepvalue. if ommited stepvalue is 1


#### KELVIN
topic: cmnd/\<devicename\>/KELVIN
payload: 
<absolutue value>  sets the color temperature to the given value (in kevin). Range 2700-4000
<up/down [stepvalue]>  change the color temperature by stepvalue. if omitted stepvalue is 10


#### ~~SCENE~~
<del>
topic: cmnd/\<devicename\>/SCENE
payload: 
<absolutue value>  sets the scene to the given value . Range 1 .. number of scenes
<up/down [stepvalue]>  Switch to the next or previous scene
</del>

*not required anymore - the mapping is done automatically now*

#### UPLIGHT
Modify the current light settings for uplight. Modifications are lost on power-down.

duration : Duration in ms, 0 for infinite
saturation : Range 0 - 255
hue : Range 0-65535  (or colortemperature in kelvin if saturation is 0  )
brightness : Range 0 - 255
 or dimmer: Range 0 - 100 

````
 mosquitto_pub  -h 192.168.1.114  -t "cmnd/lrdimmer2/uplight" -m '{"d":0,"s":25,"h":53000 ,"b" : 255}'
 ````
if you prefer to specify the brightness in % use dimmer 
 ````
 mosquitto_pub  -h 192.168.1.114  -t "cmnd/lrdimmer2/uplight" -m '{"d":0,"s":25,"h":53000 ,"dimmer" : 100}'
 ````
#### DOWNLIGHT

Modify the current light settings for downlight. Modifications are lost on power-down.

duration : Duration in ms, 0 for infinite\
saturation : Range 0 - 255\
kelvin : Range 2700- 4000\
 or\
ct : Range 250-370   (in mired instead of kelvin)\
brightness : Range 0 - 255\
 or\
dimmer: Range 0 - 100

 ````
  mosquitto_pub  -h 192.168.1.114  -t "cmnd/lrdimmer2/downlight" -m '{"duration":9550,"kelvin":4000,"dimmer" : 0 }'
 ````

#### MAPSCENE
set the brightness mapping for a scene (values are stored in flash memory)
topic: cmnd/\<devicename\>/MAPSCENE
payload: <scene> <brightness>

example:  set inital brightness for scene 3 to 60%:  
```mosquitto_pub  -h localhost  -t "cmnd/lrdimmer/mapscene" -m "3 60"```


#### OTA
Initiate OTA update. 
After this command and OTA update using the espota protocol on port 3232 can be started

topic: cmnd/\<devicename\>/OTA
payload: not used 


#### REBOOT
Reboot the esp device

topic: cmnd/\<devicename\>/RESTART
payload: not used 


#### BLECUSTOM
Send bytes to the device characteristic 44092842-0567-11E6-B862-0002A5D5C51B

topic: cmnd/\<devicename\>/blecustom
payload: hexbytes 

example:  send 0xA0 0x02 0x05 0x01 (command to select scene 1)
````
mosquitto_pub  -h localhost  -t "cmnd/lrdimmer/blecustom" -m "A0020501"
````

### MQTT Status messages

The device sends "Online" to the topic tele/devicename/LWT when connected. During a reboot offline is sent
For every change the updated values are sent to the topic "stat/devicename/RESULT" in JSON  format

```Json
{"Time":"2020-12-18T13:13:29","Heap":119,"IPAddress":"192.168.1.69","POWER":"OFF","CT":300,"KELVIN":3333,"DIMMER":50,"SCENE":1}
````

in addition for every change to brightness, colortemperature , scene and power a simple message is sent to stat/lrdimmer/command with the new value as the payload

For example 

````mosquitto_pub  -h 192.168.1.114  -t "cmnd/lrdimmer/dimmer" -m "down 10"````

triggers 2 MQTT messages from the device:
````
stat/lrdimmer/DIMMER 40
stat/lrdimmer/RESULT {"Time":"2020-12-18T13:15:58","Heap":119,"IPAddress":"192.168.1.69","POWER":"OFF","CT":300,"KELVIN":3333,"DIMMER":40,"SCENE":1}
````

### Using with Home Assistant 
The gateway is automatically detected as a mqtt device

### Using with OpenHab 
Discovery in Openhab is not yet working correctly ( not sure why )
To use the gateway I created a  mqtt thing in lr.things and then configured the channel I want to use in paperui. Of course, you can also create MQTT items directly 

````
//LR.things

Thing mqtt:topic:lrdimmer "Lampe" (mqtt:broker:my_mosquitto) @ "Wohnzimmer" {
    Channels:
        Type string : reachable "Erreichbar"      [ stateTopic="tele/lrdimmer/LWT" ]
        Type switch : power     "Lampe An/Aus"    [ stateTopic="stat/lrdimmer/POWER", commandTopic="cmnd/lrdimmer/POWER" ]
        Type  dimmer :brightness "Dimmer"         [ stateTopic="stat/lrdimmer/RESULT", transformationPattern="JSONPATH:$.DIMMER",commandTopic="cmnd/lrdimmer/Dimmer", min=0,max=100]
        Type dimmer : ColorTemp  "Farbtemperatur" [ stateTopic="stat/lrdimmer/RESULT", transformationPattern="JSONPATH:$.CT", commandTopic="cmnd/lrdimmer/ct", min=250,max=370]
        Type  number : scene     "Szene"          [ stateTopic="stat/lrdimmer/RESULT", transformationPattern="JSONPATH:$.SCENE", commandTopic="cmnd/lrdimmer/scene", min=1,max=8]

}
````



## HTTP 

Accessing the device over port 80 brings up a very simple webpage that allows setting the values.

## API

The API is under the /cm path

The HTTP mirrors the MQTT commands. 
Only GET is used 

To increase the brightness by 5 points use [http://192.168.1.69/cm?cmnd=dimmer up 5](http://192.168.1.69/cm?cmnd=dimmer%20up%205)

TODO: Implement a query interface over HTTP



## Credits

[NimBle ](https://github.com/h2zero/NimBLE-Arduino) for a great BLE library that works together with Wifi 

[esp32-rotary-encoder](https://github.com/DavidAntliff/esp32-rotary-encoder) - this ESP-IDF library works very reliable. Because it is only designed for ESP-IDF I used the code a basis for a C++ class that works with Arduino. I merged in an AceButton into this class since my rotary has a switch as well.

[AceButton](https://github.com/bxparks/AceButton) handles all the little details like debouncing and providing an event interface

[ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) for my crude HTTP interface

[PubSubClient](https://github.com/knolleary/pubsubclient) to handle all the MQTT traffic

