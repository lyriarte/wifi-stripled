/*
 * Copyright (c) 2020, Luc Yriarte
 * License: BSD <http://www.opensource.org/licenses/bsd-license.php>
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <FastLED.h>

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

#define STRIPLED_GPIO	5
#define STRIPLED_W    71
#define STRIPLED_H    6
#define N_STRIPLED    (STRIPLED_W * STRIPLED_H)

#define SPLASH_SCREEN_FILE "/test.bmp"
#define SPLASH_SCREEN_DELAY_MS 500

/* **** **** **** **** **** ****
 * Global variables
 * **** **** **** **** **** ****/

/* 
 * XBM font
 */

extern XBMFont fixedMedium_5x7;
#define DEFAULT_FONT fixedMedium_5x7
enum {
	ALIGN_LEFT,
	ALIGN_CENTER,
	ALIGN_RIGHT
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
 
CRGBArray<N_STRIPLED> leds;

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
	String text;
	int align;
	CRGB bg;
	CRGB fg;
	BMP* bmp;
	int offset;
} MESSAGEInfo;

MESSAGEInfo messageInfo = {
	String(""),
	ALIGN_CENTER,
	CRGB(0,0,0),
	CRGB(4,4,4),
	NULL,
	0
};


/* **** **** **** **** **** ****
 * Functions
 * **** **** **** **** **** ****/

void setup() {
	int i,j;
	for (i=0; i < N_LED; i++)
		pinMode(ledInfos[i].gpio, OUTPUT);
	FastLED.addLeds<NEOPIXEL,STRIPLED_GPIO>(leds, N_STRIPLED);
	Serial.begin(BPS_HOST);
	SPIFFS.begin();
	displaySplashScreen();
	wifiMacInit();
	BMP* bmp = newTextBitmap(hostnameSSID, DEFAULT_FONT);
	displayTextBitmap(hostnameSSID, DEFAULT_FONT, CRGB(0,0,0), CRGB(4,8,16), ALIGN_CENTER, bmp);
	BMP_Free(bmp);
	Serial.print("WiFi.macAddress: ");
	Serial.println(wifiMacStr);
}

void displaySplashScreen() {
	displayBitmapFile(SPLASH_SCREEN_FILE);
	FastLED.delay(SPLASH_SCREEN_DELAY_MS);
}

void setMessageDefaults() {
	messageInfo.align = ALIGN_CENTER;
	messageInfo.bg = CRGB(0,0,0);
	messageInfo.fg = CRGB(4,4,4);
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
	handleSSIDRequest();
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
		handleIPRequest();
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


/*
 * Message text
 */

void updateMessageText(String text) {
	messageInfo.text = text;
	if (messageInfo.bmp != NULL)
		BMP_Free(messageInfo.bmp);
	messageInfo.bmp = newTextBitmap(messageInfo.text, DEFAULT_FONT);
	fillStripledDisplay(messageInfo.bg);
	displayTextBitmap(messageInfo.text, DEFAULT_FONT, messageInfo.bg, messageInfo.fg, messageInfo.align, messageInfo.bmp, messageInfo.offset);
}

void updateMessageAlign(int align) {
	messageInfo.align = align;
	fillStripledDisplay(messageInfo.bg);
	displayTextBitmap(messageInfo.text, DEFAULT_FONT, messageInfo.bg, messageInfo.fg, messageInfo.align, messageInfo.bmp, messageInfo.offset);
}

void updateMessageBg(CRGB bg) {
	messageInfo.bg = bg;
	fillStripledDisplay(messageInfo.bg);
	displayTextBitmap(messageInfo.text, DEFAULT_FONT, messageInfo.bg, messageInfo.fg, messageInfo.align, messageInfo.bmp, messageInfo.offset);
}

void updateMessageFg(CRGB fg) {
	messageInfo.fg = fg;
	displayTextBitmap(messageInfo.text, DEFAULT_FONT, messageInfo.bg, messageInfo.fg, messageInfo.align, messageInfo.bmp, messageInfo.offset);
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

bool handleSTRIPLEDRequest(const char * req) {
	String strReq = req;
	int index = strReq.toInt();
	if (index < 0 || index >= N_STRIPLED)
		return false;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	if (strReq.startsWith("RGB/")) {
		int rgb = (int) strtol(strReq.substring(4).c_str(), NULL, 16);
		leds[index] = CRGB(rgb >> 16, rgb >> 8 & 0xFF, rgb & 0xFF);
		return true;
	}
	else
		return false;
	return true;
}

bool handleGRADIENTRequest(const char * req) {
	String strReq = req;
	int src = strReq.toInt();
	if (src < 0 || src >= N_STRIPLED)
		return false;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	int srcrgb = (int) strtol(strReq.substring(0,6).c_str(), NULL, 16);
	int srcr = srcrgb >> 16, srcg = srcrgb >> 8 & 0xFF, srcb = srcrgb & 0xFF;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	int dst = strReq.toInt();
	if (dst <= src || dst >= N_STRIPLED)
		return false;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	int dstrgb = (int) strtol(strReq.substring(0,6).c_str(), NULL, 16);
	int dstr = dstrgb >> 16, dstg = dstrgb >> 8 & 0xFF, dstb = dstrgb & 0xFF;
	int dr = dstr-srcr, dg = dstg-srcg, db = dstb - srcb;
	for (int i=src; i<=dst; i++) {
		leds[i] = CRGB(min(255,max(0,dstr - dr*(dst-i)/(dst-src))), min(255,max(0,dstg - dg*(dst-i)/(dst-src))), min(255,max(0,dstb - db*(dst-i)/(dst-src))));
	}
  return true;
}

bool handleMSGRequest(const char * req) {
	String strReq = req;
	String strMsg = decodeUrl(strReq.substring(strReq.indexOf("/")+1));
	updateMessageText(strMsg);
	return true;
}

bool handleALIGNRequest(const char * req) {
	String strReq = req;
	if (strReq == "CENTER")
		updateMessageAlign(ALIGN_CENTER);
	else if (strReq == "LEFT")
		updateMessageAlign(ALIGN_LEFT);
	else if (strReq == "RIGHT")
		updateMessageAlign(ALIGN_RIGHT);
	else
		return false;
	return true;
}

bool handleBGRequest(const char * req) {
	String strReq = req;
	int rgb = (int) strtol(strReq.substring(0,6).c_str(), NULL, 16);
	updateMessageBg(CRGB(rgb >> 16, rgb >> 8 & 0xFF, rgb & 0xFF));
	return true;
}

bool handleFGRequest(const char * req) {
	String strReq = req;
	int rgb = (int) strtol(strReq.substring(0,6).c_str(), NULL, 16);
	updateMessageFg(CRGB(rgb >> 16, rgb >> 8 & 0xFF, rgb & 0xFF));
	return true;
}

bool handleSPLASHSCREENRequest() {
	displayBitmapFile(SPLASH_SCREEN_FILE);
	return true;
}

bool handleSSIDRequest() {
	if (i_network < 0 || i_network >= N_NETWORKS)
		return false;
	messageInfo.align = ALIGN_LEFT;
	messageInfo.bg = CRGB(0,0,0);
	messageInfo.fg = CRGB(8,0,16);
	updateMessageText(networks[i_network].SSID);
	return true;
}

bool handleIPRequest() {
	if (i_network < 0 || i_network >= N_NETWORKS)
		return false;
	messageInfo.align = ALIGN_LEFT;
	messageInfo.bg = CRGB(0,0,0);
	messageInfo.fg = CRGB(0,8,16);
	updateMessageText(networks[i_network].address.toString());
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

bool handleHttpRequest(const char * req) {
	if (req == NULL)
		return false;
	String strReq = req;
	if (! strReq.startsWith("GET /"))
		return false;
	strReq = strReq.substring(5, strReq.indexOf(" HTTP"));
	bool result = false;
	if (strReq.startsWith("STRIPLED/"))
		result = handleSTRIPLEDRequest(strReq.substring(9).c_str());
	else if (strReq.startsWith("GRADIENT/"))
		result = handleGRADIENTRequest(strReq.substring(9).c_str());
	else if (strReq.startsWith("MSG/"))
		result = handleMSGRequest(strReq.substring(4).c_str());
	else if (strReq.startsWith("ALIGN/"))
		result = handleALIGNRequest(strReq.substring(6).c_str());
	else if (strReq.startsWith("BG/"))
		result = handleBGRequest(strReq.substring(3).c_str());
	else if (strReq.startsWith("FG/"))
		result = handleFGRequest(strReq.substring(3).c_str());
	else if (strReq.startsWith("SPLASHSCREEN"))
		result = handleSPLASHSCREENRequest();
	else if (strReq.startsWith("SSID"))
		result = handleSSIDRequest();
	else if (strReq.startsWith("IP"))
		result = handleIPRequest();
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
 * Text bitmap
 */

BMP* newTextBitmap(String text, XBMFont font) {
	BMP* bmp = BMP_Create(text.length() * font.getWidth(), font.getHeight(), 24);
	if (bmp == NULL) {
		Serial.println("Bitmap allocation failed");
		return bmp;
	}
	return bmp;
}

void fillBitmap(BMP* bmp, unsigned int x0, unsigned int y0, unsigned int dx, unsigned int dy, CRGB crgb) {
	for (unsigned int x=x0; x<x0+dx; x++)
		for (unsigned int y=y0; y<y0+dy; y++)
			BMP_SetPixelRGB(bmp, x, y, crgb.r, crgb.g, crgb.b);
}



/* 
 * Stripled bitmap
 */


void fillStripledDisplay(CRGB crgb) {
	BMP* bmp = BMP_Create(STRIPLED_W, STRIPLED_H, 24);
	fillBitmap(bmp, 0, 0, STRIPLED_W, STRIPLED_H, crgb);
	stripledBitmapBlit(bmp, 0, 0, 0, STRIPLED_W, STRIPLED_H);
	BMP_Free( bmp );
}

void stripledBitmapBlit(BMP* bmp, int i0, int ox, int oy, int dx, int dy) {
	int i=i0, ix, iy, w, h;
	w = min(min((int)BMP_GetWidth(bmp),STRIPLED_W),ox+dx);
	h = min(min((int)BMP_GetHeight(bmp),STRIPLED_H),oy+dy);
	byte r,g,b;
	for (int y=0; y<STRIPLED_H; y++) {
	  iy = y+oy;
	  for (int x=0; x<STRIPLED_W;x++) {
		ix = x+ox;
		if (ix>=0 && iy>=0 && ix<w && iy<h) {
		   BMP_GetPixelRGB(bmp,ix,iy,&r,&g,&b);
		   if (y%2 == 0) {
			   leds[i] = CRGB(r, g, b);
		   }
		   else {
			   leds[STRIPLED_W*(int)(i/STRIPLED_W) + STRIPLED_W - ((i%STRIPLED_W) + 1)] = CRGB(r, g, b);
		   }
	   }
	   ++i;
    }
  }
}

BMP* openBitmapFile(String path) {
  if (!SPIFFS.exists(path)) {
    Serial.println("File not found");
    return NULL;
  }
  BMP* bmp = BMP_ReadFile(path.c_str());
  if (bmp == NULL) {
    Serial.println("Bitmap file open error");
  }
  return bmp;
}

void displayBitmapFile(String path) {
	BMP* bmp = openBitmapFile(path);
	if (bmp == NULL)
		return;
	stripledBitmapBlit(bmp, 0, 0, 0, STRIPLED_W, STRIPLED_H);
	BMP_Free( bmp );
}

void displayTextBitmap(String text, XBMFont font, CRGB bg, CRGB fg, int align, BMP* bmp, int offset) {
	int i0 = 0;
	int width = min((int)BMP_GetWidth(bmp),STRIPLED_W);
	int height = min((int)BMP_GetHeight(bmp),STRIPLED_H);
	if (align == ALIGN_CENTER)
		i0 = (STRIPLED_W-width)/2;
	else if (align == ALIGN_RIGHT)
		i0 = (STRIPLED_W-width);
	fillBitmap(bmp, 0, 0, width, height, bg);
	drawTextBitmap(bmp, text, font, 0, 0, fg);
	stripledBitmapBlit(bmp, i0, offset, 0, width, height);
	if (freeBmp)
		BMP_Free( bmp );
}

/* 
 * Main loop
 */


void updateStatus() {
	int deviceIndex;
	for (deviceIndex=0; deviceIndex<N_LED; deviceIndex++)
		updateLEDStatus(deviceIndex);
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
