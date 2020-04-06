/*
 * Copyright (c) 2020, Luc Yriarte
 * License: BSD <http://www.opensource.org/licenses/bsd-license.php>
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>


/* **** **** **** **** **** ****
 * Constants
 * **** **** **** **** **** ****/

#define BPS_HOST 9600

#define serverPORT 80
#define WIFI_CLIENT_DELAY 500
#define WIFI_CONNECT_DELAY 3000
#define WIFI_CONNECT_RETRY 5

#define REQ_BUFFER_SIZE 1024

/* **** **** **** **** **** ****
 * Global variables
 * **** **** **** **** **** ****/

/*
 * WiFi
 */

WiFiServer wifiServer(serverPORT);
WiFiClient wifiClient;

typedef struct {
  char * SSID;
  char * passwd;
  IPAddress address;
  IPAddress gateway;
  IPAddress netmask;
} wifiNetInfo;

wifiNetInfo networks[] = {
  {
    "network1",
    "password",
    IPAddress(192,168,0,2),
    IPAddress(192,168,0,1),
    IPAddress(255,255,255,0)
  },
  {
    "network2",
    "password",
    IPAddress(0,0,0,0),
    IPAddress(0,0,0,0),
    IPAddress(0,0,0,0)
  }
};


int wifiStatus = WL_IDLE_STATUS;
char hostnameSSID[] = "ESP_XXXXXX";
char wifiMacStr[] = "00:00:00:00:00:00";
byte wifiMacBuf[6];

/* 
 * http request buffer
 */
char reqBuffer[REQ_BUFFER_SIZE];
int reqBufferIndex=0;


/* **** **** **** **** **** ****
 * Robot commands
 * **** **** **** **** **** ****/

/*
 * LED
 */
 
typedef struct {
  int gpio;
  int state;
} LEDInfo;

LEDInfo ledInfos[] = {
	{
		2,
		LOW
	}
};


/*
 * STEPPER
 */
 
typedef struct {
  int gpios[4];
  int steps;
} STEPPERInfo;

STEPPERInfo stepperInfos[] = {
	{
		{12,13,14,15},
		0
	},
	{
		{0,4,5,16},
		0
	}
};

byte steps8[] = {
  HIGH,  LOW,  LOW,  LOW,
  HIGH, HIGH,  LOW,  LOW,
   LOW, HIGH,  LOW,  LOW,
   LOW, HIGH, HIGH,  LOW,
   LOW,  LOW, HIGH,  LOW, 
   LOW,  LOW, HIGH, HIGH,
   LOW,  LOW,  LOW, HIGH,
  HIGH,  LOW,  LOW, HIGH,
};

#define STEPPERMS 5
void step8(int pin1, int pin2, int pin3, int pin4) {
	int i=0;
	while (i<32) {
		digitalWrite(pin1, steps8[i++]);
		digitalWrite(pin2, steps8[i++]);
		digitalWrite(pin3, steps8[i++]);
		digitalWrite(pin4, steps8[i++]);
		delay(STEPPERMS);
	}
} 


/* **** **** **** **** **** ****
 * Functions
 * **** **** **** **** **** ****/

void setup() {
	int i,j;
	for (i=0; i < sizeof(ledInfos) / sizeof(LEDInfo); i++)
		pinMode(ledInfos[i].gpio, OUTPUT);
	for (i=0; i < sizeof(stepperInfos) / sizeof(STEPPERInfo); i++)
		for (j=0; j < 4; j++)
			pinMode(stepperInfos[i].gpios[j], OUTPUT);
	Serial.begin(BPS_HOST);
	wifiMacInit();
	Serial.print("WiFi.macAddress: ");
	Serial.println(wifiMacStr);
}

/*
 * WiFi
 */

void wifiMacInit() {
	WiFi.macAddress(wifiMacBuf);
	int i,j,k;
	j=0; k=4;
	for (i=0; i<6; i++) {
		wifiMacStr[j] = '0' + (wifiMacBuf[i] >> 4);
		if (wifiMacStr[j] > '9')
			wifiMacStr[j] += 7;
		if (i>2)
			hostnameSSID[k++] = wifiMacStr[j];
		++j;
		wifiMacStr[j] = '0' + (wifiMacBuf[i] & 0x0f);
		if (wifiMacStr[j] > '9')
			wifiMacStr[j] += 7;
		if (i>2)
			hostnameSSID[k++] = wifiMacStr[j];
		j+=2;
	}
}

bool wifiConnect(int retry) {
	int i,n;
	wifiStatus = WiFi.status();
	if (wifiStatus == WL_CONNECTED)
		return true;
	n = sizeof(networks) / sizeof(wifiNetInfo);
	for (i=0; i<n; i++) {
		if (wifiNetConnect(&networks[i], retry))
			return true;
	}
	WiFi.disconnect();
	wifiStatus = WiFi.status();
	return false;
}

bool wifiNetConnect(wifiNetInfo *net, int retry) {
	Serial.print("Connecting to: ");
	Serial.println(net->SSID);
	WiFi.config(net->address, net->gateway, net->netmask);  
	wifiStatus = WiFi.begin(net->SSID, net->passwd);
	Serial.print("trying..");
	while (wifiStatus != WL_CONNECTED && retry > 0) {
		retry--;
		Serial.print(".");
		delay(WIFI_CONNECT_DELAY);
		wifiStatus = WiFi.status();
	}
	Serial.println();
	if (wifiStatus == WL_CONNECTED) {
		Serial.print("WiFi client IP Address: ");
		Serial.println(WiFi.localIP());
		if (MDNS.begin(hostnameSSID)) {
			Serial.print("Registered mDNS hostname: ");
			Serial.println(hostnameSSID);
  		}
	}
	return wifiStatus == WL_CONNECTED;
}


/*
 * Robot commands
 */

void updateLEDStatus(int index) {
	digitalWrite(ledInfos[index].gpio, ledInfos[index].state);
}

bool handleLEDRequest(const char * req) {
	String strReq = req;
	int index = strReq.toInt();
	if (index < 0 || index >= sizeof(ledInfos) / sizeof(LEDInfo))
		return false;
	if (strReq.endsWith("/ON"))
		ledInfos[index].state = HIGH;
	else if (strReq.endsWith("/OFF"))
		ledInfos[index].state = LOW;
	else
		return false;
	updateLEDStatus(index);
	return true;
}

void updateSTEPPERStatus(int index) {
	while (stepperInfos[index].steps) {
		if (stepperInfos[index].steps > 0) {
			step8(stepperInfos[index].gpios[0],stepperInfos[index].gpios[1],stepperInfos[index].gpios[2],stepperInfos[index].gpios[3]);
			stepperInfos[index].steps--;
		}
		else {
			step8(stepperInfos[index].gpios[3],stepperInfos[index].gpios[2],stepperInfos[index].gpios[1],stepperInfos[index].gpios[0]);
			stepperInfos[index].steps++;
		}
	}
}

bool handleSTEPPERRequest(const char * req) {
	String strReq = req;
	int index = strReq.toInt();
	if (index < 0 || index >= sizeof(stepperInfos) / sizeof(STEPPERInfo))
		return false;
	stepperInfos[index].steps = strReq.substring(strReq.indexOf("/")+1).toInt();
	updateSTEPPERStatus(index);
	return true;
}

/*
 * HTTP request main dispatch
 */

bool handleHttpRequest(const char * req) {
	if (req == NULL)
		return false;
	String strReq = req;
	if (! strReq.startsWith("GET /"))
		return false;
	strReq = strReq.substring(5, strReq.indexOf(" HTTP"));
	wifiClient.println(strReq);
	if (strReq.startsWith("LED/"))
		return handleLEDRequest(strReq.substring(4).c_str());
	if (strReq.startsWith("STEPPER/"))
		return handleSTEPPERRequest(strReq.substring(8).c_str());
	return false;
}

/* 
 * Main loop
 */

void loop() {
	while (!wifiConnect(WIFI_CONNECT_RETRY))
		delay(1000);
	wifiServer.begin();
	delay(200);
	wifiClient = wifiServer.available();
	if (wifiClient && wifiClient.connected()) {
		delay(100);
		reqBufferIndex = 0;
		while (wifiClient.available() && reqBufferIndex < REQ_BUFFER_SIZE-1) {
			reqBuffer[reqBufferIndex++] = wifiClient.read();
		}
		reqBuffer[reqBufferIndex] = 0;
		handleHttpRequest(reqBuffer);
		delay(WIFI_CLIENT_DELAY);
		wifiClient.stop();
	}
}
