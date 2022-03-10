#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <Hash.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <unordered_map>

ESP8266WebServer server(80);
IRsend ir_send(D5);
struct BatchAction {
	int sequence;
	const char* action;
	int delay;
};
struct IrCode {
	int code;
	int length;
};
std::unordered_map<std::string, IrCode> route_to_code({
	{"/power", {0x540C, 15}},
	{"/bluetooth", {0x8E114, 20}},
	{"/phono", {0x5C0C, 15}},
	{"/volume_up", {0x240C, 15}},
	{"/volume_down", {0x640C, 15}}
});

std::function<void()> make_route_handler(int code, int length) {
	return [code, length]() {
		ir_send.sendSony(code, length);
		server.send(200, "application/json", "{\"message\": \"success\"}");
	};
}

void turn_on_turntable() {
	WiFiClient client;
	HTTPClient http;
	std::string token_header = "Bearer ";
	token_header.append(HOME_ASSISTANT_TOKEN);
	http.begin(client, "homeassistant.local", 8123, "/api/services/switch/toggle");
	yield();
	http.addHeader("Content-Type", "application/json");
	http.addHeader("Authorization",  token_header.c_str());
	int responseCode = http.POST("{\"entity_id\": \"switch.stereo\"}");
	yield();
	Serial.println(responseCode);
	Serial.println(http.getString());
	http.end();
	yield();
	server.send(200, "application/json", "{\"action\": \"turntable\"}");
}

void execute_batch() {
	if (server.hasArg("plain")== false){
		server.send(400, "application/json", "{\"messge\": \"missing request body\"}");
	}
	else {
		server.send(200, "application/json", "{\"message\": \"success\"}");
		std::vector<BatchAction> normalized_actions;
		DynamicJsonDocument body(1024);
		deserializeJson(body, server.arg("plain"));
		JsonArray actions = body["actions"].as<JsonArray>();
		for (JsonVariant action : actions) {
			normalized_actions.push_back({
				action["sequence"], 
				action["action"], 
				action.containsKey("delay") ? action["delay"] : 100
			});
		}
		std::sort(normalized_actions.begin(), normalized_actions.end(), [](const BatchAction &a, const BatchAction &b) {
			return a.sequence < b.sequence;
		});
		for (BatchAction action : normalized_actions) {
			std::string path = "/";
			path.append(action.action);
			if (strcmp(action.action, "turntable") == 0) {
				turn_on_turntable();
			}
			else if (route_to_code.find(path) != route_to_code.end()) {
				const IrCode code = route_to_code[path];
				make_route_handler(code.code, code.length)();
			}
			delay(action.delay);
		}
		body.clear();
		body.garbageCollect();
	}

}

void handle_404() {
	server.send(404, "application/json", "{\"message\": \"not found\"}");
}

void setup() {
	yield();
	Serial.begin(115200);
	Serial.println("START");
  	pinMode(LED_BUILTIN, OUTPUT);
	ir_send.begin();
	Serial.print("Configuring access point...");
	digitalWrite(LED_BUILTIN, LOW);
	const IPAddress ip(192, 168, 0, 213);
	const IPAddress gateway(192,168,0,1);
	const IPAddress subnet(255,255,255,0);
	WiFi.mode(WIFI_STA);
	WiFi.config(ip, gateway, subnet);
	WiFi.begin(WIFI_SSID, WIFI_PASSWD);
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}
	Serial.println("WiFi connected!");
	digitalWrite(LED_BUILTIN, HIGH);

	server.on("/batch", execute_batch);
	server.on("/turntable", turn_on_turntable);
	server.onNotFound(handle_404);
	for (std::pair<std::string, IrCode> key_pair : route_to_code) {
		const std::string uri = key_pair.first;
		const IrCode code = key_pair.second;
		server.on(uri.c_str(), make_route_handler(code.code, code.length));
	}
	server.begin();
	yield();
}

void loop() {
	yield();
	server.handleClient();
	yield();
}