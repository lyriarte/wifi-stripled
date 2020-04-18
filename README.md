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
GET /LED/{index}/POLL/{millis}
```

#### SERVO

```
GET /SERVO/{index}/{angle}
```

#### STEPPER

```
GET /STEPPER/{index}/{steps}
```

```
GET /STEPPER/{index}/POLL/{millis}
```

