/*
 * Copyright (c) 2020, Luc Yriarte
 * License: BSD <http://www.opensource.org/licenses/bsd-license.php>
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DHT.h>

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
#include "space_invader_01.h"
#include "space_invader_02.h"
#include "space_invader_03.h"
#include "space_invader_04.h"
#include "space_invader_05.h"
#include "space_invader_06.h"
#include "space_invader_07.h"

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

#define CONNECT_MSG_DELAY_MS 20000
#define MAIN_LOOP_POLL_MS 10
#define MINIMUM_UPDATE_MS 2

const CRGB RGB_BLACK = CRGB(0,0,0);
const CRGB RGB_FRONT = CRGB(3,2,1);
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
	WRAP_COLUMNS,
	ORIGIN_TOP_LEFT
},
{
	STRIPLED_W_0*STRIPLED_H_0,
	0,
	STRIPLED_H_0,
	STRIPLED_W_0,
	STRIPLED_H_0,
	WRAP_COLUMNS,
	ORIGIN_TOP_LEFT
}
};

#define N_PANELS_0 (sizeof(panels_0) / sizeof(StripLEDPanel))



#define MSG_SCROLL_START_MS 3000
#define MSG_SCROLL_MS 50
#define MSG_SCROLL_CHARS 2

#define ANIM_ROTATE_MS 50
#define ANIM_CHARCODES_MS 500
#define ANIM_SPRITES_MS 300

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
#ifdef STRIPLED_SCREEN
bool displayingIpAddress = false;
#endif

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
 * TEMPERATURE
 */
 
typedef struct {
	DHT * dhtP;
	int dht_gpio;
	int dht_type;
} TEMPERATUREInfo;

TEMPERATUREInfo temperatureInfos[] = {
	{
		NULL,
		12,	// D6
		DHT22
	}
};

#define N_TEMPERATURE (sizeof(temperatureInfos) / sizeof(TEMPERATUREInfo))

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
	XBMFont* fontP;
	int align;
	int offset;
} MESSAGEInfo;

MESSAGEInfo messageInfos[] = {
	{
		0,
		{0, MSG_SCROLL_MS},
		"",
		NULL,
		ALIGN_CENTER,
		0
	}
};
int i_message = 0;
#define N_MESSAGE (sizeof(messageInfos) / sizeof(MESSAGEInfo))

/*
 * ANIM
 */
 
 enum {
	ANIM_NONE,
	ANIM_ROTATE,
	ANIM_CHARCODES,
	ANIM_SPRITES
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

/*
 * CHARCODES
 */
 
typedef struct {
	ANIMInfo * animInfoP;
	int charNext;
	int charEnd;
} CHARCODESInfo;

CHARCODESInfo charcodesInfos[] = {
	{
		&animInfos[0],
		0,
		255
	},
	{
		&animInfos[1],
		0,
		255
	}
};

/*
 * SPRITES
 */
 
typedef struct {
	int w;
	int h;
	unsigned char * charBytes;
} XBMInfo;
XBMInfo XBMSprites[] = {
	{space_invader_01_width, space_invader_01_height, (unsigned char *)space_invader_01_bits},
	{space_invader_02_width, space_invader_02_height, (unsigned char *)space_invader_02_bits},
	{space_invader_03_width, space_invader_03_height, (unsigned char *)space_invader_03_bits},
	{space_invader_04_width, space_invader_04_height, (unsigned char *)space_invader_04_bits},
	{space_invader_05_width, space_invader_05_height, (unsigned char *)space_invader_05_bits},
	{space_invader_06_width, space_invader_06_height, (unsigned char *)space_invader_06_bits},
	{space_invader_07_width, space_invader_07_height, (unsigned char *)space_invader_07_bits}
};

typedef struct {
	XBMInfo * XBMInfoP;
	CRGB crgb;
} SPRITEState;

SPRITEState alien1_states[] = {
	{&XBMSprites[0], CRGB(4,2,0)}, // space_invader_01, yellow (alien 1)
	{&XBMSprites[1], CRGB(4,2,0)}  // space_invader_02, yellow
};

SPRITEState alien2_states[] = {
	{&XBMSprites[2], CRGB(2,0,4)}, // space_invader_03, lavender (alien 2)
	{&XBMSprites[3], CRGB(2,0,4)}  // space_invader_04, lavender
};

SPRITEState alien3_states[] = {
	{&XBMSprites[4], CRGB(12,2,0)}, // space_invader_05, orange (alien 3)
	{&XBMSprites[5], CRGB(12,2,0)}  // space_invader_06, orange
};

SPRITEState mothership_states[] = {
	{&XBMSprites[6], CRGB(2,4,0)}, // space_invader_07, lime (flying saucer)
	{&XBMSprites[6], CRGB(0,2,4)}  // space_invader_07, cyan
};

typedef struct {
	SPRITEState * spriteStates;
	int nStates;
	int nStep;
	int currStep;
	int x0; int y0;
	int x; int y;
	int dx;	int dy;
} SPRITE_ANIM_Phase;
SPRITE_ANIM_Phase alien1phases[] = {
	{
		alien1_states, 
		2, // nStates
		48, 0, // 48 steps
		48,-1, 0,0, -1,0 // scroll left 48 to 0
	},
	{
		alien1_states, 
		2, // nStates
		16, 0, // 16 steps
		0,-1, 0,0, 0,0 // static at 0,-1
	},
	{
		alien1_states, 
		2, // nStates
		8, 0, // 8 steps
		0,-1, 0,0, 0,1 // down to 0,7
	},
	{
		alien1_states, 
		2, // nStates
		-1, 0, // infinite steps
		0,7, 0,0, 0,0 // static at 0,7
	}
};

SPRITE_ANIM_Phase alien2phases[] = {
	{
		alien2_states, 
		2, // nStates
		48, 0, // 48 steps
		64,-1, 0,0, -1,0 // scroll left 64 to 16
	},
	{
		alien2_states, 
		2, // nStates
		16, 0, // 16 steps
		16,-1, 0,0, 0,0 // static at 16,-1
	},
	{
		alien2_states, 
		2, // nStates
		8, 0, // 8 steps
		16,-1, 0,0, 0,1 // down to 16,7
	},
	{
		alien2_states, 
		2, // nStates
		-1, 0, // infinite
		16,7, 0,0, 0,0 // static at 16,7
	}
};

SPRITE_ANIM_Phase alien3_1_phases[] = {
	{
		alien3_states, 2, 
		14, 0, // 14 steps
		-16,-1, 0,0, 1,0 // right -16 to -2
	},
	{
		alien3_states, 2, 
		8, 0, // 8 steps
		-2,-1, 0,0, 0,1 // down to -2, 7
	},
	{
		alien3_states, 2,
		32, 0, // 32 steps
		-2, 7, 0,0, 0,0 // static at -2, 7
	},
	{
		alien3_states, 2, 
		8, 0, // 8 steps
		-2,7, 0,0, 0,1 // disappear down
	}
};

SPRITE_ANIM_Phase alien3_2_phases[] = {
	{
		alien3_states, 2, 
		22, 0, // 22 steps
		8,-15, 0,0, 0,1 // down to 8, 7
	},
	{
		alien3_states, 2,
		32, 0, // 32 steps
		8, 7, 0,0, 0,0 // static at 8, 7
	},
	{
		alien3_states, 2, 
		8, 0, // 8 steps
		8,7, 0,0, 0,1 // disappear down
	}
};

SPRITE_ANIM_Phase alien3_3_phases[] = {
	{
		alien3_states, 2, 
		14, 0, // 14 steps
		32,-1, 0,0, -1,0 // left 32 to 18
	},
	{
		alien3_states, 2, 
		8, 0, // 8 steps
		18,-1, 0,0, 0,1 // down to 18, 7
	},
	{
		alien3_states, 2,
		32, 0, // 32 steps
		18, 7, 0,0, 0,0 // static at 18, 7
	},
	{
		alien3_states, 2, 
		8, 0, // 8 steps
		18,7, 0,0, 0,1 // disappear down
	}
};

SPRITE_ANIM_Phase mothership_phases[] = {
	{
		mothership_states, 2, 
		64, 0, // 64 steps
		8,-9, 0,0, 0,0 // hide at 8,-9
	},
	{
		mothership_states, 2, 
		8, 0, // 8 steps
		8,-9, 0,0, 0,1 // down to 8, -1
	},
	{
		mothership_states, 2,
		-1, 0, // infinite
		8, -1, 0,0, 0,0 // static at 8, -1
	}
};

typedef struct {
	SPRITE_ANIM_Phase * animPhases;
	int nPhases;
	int phase;
} SPRITE_ANIM_PHASE_List;

SPRITE_ANIM_PHASE_List invaderAnimations[] = {
  {
    alien1phases,
    4, 0, // 4 phases
  },
  {
    alien2phases,
    4, 0, // 4 phases
  },
  {
    alien3_1_phases,
    4, 0, // 4 phases
  },
  {
    alien3_2_phases,
    3, 0, // 3 phases
  },
  {
    alien3_3_phases,
    4, 0, // 4 phases
  },
  {
    mothership_phases,
    3, 0, // 3 phases
  }
};

typedef struct {
	ANIMInfo * animInfoP;
	SPRITE_ANIM_PHASE_List * spriteAnimations;
	int nSprites;
} SPRITESInfo;

SPRITESInfo spritesInfos[] = {
  {
    &animInfos[0],
    invaderAnimations,
    6	// 6 sprites
  }
};


/* **** **** **** **** **** ****
 * Functions
 * **** **** **** **** **** ****/

void setup() {
	int i,j;
	for (i=0; i < N_LED; i++)
		pinMode(ledInfos[i].gpio, OUTPUT);
	for (i=0; i < N_TEMPERATURE; i++) {
		temperatureInfos[i].dhtP = new DHT(temperatureInfos[i].dht_gpio,temperatureInfos[i].dht_type);
		temperatureInfos[i].dhtP->begin();
	}
	FastLED.addLeds<NEOPIXEL,STRIPLED_GPIO_0>(stripledInfos[0].stripP->getLeds(), STRIPLED_W_0*STRIPLED_H_0*N_PANELS_0);
	Serial.begin(BPS_HOST);
	wifiMacInit();
#ifdef STRIPLED_SCREEN
	stripledInfos[0].stripP->setup(fontPtrs[0]);
	stripledInfos[0].stripP->setText(wifiMacStr);
	setMessageDefaults();
#endif
	Serial.print("WiFi.macAddress: ");
	Serial.println(wifiMacStr);
}

void setMessageDefaults() {
	messageInfos[i_message].fontP = fontPtrs[11];
	stripledInfos[i_message].stripP->setFont(messageInfos[i_message].fontP);
	stripledInfos[i_message].stripP->setAlignment(ALIGN_CENTER);
	stripledInfos[i_message].stripP->setBgColor(RGB_BLACK);
	stripledInfos[i_message].stripP->setFgColor(RGB_FRONT);
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
	if (strReq.endsWith("ON")) {
		ledInfos[index].blink = 0;
		ledInfos[index].state = HIGH;
	}
	else if (strReq.endsWith("OFF")) {
		ledInfos[index].blink = 0;
		ledInfos[index].state = LOW;
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
	messageInfos[index].offset = messageInfos[index].offset + 1;
	if (messageInfos[index].offset >= stripledInfos[index].stripP->getTextWidth())
		messageInfos[index].offset = -messageInfos[index].fontP->getWidth()*MSG_SCROLL_CHARS;
	stripledInfos[index].stripP->displayText(messageInfos[index].offset);
}

void updateMessageText(int index, String text) {
	messageInfos[index].text = text;
	messageInfos[index].offset = 0;
	stripledInfos[index].stripP->setText(text);
	if (stripledInfos[index].stripP->getTextWidth() > stripledInfos[index].stripP->getWidth())
		messageInfos[index].offset = -messageInfos[index].fontP->getWidth()*MSG_SCROLL_CHARS;
	stripledInfos[index].stripP->displayText(messageInfos[index].offset);
}

void updateMessageAlign(int index, int align) {
	stripledInfos[index].stripP->setAlignment(align);
	messageInfos[index].offset = 0;
	stripledInfos[index].stripP->displayText(messageInfos[index].offset);
}

void updateMessageLine(int index, int line) {
	stripledInfos[index].stripP->setLine(line);
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

void updateCharcodes(int index) {
	int w = stripledInfos[index].stripP->getWidth();
	int h = stripledInfos[index].stripP->getHeight();
	CRGB bg = RGB_BLACK;
	CRGB fg = RGB_FRONT;
	stripledInfos[index].stripP->fillBitmap(0, 0, w, h, bg);
	stripledInfos[index].stripP->setFont(&fixedMedium_4x6);
	stripledInfos[index].stripP->setText(String(charcodesInfos[index].charNext));
	stripledInfos[index].stripP->renderText(0,0,fg);
	stripledInfos[index].stripP->setFont(messageInfos[index].fontP);
	stripledInfos[index].stripP->setText(String((char) charcodesInfos[index].charNext));
	stripledInfos[index].stripP->renderText(
		w - (messageInfos[index].fontP->getWidth() + 1), 
		(h - messageInfos[index].fontP->getHeight()) / 2, 
		fg);
	stripledInfos[index].stripP->displayBitmap();
	stripledInfos[index].stripP->setAlignment(messageInfos[index].align);
	if (charcodesInfos[index].charNext >= charcodesInfos[index].charEnd)
		charcodesInfos[index].animInfoP->kind = ANIM_NONE;
	charcodesInfos[index].charNext++;
}

void resetSprites(int index) {
	for (int iSprite = 0; iSprite < spritesInfos[index].nSprites; iSprite++) {
		spritesInfos[index].spriteAnimations[iSprite].phase = 0;
		for (int iPhase = 0; iPhase < spritesInfos[index].spriteAnimations[iSprite].nPhases; iPhase++) {
			SPRITE_ANIM_Phase * animPhaseP = &spritesInfos[index].spriteAnimations[iSprite].animPhases[iPhase];
			animPhaseP->currStep = 0;
			animPhaseP->x = animPhaseP->x0;
			animPhaseP->y = animPhaseP->y0;
		}
	}
}

void updateSprites(int index) {
	int w = stripledInfos[index].stripP->getWidth();
	int h = stripledInfos[index].stripP->getHeight();
	CRGB bg = RGB_BLACK;
	stripledInfos[index].stripP->fillBitmap(0, 0, w, h, bg);
	spritesInfos[index].animInfoP->kind = ANIM_NONE;
	for (int iSprite = 0; iSprite < spritesInfos[index].nSprites; iSprite++) {
		if (spritesInfos[index].spriteAnimations[iSprite].phase >= 0) {
			spritesInfos[index].animInfoP->kind = ANIM_SPRITES;
			SPRITE_ANIM_Phase * animPhaseP = &spritesInfos[index].spriteAnimations[iSprite].animPhases[spritesInfos[index].spriteAnimations[iSprite].phase];
			SPRITEState * spriteStateP = &animPhaseP->spriteStates[animPhaseP->currStep % animPhaseP->nStates];
			stripledInfos[index].stripP->renderXpm(animPhaseP->x, animPhaseP->y, 
				spriteStateP->XBMInfoP->w, spriteStateP->XBMInfoP->h,
				spriteStateP->XBMInfoP->charBytes,
				spriteStateP->crgb);
			animPhaseP->x += animPhaseP->dx;
			animPhaseP->y += animPhaseP->dy;
			animPhaseP->currStep++;
			if (animPhaseP->nStep > 0 && animPhaseP->currStep >= animPhaseP->nStep) {
				spritesInfos[index].spriteAnimations[iSprite].phase++;
				if (spritesInfos[index].spriteAnimations[iSprite].phase >= spritesInfos[index].spriteAnimations[iSprite].nPhases)
					spritesInfos[index].spriteAnimations[iSprite].phase = -1;
			}
		}
	}
	stripledInfos[index].stripP->displayBitmap();
}

void updateAnimation(int index) {
	if (animInfos[index].kind == ANIM_NONE || !updatePollInfo(&(animInfos[index].pollInfo)))
		return;
	switch (animInfos[index].kind) {
		case ANIM_ROTATE:
			rotateStripledDisplay(animInfos[index].strip_index);
			break;
		case ANIM_CHARCODES:
			updateCharcodes(animInfos[index].strip_index);
			break;
		case ANIM_SPRITES:
			updateSprites(animInfos[index].strip_index);
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
	messageInfos[i_message].fontP = fontPtrs[index];
	stripledInfos[i_message].stripP->setFont(messageInfos[i_message].fontP);
	updateMessageText(i_message, messageInfos[i_message].text);
	return true;
}

bool handleCHARCODESRequest(const char * req) {
	String strReq = req;
	charcodesInfos[i_message].charNext = strReq.toInt();
	strReq = strReq.substring(strReq.indexOf("/")+1);
	charcodesInfos[i_message].charEnd = strReq.toInt();
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

bool handleFILLRequest(const char * req) {
	String strReq = req;
	unsigned int x0 = strReq.toInt();
	if (x0 < 0 || x0 >= stripledInfos[i_message].stripP->getWidth())
		return false;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	unsigned int y0 = strReq.toInt();
	if (y0 < 0 || y0 >= stripledInfos[i_message].stripP->getHeight())
		return false;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	unsigned int dx = strReq.toInt();
	if (dx < 1 || dx > stripledInfos[i_message].stripP->getWidth()-x0)
		return false;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	unsigned int dy = strReq.toInt();
	if (dy < 1 || dy > stripledInfos[i_message].stripP->getHeight()-y0)
		return false;
	strReq = strReq.substring(strReq.indexOf("/")+1);
	int rgb = (int) strtol(strReq.substring(0,6).c_str(), NULL, 16);
	int r = rgb >> 16, g = rgb >> 8 & 0xFF, b = rgb & 0xFF;
	stripledInfos[i_message].stripP->fillBitmap(x0, y0, dx, dy, rgb);
	stripledInfos[i_message].stripP->displayBitmap();
	return true;
}

bool handleANIMRequest(const char * req) {
	String strReq = req;
	animInfos[i_stripled].kind = ANIM_NONE;
	if (strReq == "ROTATE") {
		animInfos[i_stripled].kind = ANIM_ROTATE;
		animInfos[i_stripled].pollInfo.poll_ms = ANIM_ROTATE_MS;
	}
	else if (strReq == "CHARCODES") {
		animInfos[i_stripled].kind = ANIM_CHARCODES;
		animInfos[i_stripled].pollInfo.poll_ms = ANIM_CHARCODES_MS;
	}
	else if (strReq == "SPRITES") {
		updateMessageText(i_message, "");
		animInfos[i_stripled].kind = ANIM_SPRITES;
		animInfos[i_stripled].pollInfo.poll_ms = ANIM_SPRITES_MS;
		resetSprites(i_stripled);
	}
	return true;
}

bool handleMSGRequest(const char * req) {
	String strReq = req;
	String strMsg = decodeUrl(strReq.substring(strReq.indexOf("/")+1));
	updateMessageText(i_message, strMsg);
#ifdef STRIPLED_SCREEN
	displayingIpAddress = false;
#endif
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

bool handleLINERequest(const char * req) {
	String strReq = req;
	updateMessageLine(i_message, strReq.toInt());
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
	stripledInfos[i_message].stripP->setBgColor(RGB_BLACK);
	stripledInfos[i_message].stripP->setFgColor(CRGB(1,0,2));
	updateMessageText(i_message, networks[i_network].SSID);
	return true;
}

bool handleIPRequest() {
	if (i_network < 0 || i_network >= N_NETWORKS)
		return false;
	messageInfos[i_message].align = ALIGN_LEFT;
	stripledInfos[i_message].stripP->setBgColor(RGB_BLACK);
	stripledInfos[i_message].stripP->setFgColor(CRGB(0,1,2));
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

void replyJsonStatus(String jsonStatus) {
	wifiClient.println("HTTP/1.1 200 OK");
	wifiClient.println("Content-Type: application/json");
	wifiClient.println("Access-Control-Allow-Origin: *");
	wifiClient.println("Connection: close");
	wifiClient.println();
	wifiClient.print(jsonStatus);
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
	else if (strReq.startsWith("CHARCODES/"))
		result = handleCHARCODESRequest(strReq.substring(10).c_str());
	else if (strReq.startsWith("STRIPLED/"))
		result = handleSTRIPLEDRequest(strReq.substring(9).c_str());
	else if (strReq.startsWith("GRADIENT/"))
		result = handleGRADIENTRequest(strReq.substring(9).c_str());
	else if (strReq.startsWith("FILL/"))
		result = handleFILLRequest(strReq.substring(5).c_str());
	else if (strReq.startsWith("ANIM/"))
		result = handleANIMRequest(strReq.substring(5).c_str());
	else if (strReq.startsWith("MSG/"))
		result = handleMSGRequest(strReq.substring(4).c_str());
	else if (strReq.startsWith("SCROLL/"))
		result = handleSCROLLRequest(strReq.substring(7).c_str());
	else if (strReq.startsWith("ALIGN/"))
		result = handleALIGNRequest(strReq.substring(6).c_str());
	else if (strReq.startsWith("LINE/"))
		result = handleLINERequest(strReq.substring(5).c_str());
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
	if (strReq.startsWith("STATUS")) {
		replyJsonStatus(getJsonStatus());
		return true;
	}
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


String getJsonStatus() {;
	String jsonStatus = "{";
	int deviceIndex;
	jsonStatus += "  \"LED\":[";
	for (deviceIndex=0; deviceIndex<N_LED; deviceIndex++) {
		if (deviceIndex) jsonStatus += ",";
		jsonStatus += ledInfos[deviceIndex].state == HIGH ? "1" : "0";
	}
	jsonStatus += "]";
	jsonStatus += ",  \"MSG\":[";
	for (deviceIndex=0; deviceIndex<N_MESSAGE; deviceIndex++) {
		if (deviceIndex) jsonStatus += ",";
		jsonStatus += String("\"") + messageInfos[deviceIndex].text + String("\"");
	}
	jsonStatus += "]";
        jsonStatus += ",  \"TEMPERATURE\":[";
        for (deviceIndex=0; deviceIndex<N_TEMPERATURE; deviceIndex++) {
                float temp = temperatureInfos[deviceIndex].dhtP->readTemperature();
                if (isnan(temp))
                        continue;
                if (deviceIndex) jsonStatus += ",";
                jsonStatus += String(temp);
        }
        jsonStatus += "]";
	jsonStatus += "}";
	return jsonStatus;
}
void updateStatus() {
	int deviceIndex;
	for (deviceIndex=0; deviceIndex<N_LED; deviceIndex++)
		updateLEDStatus(deviceIndex);
	for (deviceIndex=0; deviceIndex<N_STRIPLED; deviceIndex++) {
		updateAnimation(deviceIndex);
	}
#ifdef STRIPLED_SCREEN
	updateMessageScroll(i_message);
#endif
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
	int start_loop_ms, connect_ms;
	delayWithUpdateStatus(1000);
	while (!wifiConnect(WIFI_CONNECT_RETRY))
		delayWithUpdateStatus(WIFI_CONNECT_RETRY_DELAY_MS);
	connect_ms = millis();
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
#ifdef STRIPLED_SCREEN
		if (displayingIpAddress && millis() - connect_ms > CONNECT_MSG_DELAY_MS) {
			updateMessageText(i_message, "");
			displayingIpAddress = false;
		}
#endif
		wifiStatus = WiFi.status();
	}
  Serial.println("WiFi disconnected");
}
