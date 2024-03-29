#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ArduinoJson.h>

// PAS DE ONDERSTAANDE CONFIGURATIE AAN.
// =========================================

// WiFi parameters to be configured
const char* ssid						= "network";		// Check capitals!
const char* password					= "******";	// Check capitals!

// API URL Heroku
// FormatYour http://**your_heruko_container_name**.herokuapp.com/api/events/today
// For example: http://iot-alarm.herokuapp.com/api/events/today
const char* apiUrl						= "http://**your_heruko_container_name**.herokuapp.com/api/events/today";

// Calender URL out of mijnrooster.hu.nl
const char* iCalUrl						= "*****";

// Your personal travel time.
const unsigned int travelTimeMin		= 60;


// PAS HIERONDER NIETS AAN.
// =================================

#define DEBUG
const char speakerPin					= D2;
const char ledPin						= 4;
const int timeZone						= 0;     // Central European Time
const unsigned int localPort			= 8888;  // local port to listen for UDP packets
const unsigned int alarmRefireInterval  = 30000; // 30 seconds.
const char* ntpServerName				= "nl.pool.ntp.org"; // NTP-server
const constexpr time_t travelTimeSec	= travelTimeMin * 60;

// Declarations...
WiFiUDP Udp;
HTTPClient http;

bool alarmFiring = false;
bool alarmState = false;

unsigned int alarmTiming = 0;
unsigned int updateTiming = 0;

unsigned int alarmFireStart = 0;



// Function declarations...
time_t getNtpTime();
void digitalClockDisplay();
void printDigits(int digits);
void sendNTPpacket(IPAddress &address);

// JSON stuff
const size_t capacity = JSON_ARRAY_SIZE(61) + 86 * JSON_OBJECT_SIZE(4);

void setup()
{
	pinMode(speakerPin, OUTPUT);
	
	Serial.begin(112500);
	Serial.println("Connecting to WiFi...");
	WiFi.begin(ssid, password);
	
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}
	
	Serial.print("IP: ");
	Serial.println(WiFi.localIP());
	
	Udp.begin(localPort);
	setSyncProvider(getNtpTime);
	setSyncInterval(300);
}

void loop()
{
	if (WiFi.status() != WL_CONNECTED) {
		return;
	}
	
	if (alarmFiring && (millis() - alarmTiming) > 750) {
		if ((millis() - alarmFireStart) >= alarmRefireInterval) {
			digitalWrite(speakerPin, LOW);
		} else {
			alarmTiming = millis();
			
			alarmState = !alarmState;
			tone(speakerPin, 1000, 500);
			
			Serial.println(alarmState);
		}
	}
	
	if ((millis() - updateTiming) > 25000) {
		updateTiming = millis();
		Serial.print("Timestamp: ");
		Serial.println(now());
		
		http.begin(apiUrl);
		http.addHeader("calendarurl", iCalUrl);
		
		unsigned int httpCode = http.GET();
		
		if (httpCode > 0) {
			Serial.print("HTTP code: ");
			Serial.println(httpCode);
			
			DynamicJsonDocument doc(capacity);
			
			// Parse JSON object
			DeserializationError error = deserializeJson(doc, http.getString());
			if (error) {
				Serial.print(F("deserializeJson() failed: "));
				Serial.println(error.c_str());
				return;
			}
			
			
			JsonArray lessons = doc.as<JsonArray>();
			
			size_t closestIndex = 0;
			signed int closestTimestamp = -1;
			
			time_t nowTimestamp = now();
			
			for (unsigned int i = 0; i < lessons.size(); i++) {
				JsonObject lesson = lessons[i];
				
				if (!lesson.isNull()) {
					signed int diff = static_cast<unsigned int>(lesson["date"]) - nowTimestamp;
					#ifdef DEBUG:
					
					Serial.println(static_cast<const char* >(lesson["title"]));
					Serial.println(diff);
					
					#endif
					
					if (closestTimestamp == -1 || (diff >= 0 && closestTimestamp >= abs(diff))) {
						closestIndex = i;
						closestTimestamp = abs(diff);
					}
				}
			}
			
			if (closestTimestamp > 0) {
				JsonObject lesson = lessons[closestIndex];
				
				Serial.println(static_cast<const char* >(lesson["title"]));
				Serial.print("--> Time to go: ");
				Serial.print(static_cast<int>(round(closestTimestamp / 3600)), DEC);
				Serial.print(" hour(s) = ");
				Serial.print(static_cast<int>(closestTimestamp));
				Serial.println(" second (s)");
				
				if (travelTimeSec >= closestTimestamp) {
					if (!alarmFiring) {
						Serial.println("ALARM TRIGGERED!");
						alarmFiring = true;
						alarmFireStart = millis();
					}
					
					if ((millis() - alarmFireStart) >= alarmRefireInterval) {
						alarmFiring = false;
					}
				} else {
					alarmFiring = false;
					alarmFireStart = 0;
				}
			}
			
			
		}
		
		http.end();
		
		
	}
	
	
	
	

}

void printDigits(int digits)
{
	// utility for digital clock display: prints preceding colon and leading 0
	Serial.print(":");
	if (digits < 10)
	Serial.print('0');
	Serial.print(digits);
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
	IPAddress ntpServerIP; // NTP server's ip address

	while (Udp.parsePacket() > 0) ; // discard any previously received packets
	Serial.println("Transmit NTP Request");
	// get a random server from the pool
	WiFi.hostByName(ntpServerName, ntpServerIP);
	Serial.print(ntpServerName);
	Serial.print(": ");
	Serial.println(ntpServerIP);
	sendNTPpacket(ntpServerIP);
	uint32_t beginWait = millis();
	while (millis() - beginWait < 1500) {
		int size = Udp.parsePacket();
		if (size >= NTP_PACKET_SIZE) {
			Serial.println("Receive NTP Response");
			Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
			unsigned long secsSince1900;
			// convert four bytes starting at location 40 to a long integer
			secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
			secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
			secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
			secsSince1900 |= (unsigned long)packetBuffer[43];
			return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
		}
	}
	Serial.println("No NTP Response :-(");
	return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
	// set all bytes in the buffer to 0
	memset(packetBuffer, 0, NTP_PACKET_SIZE);
	// Initialize values needed to form NTP request
	// (see URL above for details on the packets)
	packetBuffer[0] = 0b11100011;   // LI, Version, Mode
	packetBuffer[1] = 0;     // Stratum, or type of clock
	packetBuffer[2] = 6;     // Polling Interval
	packetBuffer[3] = 0xEC;  // Peer Clock Precision
	// 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;
	// all NTP fields have been given values, now
	// you can send a packet requesting a timestamp:
	Udp.beginPacket(address, 123); //NTP requests are to port 123
	Udp.write(packetBuffer, NTP_PACKET_SIZE);
	Udp.endPacket();
}
