## WiFi Robot

Setup a web server for sensors and actuator controls on a ESP-12 controller.

### REST API


#### LED

```
GET /LED/{index}/[ON,OFF]
```

```
GET /LED/{index}/BLINK/{count}
```

```
GET /LED/{index}/FREQ/{milliseq}
```

#### STEPPER

```
GET /STEPPER/{index}/{steps}
```

```
GET /STEPPER/{index}/FREQ/{milliseq}
```

