# Power Pellets Meter

Arduino ESP based system to work with Home Assistant or other MQTT aware system. Basic features are:

* Temperatur logging
* Pellets logging
* Power meter logging
* Web server for configuration and messages
* Analog measure of one error diode to be added

### Temperatur:
Temperatur, sends every 1-wire DS18B20 device as it's serial number.

MQTT state_topic | Function 
-----------------|---------
home/sensor1/device-id|Temperature in hecto to avoid decimal. 
#### Example:
```
sensor:
  - platform: mqtt
    state_topic: 'home/sensor1/28ff248334036'
    name: 'Out temp Back'
    unit_of_measurement: '°C'
    value_template: >
       {{ (float(value) / 100)|round(1)}}

  - platform: mqtt
    state_topic: 'home/sensor1/28ff8d514b40f3'
    name: 'Temperatur Boiler'
    #sensor_type: temperature
    unit_of_measurement: '°C'
    value_template: >
       {{ (float(value) / 100)|round(1)}}
```

### Pellets logging:
I count the turns the pellets screw is making, for me it is 44 gram per turn.

MQTT state_topic | Function 
-----------------|---------
home/sensor1/turn | Screw turn count
home/sensor1/bagday | Consum of pellets bags per day, calculated from start to start of the burner.
home/sensor1/gramtoday | Gram of pellets consumed from midnight. Resets every night.
home/sensor1/gramday | Sends every midnight, the last day pellets consumed in gram.
home/sensor1/storage | Gram pellets in internal storage
home/sensor1/baghome | Pellets bags home
home/sensor1/burnerkw | Current Burner kW
home/sensor1/clean/s_cleankg | Kg since simple cleaning of burner
home/sensor1/clean/b_cleankg | Kg since bigger cleaning of burner
home/sensor1/burnerOn | If burner is on or off, binary sensor

#### Example:
```
sensor:
  - platform: mqtt
    state_topic: 'home/sensor1/turn'
    name: 'Burner Truns'
    unit_of_measurement: 'Turn'

  - platform: mqtt
    state_topic: 'home/sensor1/bagday'
    name: 'Burner Bag/Day'
    unit_of_measurement: 'Bag'
    value_template: >
       {{ (float(value) / 1000)|round(2)}}
       
  - platform: mqtt
    state_topic: 'home/sensor1/gramtoday'
    name: 'Burner Consumed'
    unit_of_measurement: 'Kg'
    value_template: >
       {{ (float(value) / 1000)|round(2)}}
       
  - platform: mqtt
    state_topic: 'home/sensor1/gramday'
    name: 'Burner Kg/day'
    unit_of_measurement: 'Kg'
    value_template: >
       {{ (float(value) / 1000)|round(2)}}
       
  - platform: mqtt
    state_topic: 'home/sensor1/storage'
    name: 'Pellets in Storage'
    unit_of_measurement: 'Kg'
    value_template: >
       {{ (float(value) / 1000)|round(1)}}
       
  - platform: mqtt
    state_topic: 'home/sensor1/storage'
    name: 'Pellets Bags in Storage'
    unit_of_measurement: 'Bag'
    value_template: >
       {{ (float(value) / 1000/16)|round(2)}}
  - platform: mqtt
    state_topic: 'home/sensor1/baghome'
    name: 'Pellets Bags Home'
    unit_of_measurement: 'Bag'

  - platform: mqtt
    state_topic: 'home/sensor1/burnerkw'
    name: 'Burner kW'
    unit_of_measurement: 'kW'
    value_template: >
       {{ (float(value) / 1000)|round(2)}}
       
  - platform: mqtt
    state_topic: 'home/sensor1/clean'
    name: 'Burner Kg small clean'
    unit_of_measurement: 'Kg'
    value_template: '{{ value_json.s_cleankg }}'
    
  - platform: mqtt
    state_topic: 'home/sensor1/clean'
    name: 'Burner Kg bigg clean'
    unit_of_measurement: 'Kg'
    value_template: '{{ value_json.b_cleankg }}'

binary_sensor:
  - platform: mqtt
    state_topic: "home/sensor1/burnerOn"
    name: "Burner"
    payload_on: "1"
    payload_off: "0"
```



### Power meter puls/blink logging:
Count blink of the power meter. 1000 impulse is 1 kWh on my meter. Sedans MQTT messages with a limit of 4 seconds between messages, for fast response but not to fast!

MQTT state_topic | Function 
-----------------|---------
home/sensor1/meter/pulse | pulse count
home/sensor1/meter/watt | current power consumption in watt
home/sensor1/wh | When consumed last hour, sends every hour
home/sensor1/whd | kWh consumed last day, sends every midnight
home/sensor1/kwh | kWh count of the meter

#### Example:
```
sensor:
  - platform: mqtt
    state_topic: 'home/sensor1/meter'
    name: 'Power puls'
    unit_of_measurement: 'Pulse'
    value_template: '{{ value_json.pulse }}'

  - platform: mqtt
    state_topic: 'home/sensor1/meter'
    name: 'Power usage'
    unit_of_measurement: 'Watt'
    value_template: '{{ value_json.watt }}'

  - platform: mqtt
    state_topic: 'home/sensor1/wh'
    name: 'Power Wh/h'
    unit_of_measurement: 'Wh'

  - platform: mqtt
    state_topic: 'home/sensor1/whd'
    name: 'Power Daily'
    unit_of_measurement: 'kWh'
    value_template: >
       {{ (float(value) / 1000)|round(1)}}

  - platform: mqtt
    state_topic: 'home/sensor1/kwh'
    name: 'Power kWh'
    unit_of_measurement: 'kWh'
```    

### Configure via http rest command
To configure and input new values in to the system there is a sort of rest api. Very simple and result is just printed out in debug serial console.

* A secret word is used for password protections...
* A wrong password is resulting in a "Ok, all system running." response.

MQTT state_topic | Function 
-----------------|---------
/PASSWORD/info/ | Prints out all pellets variables and values for debug purpose
/PASSWORD/temp/ | Prints out all temperature sensors names and values
/PASSWORD/show/storage-g/ | Return the number of gram in pellets storage
/PASSWORD/bagsh/x | x is the number of pellets bags home
/PASSWORD/bagsa/x | Poures x bags into storage, bags home decreased
/PASSWORD/storage/x | Sets the storage to X kg
/PASSWORD/kwh/x | Sets the kWh meter X, to check if system counts right!
/PASSWORD/sclean/ | Call this then simple cleaning of burner is performed, resets kg counter.
/PASSWORD/bclean/ | Call this then big/advanced cleaning of burner is performed, resets kg counter of this and simple cleaning.

#### Example of: http://192.168.2.26/PASSWORD/temp
```
New client: GET /PASSWORD/temp/ HTTP/1.1
Req. temps Sensor 
home/sensor1/28897b34500c / 2212 22.12 res: 11
home/sensor1/28bd8334500a5 / 3650 36.50 res: 11
home/sensor1/28311665005a / 4237 42.38 res: 11
home/sensor1/28d379345006e / -825 -8.25 res: 11
home/sensor1/28ff248334036 / -937 -9.38 res: 11
home/sensor1/28ff8d514b40f3 / 6612 66.12 res: 11
All done 6
```    

## Example of pushbullet configurations in Home Assistant
```
- id: boiler_cold
  alias: Pannan kall
  trigger:
    platform: numeric_state
    entity_id: sensor.temperatur_boiler
    # At least one of the following required
    below: 60
    for:
      minutes: 1
  action:
    - service: notify.henrik
      data:
        message: Pannan är under 60 grader nu!!!
        title: "The Boiler!"

- id: pellets_16kg_left
  alias: 16Kg pellets kvar
  trigger:
    platform: numeric_state
    entity_id: sensor.pelles_in_storage
    below: 16
  action:
    - service: notify.henrik
      data:
        message: Under 16kg pellets i förrådet! Fyllpå!
        title: "The Boiler!"
```    

