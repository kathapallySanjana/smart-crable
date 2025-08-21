include <WiFi.h> //wifi 
#include <ESPAsyncWebServer.h>//webserver
#include <AsyncTCP.h>//to upd asynchronisly
#include <ESP32Servo.h>//servo motor

// WiFi Credentials
const char* ssid = "srivalli";
const char* password = "7075039316";

// Pins
#define SOUND_SENSOR_PIN 34
#define RAIN_SENSOR_PIN 35
#define SERVO_PIN 18
#define APR_MODULE_PIN 19

// Servo Motor
Servo cradleServo;

// Web Server
AsyncWebServer server(80);

AsyncWebSocket ws("/ws");

// States
bool babyCrying = false;
bool bedWet = false;
bool cradleSwinging = false;
bool swingRequested = false;
int k=0;

// Swinging Timer
unsigned long swingStartTime = 0;
const unsigned long swingDuration = 20000; // Swing for 20 seconds

// WebSocket Event
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("WebSocket client connected");
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.println("WebSocket client disconnected");
  } else if (type == WS_EVT_DATA) {
    // Handle incoming WebSocket data if needed
    String message = String((char*)data);
    if (message == "swing") {
      swingRequested = true;
      Serial.println("Swing request received from web interface");
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Allocate timers for ESP32Servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  // Initialize Servo
  cradleServo.setPeriodHertz(50);    // Standard 50Hz servo
  cradleServo.attach(SERVO_PIN, 500, 2400); // Attach servo to pin with min/max PWM values
  cradleServo.write(15); // Neutral position

  // Initialize Web Server
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = R"rawliteral(
      <!DOCTYPE html>
      <html>
      <head>
      <style>
      .heading
      {
        color:black;
        font-family:"Roboto";
        text-align:center;
        font-size:40px;
      }
      </style>
        <h1 class="heading">Baby Monitor</h1>
        <style>
          body { font-family: Arial;font-style:italic; text-align: center; padding: 20px; color:blue; background-color:lightblue;}
          .status { font-size: 30px; margin: 30px 0; }
          .switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 34px;
          }
          .switch input { opacity: 0; width: 0; height: 0; }
          .slider {
            position: absolute;
            cursor: pointer;
            top: 0; left: 0; right: 0; bottom: 0;
            background-color: yellow;
            transition: .4s;
            border-radius: 34px;
          }
          .slider:before {
            position: absolute;
            content: "";
            height: 26px;
            width: 26px;
            border-radius: 50%;
            left: 4px;
            bottom: 4px;
            background-color: pink;
            transition: .4s;
          }
          input:checked + .slider {
            background-color: yellow;
          }
          input:checked + .slider:before {
            transform: translateX(26px);
          }
        </style>
      </head>
      <body>
        <p class="status cryingStatus">Baby Crying: No</p>
        <p class="status bedWetStatus">Bed Wet: No</p>
        <p class="status cradleStatus">Cradle: Stopped</p>

        <!-- Button to control cradle swing -->
        <style> 
        #swingButton {
           background-color:violet;
        }
        </style>
        <button id="swingButton">Press to Swing Cradle</button>

        <script>
        const ws = new WebSocket("ws://" + window.location.hostname + "/ws");

        ws.onopen = function() {
          console.log("Connected to WebSocket");
        };

        ws.onmessage = function(event) {
          const data = JSON.parse(event.data);

          // Update crying status
          if (data.crying !== undefined) {
            document.querySelector('.cryingStatus').innerText = 'Baby Crying: ' + (data.crying ? 'Yes' : 'No');
          }

          // Update bed wet status
          if (data.bedwet !== undefined) {
            document.querySelector('.bedWetStatus').innerText = 'Bed Wet: ' + (data.bedwet ? 'Yes' : 'No');
          }

          // Update cradle status
          if (data.cradle !== undefined) {
            document.querySelector('.cradleStatus').innerText = 'Cradle: ' + data.cradle.charAt(0).toUpperCase() + data.cradle.slice(1);
          }
        };

        // Handle cradle swing button click
        const swingButton = document.getElementById("swingButton");
        swingButton.addEventListener("click", function() {
          ws.send("swing");
        });
        </script>
      </body>
      </html>
    )rawliteral";

    request->send(200, "text/html", html);
  });

  // Initialize WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.begin();

  // Initialize Pins
  pinMode(SOUND_SENSOR_PIN, INPUT);
  pinMode(RAIN_SENSOR_PIN, INPUT);
  pinMode(APR_MODULE_PIN, OUTPUT);

  Serial.println("Setup complete");
}

// Smooth servo swing between two angles
void smoothSwing(int startAngle, int endAngle, int stepDelay) {
  if (startAngle < endAngle) {
    for (int pos = startAngle; pos <= endAngle; pos++) {
      cradleServo.write(pos);
      delay(stepDelay);
    }
  } else {
    for (int pos = startAngle; pos >= endAngle; pos--) {
      cradleServo.write(pos);
      delay(stepDelay);
    }
  }
}

void loop() {
  // Check if baby is crying
  if (digitalRead(SOUND_SENSOR_PIN) == LOW) {
    if (!babyCrying) {
      babyCrying = true;
      cradleSwinging = true;
      Serial.println("Baby is crying! Cradle starts swinging.");
      swingStartTime = millis();
      k=1;

      // Play APR Voice Module
      digitalWrite(APR_MODULE_PIN, HIGH);
      delay(500); // Play for 500ms
      digitalWrite(APR_MODULE_PIN, LOW);

      // Notify WebSocket clients
      String cryingJson = "{\"crying\":true}";
      ws.textAll(cryingJson);
    }
  }

  if (k>0 && millis() - swingStartTime < swingDuration) 
  {
    String cradleJson = "{\"cradle\":\"swinging\"}";
    ws.textAll(cradleJson);
    // Start swinging slowly
    smoothSwing(15, 165, 5); // Swing to 60 degrees
    smoothSwing(165, 15, 5); // Swing to 120 degrees
    // smoothSwing(120, 90, 10); // Return to 90 degrees
    
  }
  else
  {
    String cradleJson = "{\"cradle\":\"stopped\"}";
    ws.textAll(cradleJson);
  }

  // Stop swinging after duration
  if (babyCrying && millis() - swingStartTime > swingDuration) {
    cradleServo.write(90); // Stop swinging
    babyCrying = false;
    cradleSwinging = false;
    Serial.println("Cradle stopped swinging.");

    // Notify WebSocket clients
    String cryingJson = "{\"crying\":false}";
    ws.textAll(cryingJson);
    String cradleJson = "{\"cradle\":\"stopped\"}";
    ws.textAll(cradleJson);
  }

  // Check if bed is wet
  if (digitalRead(RAIN_SENSOR_PIN) == LOW) {
    if (!bedWet) {
      bedWet = true;
      Serial.println("Bed is wet!");

      // Notify WebSocket clients
      String bedWetJson = "{\"bedwet\":true}";
      ws.textAll(bedWetJson);
    }
  } else {
    bedWet = false;
    String bedWetJson = "{\"bedwet\":false}";
      ws.textAll(bedWetJson);
  }

  // Handle swing request
  if (swingRequested) 
  {
    Serial.println("Swing requested from webpage");
    swingRequested = false;
    cradleSwinging = true;
    swingStartTime = millis(); // Reset swing timer
    k=1;
  }

  delay(500); // Reduce CPU load
}