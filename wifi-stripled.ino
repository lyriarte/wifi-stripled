/*
 * Copyright (c) 2020, Luc Yriarte
 * License: BSD <http://www.opensource.org/licenses/bsd-license.php>
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#define FONT_4x6_FIXED_MEDIUM
#define FONT_5x6_FIXED_MEDIUM
#define FONT_5x7_FIXED_MEDIUM
#define FONT_5x8_FIXED_MEDIUM
#define FONT_6x8_FIXED_MEDIUM
#define FONT_6x9_FIXED_MEDIUM
#define FONT_6x10_FIXED_MEDIUM
#define FONT_6x12_FIXED_MEDIUM
#define FONT_6x13_FIXED_MEDIUM
#define FONT_7x13_FIXED_BOLD
#define FONT_8x13_FIXED_MEDIUM
#define FONT_8x13_FIXED_BOLD
#include <StripDisplay.h>

#include "XBMFont.h"
#include "qdbmp.h"

/* **** **** **** **** **** ****
 * Constants
 * **** **** **** **** **** ****/

#define BPS_HOST 9600

#define serverPORT 80
#define WIFI_CLIENT_DELAY_MS 500
#define WIFI_CLIENT_CONNECTED_DELAY_MS 100
#define WIFI_SERVER_DELAY_MS 200
#define WIFI_CONNECT_DELAY_MS 3000
#define WIFI_CONNECT_RETRY_DELAY_MS 1000
#define WIFI_CONNECT_RETRY 5

#define REQ_BUFFER_SIZE 1024

#define MAIN_LOOP_POLL_MS 10
#define MINIMUM_UPDATE_MS 2

#define STRIPLED_SCREEN

#define STRIPLED_GPIO_0	5
#define BITMAP_W_0	32
#define BITMAP_H_0	16
#define STRIPLED_W_0	32
#define STRIPLED_H_0	8

StripLEDPanel panels_0[] = {
{
	0,
	0,
	0,
	STRIPLED_W_0,
	STRIPLED_H_0,
	WRAP_COLUMNS
},
{
	STRIPLED_W_0*STRIPLED_H_0,
	0,
	STRIPLED_H_0,
	STRIPLED_W_0,
	STRIPLED_H_0,
	WRAP_COLUMNS
}
};

#define N_PANELS_0 (sizeof(panels_0) / sizeof(StripLEDPanel))



#define MSG_SCROLL_START_MS 3000
#define MSG_SCROLL_MS 50

/* **** **** **** **** **** ****
 * Global variables
 * **** **** **** **** **** ****/

/* 
 * XBM font
 */

#define N_FONT 12
XBMFont * fontPtrs[N_FONT] = {
	&fixedMedium_4x6,
	&fixedMedium_5x6,
	&fixedMedium_5x7,
	&fixedMedium_5x8,
	&fixedMedium_6x8,
	&fixedMedium_6x9,	
	&fixedMedium_6x10,	
	&fixedMedium_6x12,	
	&fixedMedium_6x13,	
	&fixedBold_7x13,	
	&fixedMedium_8x13,	
	&fixedBold_8x13	
};

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

#define N_NETWORKS (sizeof(networks) / sizeof(wifiNetInfo))

int i_network = -1;
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
 * Time management
 */ 

typedef struct {
	int updated_ms;
	int poll_ms;
} POLLInfo;


/*
 * STRIPLED
 */
 
typedef struct {
	StripDisplay * stripP;
} STRIPLEDInfo;

STRIPLEDInfo stripledInfos[] = {
	{
	new StripDisplay(
		STRIPLED_GPIO_0,
		BITMAP_W_0,
		BITMAP_H_0,
		(CRGB*) malloc(STRIPLED_W_0*STRIPLED_H_0*N_PANELS_0*sizeof(CRGB)),
		panels_0,
		N_PANELS_0
		)
	}
};

int stripledCount(STRIPLEDInfo * stripledInfosP) {
	return stripledInfosP->stripP->getWidth() * stripledInfosP->stripP->getHeight();
}

#define N_STRIPLED (sizeof(stripledInfos) / sizeof(STRIPLEDInfo))
int i_stripled = N_STRIPLED-1;

/*
 * LED
 */
 
typedef struct {
	POLLInfo pollInfo;
	int gpio;
	int state;
	int blink;
	int blink_on_ms;
	int blink_off_ms;
} LEDInfo;

LEDInfo ledInfos[] = {
	{
		{0, 0},
		2,	// D4 led
		LOW,
		-1,
		4950,50
	}
};

#define N_LED (sizeof(ledInfos) / sizeof(LEDInfo))


/*
 * MESSAGE
 */
 
typedef struct {
	int strip_index;
	POLLInfo pollInfo;
	String text;
	int align;
	int offset;
} MESSAGEInfo;

MESSAGEInfo messageInfos[] = {
	{
		0,
		{0, MSG_SCROLL_MS},
		"",
		ALIGN_CENTER,
		0
	}
};
int i_message = 0;

/*
 * ANIM
 */
 
 enum {
	ANIM_NONE,
	ANIM_ROTATE
};

typedef struct {
	int strip_index;
	POLLInfo pollInfo;
	int kind;
} ANIMInfo;

ANIMInfo animInfos[] = {
	{
		0,
		{0, MSG_SCROLL_MS},
		ANIM_NONE
	},
	{
		1,
		{0, MSG_SCROLL_MS},
		ANIM_NONE
	}
};



/* **** **** **** **** **** ****
 * Functions
 * **** **** **** **** **** ****/

void setup() {
	int i,j;
	for (i=0; i < N_LED; i++)
		pinMode(ledInfos[i].gpio, OUTPUT);
	FastLED.addLeds<NEOPIXEL,STRIPLED_GPIO_0>(stripledInfos[0].stripP->getLeds(), STRIPLED_W_0*STRIPLED_H_0*N_PANELS_0);
	Serial.begin(BPS_HOST);
	wifiMacInit();
	stripledInfos[0].stripP->setup(fontPtrs[0]);
	stripledInfos[0].stripP->setText(wifiMacStr);
	setMessageDefaults();
	Serial.print("WiFi.macAddress: ");
	Serial.println(wifiMacStr);
}

void setMessageDefaults() {
	stripledInfos[i_message].stripP->setFont(fontPtrs[2]);
	stripledInfos[i_message].stripP->setAlignment(ALIGN_CENTER);
	stripledInfos[i_message].stripP->setBgColor(CRGB(0,0,0));
	stripledInfos[i_message].stripP->setFgColor(CRGB(4,4,4));
}

/*
 * Time management
 */

bool pollDelay(int poll_ms, int from_ms) {
	int elapsed_ms = millis() - from_ms;
	if (elapsed_ms < poll_ms) {
		FastLED.delay(poll_ms - elapsed_ms);
		return true;
	}
	return false;
}

bool updatePollInfo(POLLInfo * pollInfoP) {
	int current_ms = millis();
	if (pollInfoP->poll_ms <= current_ms - pollInfoP->updated_ms) {
		pollInfoP->updated_ms = current_ms;
		return true;
	}
	return false;
}

bool handlePollInfoRequest(const char * req, POLLInfo * pollInfoP) {
	String strReq = req;
	pollInfoP->poll_ms = strReq.toInt();
	return true;
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
	wifiStatus = WiFi.status();
	if (i_network >= 0 && wifiStatus == WL_CONNECTED)
		return true;
	for (i_network=0; i_network<N_NETWORKS; i_network++) {
		if (wifiNetConnect(&networks[i_network], retry))
			return true;
	}
	WiFi.disconnect();
	wifiStatus = WiFi.status();
	i_network = -1;
	return false;
}

bool wifiNetConnect(wifiNetInfo *net, int retry) {
	Serial.print("Connecting to: ");
	Serial.println(net->SSID);
#ifdef STRIPLED_SCREEN
	handleSSIDRequest();
#endif
	WiFi.config(net->address, net->gateway, net->netmask);  
	wifiStatus = WiFi.begin(net->SSID, net->passwd);
	Serial.print("trying..");
	while (wifiStatus != WL_CONNECTED && retry > 0) {
		retry--;
		Serial.print(".");
		FastLED.delay(WIFI_CONNECT_DELAY_MS);
		wifiStatus = WiFi.status();
	}
	Serial.println();
	if (wifiStatus == WL_CONNECTED) {
		Serial.print("WiFi client IP Address: ");
		Serial.println(WiFi.localIP());
		net->address = WiFi.localIP();
#ifdef STRIPLED_SCREEN
		handleIPRequest();
#endif
		if (MDNS.begin(hostnameSSID)) {
			Serial.print("Registered mDNS hostname: ");
			Serial.println(hostnameSSID);
  		}
	}
	return wifiStatus == WL_CONNECTED;
}


/*
 * Blink led
 */

void updateLEDStatus(int index) {
	if (ledInfos[index].blink) {
		if (!updatePollInfo(&(ledInfos[index].pollInfo)))
			return;
		if (ledInfos[index].state == LOW) {
			ledInfos[index].state = HIGH;
			ledInfos[index].pollInfo.poll_ms = ledInfos[index].blink_on_ms;
		}
		else {
			ledInfos[index].state = LOW;
			ledInfos[index].pollInfo.poll_ms = ledInfos[index].blink_off_ms;
		}
	}
	if (ledInfos[index].blink > 0)
		ledInfos[index].blink--;
	digitalWrite(ledInfos[index].gpio, ledInfos[index].state);
}

bool handleLEDRequest(const char * req) {
	String strReq = req;
	int index = strReq.toInt();
	if (index < 0 || index >= N_LED)
		return false;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	if (strReq.startsWith("POLL/")) {
		handlePollInfoRequest(strReq.substring(5).c_str(), &(ledInfos[index].pollInfo));
		ledInfos[index].blink_on_ms = ledInfos[index].pollInfo.poll_ms;
		return true;
	}
	else
		return false;
	return true;
}


/*
 * Message text
 */

void updateMessageScroll(int index) {
	if (!updatePollInfo(&(messageInfos[index].pollInfo)))
		return;
  if (stripledInfos[index].stripP->getTextWidth() < stripledInfos[index].stripP->getWidth())
    return;
	messageInfos[index].offset = (messageInfos[index].offset + 1) % stripledInfos[index].stripP->getTextWidth();
	stripledInfos[index].stripP->displayText(messageInfos[index].offset);
}

void updateMessageText(int index, String text) {
	messageInfos[index].text = text;
	messageInfos[index].offset = 0;
	stripledInfos[index].stripP->setText(text);
	stripledInfos[index].stripP->displayText(messageInfos[index].offset);
	if (stripledInfos[index].stripP->getTextWidth() > stripledInfos[index].stripP->getWidth())
		FastLED.delay(MSG_SCROLL_START_MS);
}

void updateMessageAlign(int index, int align) {
	stripledInfos[index].stripP->setAlignment(align);
	messageInfos[index].offset = 0;
	stripledInfos[index].stripP->displayText(messageInfos[index].offset);
}

void updateMessageBg(int index, CRGB bg) {
	stripledInfos[index].stripP->setBgColor(bg);
	stripledInfos[index].stripP->displayText(messageInfos[index].offset);
}

void updateMessageFg(int index, CRGB fg) {
	stripledInfos[index].stripP->setFgColor(fg);
	stripledInfos[index].stripP->displayText(messageInfos[index].offset);
}


/*
 * Animation
 */

void rotateStripledDisplay(int index) {
	CRGB carry = stripledInfos[index].stripP->getLeds()[0];
	for (int i=0; i<stripledCount(&stripledInfos[index])-1; i++) {
		stripledInfos[index].stripP->getLeds()[i] = stripledInfos[index].stripP->getLeds()[i+1];
	}
	stripledInfos[index].stripP->getLeds()[stripledCount(&stripledInfos[index])-1] = carry;
}

void updateAnimation(int index) {
	if (animInfos[index].kind == ANIM_NONE || !updatePollInfo(&(animInfos[index].pollInfo)))
		return;
	switch (animInfos[index].kind) {
		case ANIM_ROTATE:
			rotateStripledDisplay(animInfos[index].strip_index);
			break;
	}
}


/*
 * Handle REST commands
 */

String decodeUrl(String encoded) {
	String decoded = "";
	int i = 0;
	int j = encoded.indexOf("%", i);
	while (j >= 0) {
		decoded += encoded.substring(i,j) + String((char)strtol(encoded.substring(j+1,j+3).c_str(), NULL, 16));
		i=j+3;
		j = encoded.indexOf("%", i);
	}
	decoded += encoded.substring(i);
	return decoded;
}

bool handleSTRIPRequest(const char * req) {
	String strReq = req;
	int index = strReq.toInt();
	if (index < 0 || index >= N_STRIPLED)
		return false;
	i_stripled = index;
	index = strReq.indexOf("/");
	if (index > -1)
		return dispatchHttpRequest(strReq.substring(index+1).c_str());
	return true;
}

bool handleFONTRequest(const char * req) {
	String strReq = req;
	int index = strReq.toInt();
	if (index < 0 || index >= N_FONT)
		return false;
	stripledInfos[i_message].stripP->setFont(fontPtrs[index]);
	updateMessageText(i_message, messageInfos[i_message].text);
	return true;
}

bool handleSTRIPLEDRequest(const char * req) {
	String strReq = req;
	int index = strReq.toInt();
	if (index < 0 || index >= stripledCount(&stripledInfos[i_stripled]))
		return false;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	if (strReq.startsWith("RGB/")) {
		int rgb = (int) strtol(strReq.substring(4).c_str(), NULL, 16);
		stripledInfos[i_stripled].stripP->getLeds()[index] = CRGB(rgb >> 16, rgb >> 8 & 0xFF, rgb & 0xFF);
		return true;
	}
	else
		return false;
	return true;
}

bool handleGRADIENTRequest(const char * req) {
	String strReq = req;
	int src = strReq.toInt();
	if (src < 0 || src >= stripledCount(&stripledInfos[i_stripled]))
		return false;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	int srcrgb = (int) strtol(strReq.substring(0,6).c_str(), NULL, 16);
	int srcr = srcrgb >> 16, srcg = srcrgb >> 8 & 0xFF, srcb = srcrgb & 0xFF;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	int dst = strReq.toInt();
	if (dst <= src || dst >= stripledCount(&stripledInfos[i_stripled]))
		return false;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	int dstrgb = (int) strtol(strReq.substring(0,6).c_str(), NULL, 16);
	int dstr = dstrgb >> 16, dstg = dstrgb >> 8 & 0xFF, dstb = dstrgb & 0xFF;
	int dr = dstr-srcr, dg = dstg-srcg, db = dstb - srcb;
	for (int i=src; i<=dst; i++) {
		stripledInfos[i_stripled].stripP->getLeds()[i] = CRGB(min(255,max(0,dstr - dr*(dst-i)/(dst-src))), min(255,max(0,dstg - dg*(dst-i)/(dst-src))), min(255,max(0,dstb - db*(dst-i)/(dst-src))));
	}
  return true;
}

bool handleANIMRequest(const char * req) {
	String strReq = req;
	animInfos[i_stripled].kind = ANIM_NONE;
	if (strReq == "ROTATE")
		animInfos[i_stripled].kind = ANIM_ROTATE;
	return true;
}

bool handleMSGRequest(const char * req) {
	String strReq = req;
	String strMsg = decodeUrl(strReq.substring(strReq.indexOf("/")+1));
	updateMessageText(i_message, strMsg);
	return true;
}

bool handleSCROLLRequest(const char * req) {
	return handlePollInfoRequest(req, &(messageInfos[i_message].pollInfo));
}

bool handleALIGNRequest(const char * req) {
	String strReq = req;
	if (strReq == "CENTER")
		updateMessageAlign(i_message, ALIGN_CENTER);
	else if (strReq == "LEFT")
		updateMessageAlign(i_message, ALIGN_LEFT);
	else if (strReq == "RIGHT")
		updateMessageAlign(i_message, ALIGN_RIGHT);
	else
		return false;
	return true;
}

bool handleBGRequest(const char * req) {
	String strReq = req;
	int rgb = (int) strtol(strReq.substring(0,6).c_str(), NULL, 16);
	updateMessageBg(i_message, CRGB(rgb >> 16, rgb >> 8 & 0xFF, rgb & 0xFF));
	return true;
}

bool handleFGRequest(const char * req) {
	String strReq = req;
	int rgb = (int) strtol(strReq.substring(0,6).c_str(), NULL, 16);
	updateMessageFg(i_message, CRGB(rgb >> 16, rgb >> 8 & 0xFF, rgb & 0xFF));
	return true;
}

bool handleSSIDChangeRequest(const char * req) {
	String strReq = req;
	int networkReq = strReq.toInt();
	if (networkReq < 0 || networkReq >= N_NETWORKS)
		return false;
	i_network = networkReq;
	return wifiNetConnect(&networks[i_network], WIFI_CONNECT_RETRY);
}

bool handleSSIDRequest() {
	if (i_network < 0 || i_network >= N_NETWORKS)
		return false;
	messageInfos[i_message].align = ALIGN_LEFT;
	stripledInfos[i_message].stripP->setBgColor(CRGB(0,0,0));
	stripledInfos[i_message].stripP->setFgColor(CRGB(8,0,16));
	updateMessageText(i_message, networks[i_network].SSID);
	return true;
}

bool handleIPRequest() {
	if (i_network < 0 || i_network >= N_NETWORKS)
		return false;
	messageInfos[i_message].align = ALIGN_LEFT;
	stripledInfos[i_message].stripP->setBgColor(CRGB(0,0,0));
	stripledInfos[i_message].stripP->setFgColor(CRGB(0,8,16));
	updateMessageText(i_message, networks[i_network].address.toString());
	return true;
}



/*
 * HTTP request main dispatch
 */

void replyHttpSuccess(String strResult) {
	wifiClient.println("HTTP/1.1 200 OK");
	wifiClient.println("Content-Type: text/plain");
	wifiClient.println("Access-Control-Allow-Origin: *");
	wifiClient.println("Connection: close");
	wifiClient.println();
	wifiClient.print(strResult);
	wifiClient.println();
}

void replyHttpError(String strError) {
	wifiClient.println("HTTP/1.1 400 Bad Request");
	wifiClient.println("Access-Control-Allow-Origin: *");
	wifiClient.println();
	wifiClient.print(strError);
	wifiClient.println();
}

bool dispatchHttpRequest(const char * req) {
  String strReq = req;
	bool result = false;
	if (strReq.startsWith("LED/"))
		result = handleLEDRequest(strReq.substring(4).c_str());
	else if (strReq.startsWith("STRIP/"))
		result = handleSTRIPRequest(strReq.substring(6).c_str());
	else if (strReq.startsWith("FONT/"))
		result = handleFONTRequest(strReq.substring(5).c_str());
	else if (strReq.startsWith("STRIPLED/"))
		result = handleSTRIPLEDRequest(strReq.substring(9).c_str());
	else if (strReq.startsWith("GRADIENT/"))
		result = handleGRADIENTRequest(strReq.substring(9).c_str());
	else if (strReq.startsWith("ANIM/"))
		result = handleANIMRequest(strReq.substring(5).c_str());
	else if (strReq.startsWith("MSG/"))
		result = handleMSGRequest(strReq.substring(4).c_str());
	else if (strReq.startsWith("SCROLL/"))
		result = handleSCROLLRequest(strReq.substring(7).c_str());
	else if (strReq.startsWith("ALIGN/"))
		result = handleALIGNRequest(strReq.substring(6).c_str());
	else if (strReq.startsWith("BG/"))
		result = handleBGRequest(strReq.substring(3).c_str());
	else if (strReq.startsWith("FG/"))
		result = handleFGRequest(strReq.substring(3).c_str());
	else if (strReq.startsWith("SSID/"))
		result = handleSSIDChangeRequest(strReq.substring(5).c_str());
	else if (strReq.startsWith("SSID"))
		result = handleSSIDRequest();
	else if (strReq.startsWith("IP"))
		result = handleIPRequest();
	return result;
}

bool handleHttpRequest(const char * req) {
	if (req == NULL)
		return false;
	String strReq = req;
	if (! strReq.startsWith("GET /"))
		return false;
	strReq = strReq.substring(5, strReq.indexOf(" HTTP"));
	bool result = dispatchHttpRequest(strReq.c_str());
	if (result)
		replyHttpSuccess(strReq);
	else
		replyHttpError("ERROR");
	return result;
}



/* 
 * XBM font
 */

void drawTextBitmap(BMP* bmp, String text, XBMFont font, unsigned int x0, unsigned int y0, CRGB crgb) {
	unsigned int x = x0;
	for (int i=0; i<text.length(); i++) {
		char c = text.charAt(i);
		for (int j=0; j<font.getWidth(); j++) {
			for (int k=0; k<font.getHeight(); k++)
				if (font.getPixel(c,k,j))
					BMP_SetPixelRGB(bmp, x+j, y0+k, crgb.r, crgb.g, crgb.b);
		}
		x += font.getWidth();
	}
}



/* 
 * Main loop
 */


void updateStatus() {
	int deviceIndex;
	for (deviceIndex=0; deviceIndex<N_LED; deviceIndex++)
		updateLEDStatus(deviceIndex);
	for (deviceIndex=0; deviceIndex<N_STRIPLED; deviceIndex++) {
		updateAnimation(deviceIndex);
	}
	updateMessageScroll(i_message);
}

void delayWithUpdateStatus(int delay_ms) {
	int start_ms = millis();
	while (millis() - start_ms < delay_ms) {
		updateStatus();
		FastLED.delay(MINIMUM_UPDATE_MS);
	}
}

void delayedWifiClientStop(int start_ms) {
	while (wifiClient && wifiClient.connected() && millis() < start_ms + WIFI_CLIENT_DELAY_MS) {
		updateStatus();
		FastLED.delay(MINIMUM_UPDATE_MS);
	}
	if (wifiClient)
		wifiClient.stop();
}

void loop() {
	int start_loop_ms;
	delayWithUpdateStatus(1000);
	while (!wifiConnect(WIFI_CONNECT_RETRY))
		delayWithUpdateStatus(WIFI_CONNECT_RETRY_DELAY_MS);
	setMessageDefaults();
	wifiServer.begin();
	delayWithUpdateStatus(WIFI_SERVER_DELAY_MS);
	while (wifiStatus == WL_CONNECTED) {
		start_loop_ms = millis();
		wifiClient = wifiServer.available();
		if (wifiClient && wifiClient.connected()) {
			delayWithUpdateStatus(WIFI_CLIENT_CONNECTED_DELAY_MS);
			reqBufferIndex = 0;
			while (wifiClient.available() && reqBufferIndex < REQ_BUFFER_SIZE-1) {
				reqBuffer[reqBufferIndex++] = wifiClient.read();
			}
			reqBuffer[reqBufferIndex] = 0;
			handleHttpRequest(reqBuffer);
			delayedWifiClientStop(millis());
		}
		updateStatus();
		pollDelay(MAIN_LOOP_POLL_MS, start_loop_ms);
		wifiStatus = WiFi.status();
	}
}
