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

#define STRIPLED_SCREEN

#define STRIPLED_GPIO_0	5
#define STRIPLED_W_0    71
#define STRIPLED_H_0    6

#define SPLASH_SCREEN_FILE "/test.bmp"
#define SPLASH_SCREEN_DELAY_MS 500

#define MSG_SCROLL_START_MS 3000
#define MSG_SCROLL_MS 50

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
 
typedef struct {
	int gpio;
	int w;
	int h;
	CRGB *leds;
} STRIPLEDInfo;

STRIPLEDInfo stripledInfos[] = {
	{
		STRIPLED_GPIO_0,
		STRIPLED_W_0,
		STRIPLED_H_0,
		(CRGB*) malloc(STRIPLED_W_0*STRIPLED_H_0*sizeof(CRGB))
	}
};

int stripledCount(STRIPLEDInfo * stripledInfosP) {
	return stripledInfosP->w * stripledInfosP->h;
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
	POLLInfo pollInfo;
	String text;
	int align;
	CRGB bg;
	CRGB fg;
	BMP* bmp;
	int offset;
} MESSAGEInfo;

MESSAGEInfo messageInfo = {
	{0, MSG_SCROLL_MS},
	String(""),
	ALIGN_CENTER,
	CRGB(0,0,0),
	CRGB(4,4,4),
	NULL,
	0
};

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
	FastLED.addLeds<NEOPIXEL,STRIPLED_GPIO_0>(stripledInfos[0].leds, STRIPLED_W_0*STRIPLED_H_0);
	Serial.begin(BPS_HOST);
	SPIFFS.begin();
#ifdef STRIPLED_SCREEN
	displaySplashScreen();
#endif
	wifiMacInit();
#ifdef STRIPLED_SCREEN
	BMP* bmp = newTextBitmap(hostnameSSID, DEFAULT_FONT);
	displayTextBitmap(hostnameSSID, DEFAULT_FONT, CRGB(0,0,0), CRGB(4,8,16), ALIGN_CENTER, bmp, 0);
	BMP_Free(bmp);
#endif
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

void updateMessageScroll() {
	if (messageInfo.bmp == NULL || BMP_GetWidth(messageInfo.bmp) <= stripledInfos[i_stripled].w || !updatePollInfo(&(messageInfo.pollInfo)))
		return;
	messageInfo.offset = (messageInfo.offset + 1) % BMP_GetWidth(messageInfo.bmp);
	fillStripledDisplay(messageInfo.bg);
	stripledBitmapBlit(messageInfo.bmp, 0, messageInfo.offset, 0, stripledInfos[i_stripled].w, stripledInfos[i_stripled].h);
}

void updateMessageText(String text) {
	messageInfo.text = text;
	if (messageInfo.bmp != NULL)
		BMP_Free(messageInfo.bmp);
	messageInfo.bmp = newTextBitmap(messageInfo.text, DEFAULT_FONT);
	messageInfo.offset = 0;
	fillStripledDisplay(messageInfo.bg);
	displayTextBitmap(messageInfo.text, DEFAULT_FONT, messageInfo.bg, messageInfo.fg, messageInfo.align, messageInfo.bmp, messageInfo.offset);
	if (BMP_GetWidth(messageInfo.bmp) > stripledInfos[i_stripled].w)
		FastLED.delay(MSG_SCROLL_START_MS);
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
 * Animation
 */

void rotateStripledDisplay(int index) {
	CRGB carry = stripledInfos[index].leds[0];
	for (int i=0; i<stripledCount(&stripledInfos[index])-1; i++) {
		stripledInfos[index].leds[i] = stripledInfos[index].leds[i+1];
	}
	stripledInfos[index].leds[stripledCount(&stripledInfos[index])-1] = carry;
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
		stripledInfos[i_stripled].leds[index] = CRGB(rgb >> 16, rgb >> 8 & 0xFF, rgb & 0xFF);
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
		stripledInfos[i_stripled].leds[i] = CRGB(min(255,max(0,dstr - dr*(dst-i)/(dst-src))), min(255,max(0,dstg - dg*(dst-i)/(dst-src))), min(255,max(0,dstb - db*(dst-i)/(dst-src))));
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
	updateMessageText(strMsg);
	return true;
}

bool handleSCROLLRequest(const char * req) {
	return handlePollInfoRequest(req, &(messageInfo.pollInfo));
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
	if (strReq.startsWith("LED/"))
		result = handleLEDRequest(strReq.substring(4).c_str());
	else if (strReq.startsWith("STRIP/"))
		result = handleSTRIPRequest(strReq.substring(6).c_str());
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
	else if (strReq.startsWith("SPLASHSCREEN"))
		result = handleSPLASHSCREENRequest();
	else if (strReq.startsWith("SSID/"))
		result = handleSSIDChangeRequest(strReq.substring(5).c_str());
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
	BMP* bmp = BMP_Create(stripledInfos[i_stripled].w, stripledInfos[i_stripled].h, 24);
	fillBitmap(bmp, 0, 0, stripledInfos[i_stripled].w, stripledInfos[i_stripled].h, crgb);
	stripledBitmapBlit(bmp, 0, 0, 0, stripledInfos[i_stripled].w, stripledInfos[i_stripled].h);
	BMP_Free( bmp );
}

void stripledBitmapBlit(BMP* bmp, int i0, int ox, int oy, int dx, int dy) {
	int i=i0, ix, iy, w, h;
	w = min((int)BMP_GetWidth(bmp),ox+dx);
	h = min((int)BMP_GetHeight(bmp),oy+dy);
	byte r,g,b;
	for (int y=0; y<stripledInfos[i_stripled].h; y++) {
	  iy = y+oy;
	  for (int x=0; x<stripledInfos[i_stripled].w;x++) {
		ix = x+ox;
		if (ix>=0 && iy>=0 && ix<w && iy<h) {
		   BMP_GetPixelRGB(bmp,ix,iy,&r,&g,&b);
		   if (y%2 == 0) {
			   stripledInfos[i_stripled].leds[i] = CRGB(r, g, b);
		   }
		   else {
			   stripledInfos[i_stripled].leds[stripledInfos[i_stripled].w*(int)(i/stripledInfos[i_stripled].w) + stripledInfos[i_stripled].w - ((i%stripledInfos[i_stripled].w) + 1)] = CRGB(r, g, b);
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
	stripledBitmapBlit(bmp, 0, 0, 0, stripledInfos[i_stripled].w, stripledInfos[i_stripled].h);
	BMP_Free( bmp );
}

void displayTextBitmap(String text, XBMFont font, CRGB bg, CRGB fg, int align, BMP* bmp, int offset) {
	if (bmp == NULL)
		return;
	int i0 = 0;
	int width = min((int)BMP_GetWidth(bmp),stripledInfos[i_stripled].w);
	int height = min((int)BMP_GetHeight(bmp),stripledInfos[i_stripled].h);
	if (align == ALIGN_CENTER)
		i0 = (stripledInfos[i_stripled].w-width)/2;
	else if (align == ALIGN_RIGHT)
		i0 = (stripledInfos[i_stripled].w-width);
	fillBitmap(bmp, 0, 0, (int)BMP_GetWidth(bmp), (int)BMP_GetHeight(bmp), bg);
	drawTextBitmap(bmp, text, font, 0, 0, fg);
	stripledBitmapBlit(bmp, i0, offset, 0, width, height);
}

/* 
 * Main loop
 */


void updateStatus() {
	int deviceIndex;
	for (deviceIndex=0; deviceIndex<N_LED; deviceIndex++)
		updateLEDStatus(deviceIndex);
	for (deviceIndex=0; deviceIndex<N_STRIPLED; deviceIndex++) {
		updateMessageScroll();
		updateAnimation(deviceIndex);
	}
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
