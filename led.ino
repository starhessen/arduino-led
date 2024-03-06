

#include <Arduino.h>

// --- General Settings
const uint16_t 
	Num_Leds   =  80;         // strip length
const uint8_t
	Brightness =  255;        // maximum brightness


#define LED_TYPE     WS2812B  // led strip type for FastLED
#define COLOR_ORDER  GRB      // color order for bitbang
#define PIN_DATA     6        // led data output pin

// --- Serial Settings
const unsigned long
	SerialSpeed    = 115200;  // serial port speed
const uint16_t
	SerialTimeout  = 60;      // time before LEDs are shut off if no data (in seconds), 0 to disable


#define SERIAL_FLUSH          // Serial buffer cleared on LED latch


#include <FastLED.h>

CRGB leds[Num_Leds];
uint8_t * ledsRaw = (uint8_t *)leds;


const uint8_t magic[] = {
	'A','d','a'};
#define MAGICSIZE  sizeof(magic)

// Check values are header byte # - 1, as they are indexed from 0
#define HICHECK    (MAGICSIZE)
#define LOCHECK    (MAGICSIZE + 1)
#define CHECKSUM   (MAGICSIZE + 2)

enum processModes_t {Header, Data} mode = Header;

int16_t c;  // current byte, must support -1 if no data available
uint16_t outPos;  // current byte index in the LED array
uint32_t bytesRemaining;  // count of bytes yet received, set by checksum
unsigned long t, lastByteTime, lastAckTime;  // millisecond timestamps

void headerMode();
void dataMode();
void timeouts();

// Macros initialized
#ifdef SERIAL_FLUSH
	#undef SERIAL_FLUSH
	#define SERIAL_FLUSH while(Serial.available() > 0) { Serial.read(); }
#else
	#define SERIAL_FLUSH
#endif

#ifdef DEBUG_LED
	#define ON  1
	#define OFF 0

	#define D_LED(x) do {digitalWrite(DEBUG_LED, x);} while(0)
#else
	#define D_LED(x)
#endif

#ifdef DEBUG_FPS
	#define D_FPS do {digitalWrite(DEBUG_FPS, HIGH); digitalWrite(DEBUG_FPS, LOW);} while (0)
#else
	#define D_FPS
#endif

void setup(){
	#ifdef DEBUG_LED
		pinMode(DEBUG_LED, OUTPUT);
		digitalWrite(DEBUG_LED, LOW);
	#endif

	#ifdef DEBUG_FPS
		pinMode(DEBUG_FPS, OUTPUT);
	#endif

	#if defined(PIN_CLOCK) && defined(PIN_DATA)
		FastLED.addLeds<LED_TYPE, PIN_DATA, PIN_CLOCK, COLOR_ORDER>(leds, Num_Leds);
	#elif defined(PIN_DATA)
		FastLED.addLeds<LED_TYPE, PIN_DATA, COLOR_ORDER>(leds, Num_Leds);
	#else
		#error "No LED output pins defined. Check your settings at the top."
	#endif
	
	FastLED.setBrightness(Brightness);

	#ifdef CLEAR_ON_START
		FastLED.show();
	#endif

	Serial.begin(SerialSpeed);
	Serial.print("Ada\n"); // Send ACK string to host

	lastByteTime = lastAckTime = millis(); // Set initial counters
}

void loop(){ 
	t = millis(); // Save current time

	// If there is new serial data
	if((c = Serial.read()) >= 0){
		lastByteTime = lastAckTime = t; // Reset timeout counters

		switch(mode) {
			case Header:
				headerMode();
				break;
			case Data:
				dataMode();
				break;
		}
	}
	else {
		// No new data
		timeouts();
	}
}

void headerMode(){
	static uint8_t
		headPos,
		hi, lo, chk;

	if(headPos < MAGICSIZE){
		// Check if magic word matches
		if(c == magic[headPos]) {headPos++;}
		else {headPos = 0;}
	}
	else{
		// Magic word matches! Now verify checksum
		switch(headPos){
			case HICHECK:
				hi = c;
				headPos++;
				break;
			case LOCHECK:
				lo = c;
				headPos++;
				break;
			case CHECKSUM:
				chk = c;
				if(chk == (hi ^ lo ^ 0x55)) {
					// Checksum looks valid. Get 16-bit LED count, add 1
					// (# LEDs is always > 0) and multiply by 3 for R,G,B.
					D_LED(ON);
					bytesRemaining = 3L * (256L * (long)hi + (long)lo + 1L);
					outPos = 0;
					memset(leds, 0, Num_Leds * sizeof(struct CRGB));
					mode = Data; // Proceed to latch wait mode
				}
				headPos = 0; // Reset header position regardless of checksum result
				break;
		}
	}
}

void dataMode(){
	// If LED data is not full
	if (outPos < sizeof(leds)){
		ledsRaw[outPos++] = c; // Issue next byte
	}
	bytesRemaining--;
 
	if(bytesRemaining == 0) {
		// End of data -- issue latch:
		mode = Header; // Begin next header search
		FastLED.show();
		D_FPS;
		D_LED(OFF);
		SERIAL_FLUSH;
	}
}

void timeouts(){
	// No data received. If this persists, send an ACK packet
	// to host once every second to alert it to our presence.
	if((t - lastAckTime) >= 1000) {
		Serial.print("Ada\n"); // Send ACK string to host
		lastAckTime = t; // Reset counter

		// If no data received for an extended time, turn off all LEDs.
		if(SerialTimeout != 0 && (t - lastByteTime) >= (uint32_t) SerialTimeout * 1000) {
			memset(leds, 0, Num_Leds * sizeof(struct CRGB)); //filling Led array by zeroes
			FastLED.show();
			mode = Header;
			lastByteTime = t; // Reset counter
		}
	}
}
