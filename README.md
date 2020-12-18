# Remote Control for Luke Roberts Model F Lamp

##Caveat: Work in progress neither the docs nor the code is complete yet



## Introduction



The Luke Robers lamp can be controlled over Bluetooth BLE or using a [cloud based API](https://cloud.luke-roberts.com/api/v1/documentation)

To enable integration into smarthome systems like iobroker, homeassistent, openhab the Cloud REST API can be used. 
However I also want to control my lamp the traditionanal way (Wall switches , rotary dimmer..).  Actually this was my main motivation - I want to control the lamp using a rotarty dimmer not from my smartphone. 

Therefore I implemented a ESP32 based bridge that talks to the lamp using BLE. 
The gateway uses MQTT for control and status messages. 
A extremly simply REST API is also included but currently only supports sending commands and not querying the current status. 

The lamp doesn't offer a public API to query the current values of brightness, colortemperatur or other paramaters. Only the currently selected scene can be queried over BLE
Therefore the state is kept the gateways memory. Because every scene has different brightness levels I created a translation map for the 7 Built-in scenes (just by trial and error) to know the inital brightness when switching to a new scene. 

Currently I have tested 2 setups to control (mainly) the brightness of the lamp. 

1. Using a wireless rotary (or switch). 
The wireless rotary sends the events to a smarthome server that will then control the lamp using MQTT or HTTP
In my case I use a [Homematic IP Rotary Button](https://www.homematic-ip.com/en/products/detail/homematic-ip-rotary-button.html). 
It creates 6 different events:  turn left, turn right, fast turn left, fast turn right, button press, long button press. 
For a turn left event I send "http://<gatewayip>/cm?cmnd=dimmer down 5"  to decrease the brightness by 5 points (0 - 100)
For a fast turn left event I send "http://<gatewayip>/cm?cmnd=dimmer down 10"  to decrease the brightness by 10 points (0 - 100)
A button press event will toggle the lamp on or of using "http://<gatewayip>/cm?cmnd=power toggle". 
A long press will switch to the next scene using "http://<gatewayip>/cm?cmnd=scene up"

The advantage is that the esp device can be anywhere in the room and even use ethernet instead wifi if you have an esp32 with a ethernetport connected 
(Wifi and BLE can cause issues). 
At least for homematic ip the disadantage is a higher duty cycle because turning the dimmer can send quite a number of commands. 
(and I don't like the design of the Homematic IP Rotary Button)

2. Using a rotary button directly connected to esp32 

The rotary switch is connected to pins 4,5 and 23 on my esp32 device. Everything fits into the wall socket

Find the device address of you lamp (example "c4:b9:69:fe:14:b6")

1. Modifiy the settings in platformio_usersettings.ini 
2. Build and upload
3. Updates can be sent using OTA. 



## MQTT 
The format is similar to tasmota 


### Commands
all commands use the topic cmnd/devicename>/command



#### POWER
topic: cmnd/<devicename>/POWER
payload  ON/OFF 1/0 or TOGGLE 

example:  turn on the light on my device lrdimmer:  
```mosquitto_pub  -h localhost  -t "cmnd/lrdimmer/power" -m "off"```


#### DIMMER
topic: cmnd/<devicename>/DIMMER
payload: 
<absolutue value>  sets the brightness to the given value. Range 0 - 100
<up/down [stepvalue]>  change the brightness by stepvalue. if ommited stepvalue is 10


#### CT
topic: cmnd/<devicename>/CT
payload: 
<absolutue value>  sets the color temperature to the given value (in mireds). Range 250 - 416
<up/down [stepvalue]>  change the color temperature by stepvalue. if ommited stepvalue is 1


#### KELVIN
topic: cmnd/<devicename>/KELVIN
payload: 
<absolutue value>  sets the color temperature to the given value (in kevin). Range 2700-4000
<up/down [stepvalue]>  change the color temperature by stepvalue. if ommited stepvalue is 10

### UPLIGHT
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
### DOWNLIGHT

Modify the current light settings for downlight. Modifications are lost on power-down.

duration : Duration in ms, 0 for infinite
saturation : Range 0 - 255
kelvin  : Range 2700- 4000
brightness : Range 0 - 255
 or dimmer: Range 0 - 100 

 ````
  mosquitto_pub  -h 192.168.66.114  -t "cmnd/lrdimmer2/downlight" -m '{"duration":9550,"kelvin":4000,"dimmer" : 0 }'
 ````

#### MAPSCENE
set the brightness mapping for a scene (values are stored in flash memory)
topic: cmnd/<devicename>/MAPSCENE
payload: <scene> <brightness>

example:  set inital brightness for scene 3 to 60%:  
```mosquitto_pub  -h localhost  -t "cmnd/lrdimmer/mapscene" -m "3 60"```


#### OTA
Initiate OTA update. 
After this command and OTA update using the espota protocol on port 3232 can be started

topic: cmnd/<devicename>/OTA
payload: not used 


#### REBOOT
Reboot the esp device

topic: cmnd/<devicename>/RESTART
payload: not used 


#### BLECUSTOM
Send bytes to the device characteristic 44092842-0567-11E6-B862-0002A5D5C51B

topic: cmnd/<devicename>/blecustom
payload: hexbytes 

example:  send 0xA0 0x02 0x05 0x01 (command to select scene 1)
```mosquitto_pub  -h localhost  -t "cmnd/lrdimmer/blecustom" -m "A0020501"```

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


## HTTP 

Acessing the device over port 80 brings up a very simple webpage that allows setting the values.

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




































































