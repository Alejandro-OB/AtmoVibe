#include <WiFi.h>
#include "SPIFFS.h"
#include <ESPAsyncWebServer.h>       
#include <ArduinoJson.h>
#include <SimpleTimer.h>
#include "ThingSpeak.h"
#include <FS.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HTTPClient.h>  // Incluimos la librería para hacer solicitudes HTTP

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
float t = 0;

//define custom fields
char limite_malo[100];
char limite_bueno[100];
char api_key[100];
char numero_canal[100];
char offset_calibracion[100];
char tiempo_envio[100];

AsyncWebServer server(80);
SimpleTimer timer;
WiFiClient client;

bool shouldSaveConfig = false;
bool isAuthenticated = false; // Bandera para saber si el usuario está autenticado

// Definir la URL de la API de autenticación PHP
const char* api_login_url = "http://192.168.18.99/atmovibe/login.php";

void saveConfig() {
  Serial.println("Saving config...");
  DynamicJsonDocument jsonDocument(1024);
  JsonObject json = jsonDocument.to<JsonObject>();
  json["limite_malo"] = limite_malo;
  json["limite_bueno"] = limite_bueno;
  json["api_key"] = api_key;
  json["numero_canal"] = numero_canal;
  json["offset_calibracion"] = offset_calibracion;
  json["tiempo_envio"] = tiempo_envio;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  serializeJson(json, configFile);
  configFile.close();
  Serial.println("Config saved.");
}

void loadConfig() {
  if (SPIFFS.begin()) {
    Serial.println("Mounted file system");
    if (SPIFFS.exists("/config.json")) {
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("Opened config file");
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument jsonDocument(1024);
        DeserializationError error = deserializeJson(jsonDocument, buf.get());
        
        if (!error) {
          JsonObject json = jsonDocument.as<JsonObject>();
          strcpy(limite_malo, json["limite_malo"]);
          strcpy(limite_bueno, json["limite_bueno"]);
          strcpy(api_key, json["api_key"]);
          strcpy(numero_canal, json["numero_canal"]);
          strcpy(offset_calibracion, json["offset_calibracion"]);
          strcpy(tiempo_envio, json["tiempo_envio"]);
        } else {
          Serial.println("Failed to load json config");
        }
      }
    }
  } else {
    Serial.println("Failed to mount FS");
  }
}

// Función para autenticar al usuario usando la API PHP
bool authenticateUser(const char* username, const char* password) {
  HTTPClient http;
  http.begin(api_login_url);
  http.addHeader("Content-Type", "application/json");

  // Crear el cuerpo JSON para la solicitud
  String requestBody = "{\"username\": \"" + String(username) + "\", \"password\": \"" + String(password) + "\"}";
  
  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println(httpResponseCode);
    Serial.println(response);

    DynamicJsonDocument jsonDoc(200);
    DeserializationError error = deserializeJson(jsonDoc, response);
    
    if (!error) {
      if (jsonDoc["status"] == "success") {
        return true;  // Autenticación exitosa
      } else {
        Serial.println("Error: " + String((const char*)jsonDoc["message"]));
      }
    } else {
      Serial.println("Error parsing JSON response.");
    }
  } else {
    Serial.print("Error on sending POST: ");
    Serial.println(httpResponseCode);
  }

  http.end();
  return false;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  
  loadConfig();

  WiFi.begin("APTO4A", "angel1912leg3526");  // Cambia esto por tus credenciales
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected!");

  // Página de login
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><head><meta charset='UTF-8'><style>";
    html += "body { font-family: Arial; background-color: #f2f2f2; }";
    html += "form { max-width: 300px; margin: auto; padding: 10px; background: #ffffff; border-radius: 5px; }";
    html += "input[type=text], input[type=password] { width: 100%; padding: 12px; margin: 5px 0 10px; border: 1px solid #ccc; border-radius: 3px; }";
    html += "input[type=submit] { background-color: #4CAF50; color: white; padding: 12px; border: none; border-radius: 3px; cursor: pointer; }";
    html += "</style></head><body>";
    html += "<h2 style='text-align: center;'>Iniciar Sesión</h2>";
    html += "<form action='/login' method='POST'>";
    html += "Usuario: <input type='text' name='username'><br>";
    html += "Contraseña: <input type='password' name='password'><br>";
    html += "<input type='submit' value='Ingresar'>";
    html += "</form></body></html>";
    request->send(200, "text/html", html);
  });

  // Manejo del login
  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("username", true) && request->hasParam("password", true)) {
      String username = request->getParam("username", true)->value();
      String password = request->getParam("password", true)->value();
      if (authenticateUser(username.c_str(), password.c_str())) {
        isAuthenticated = true;
        request->redirect("/config");
      } else {
        request->send(401, "text/html", "<html><body><h1>Usuario o Contraseña Incorrectos!</h1><a href='/login'>Volver</a></body></html>");
      }
    }
  });

 // Página de configuración (solo si está autenticado)
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAuthenticated) {
      request->redirect("/login");
      return;
    }
    String html = "<html><head><meta charset='UTF-8'><style>";
    html += "body { font-family: Arial; background-color: #f2f2f2; }";
    html += "form { max-width: 500px; margin: auto; padding: 20px; background: #ffffff; border-radius: 5px; }";
    html += "input[type=text] { width: 100%; padding: 12px; margin: 5px 0 10px; border: 1px solid #ccc; border-radius: 3px; }";
    html += "input[type=submit] { background-color: #4CAF50; color: white; padding: 15px; border: none; border-radius: 3px; cursor: pointer; width: 100%; }";
    html += "div.navbar { background-color: #00C853; overflow: hidden; margin-bottom: 20px; padding: 10px 0; }";
    html += "div.navbar a { float: left; display: block; color: #f2f2f2; text-align: center; padding: 14px 16px; text-decoration: none; }";
    html += "div.navbar a:hover { background-color: #ddd; color: black; }";
    html += "</style></head><body>";
    html += "<div class='navbar'>";
    html += "<a href='/config'>Configuración</a>";
    html += "<a href='/datos'>Datos</a>";
    html += "</div>";
    html += "<h2 style='text-align: center;'>Configuración del Sistema</h2>";
    html += "<form action='/save' method='POST'>";
    html += "Limite Malo: <input type='text' name='limite_malo' value='" + String(limite_malo) + "'><br>";
    html += "Limite Bueno: <input type='text' name='limite_bueno' value='" + String(limite_bueno) + "'><br>";
    html += "API Key: <input type='text' name='api_key' value='" + String(api_key) + "'><br>";
    html += "Channel ID: <input type='text' name='numero_canal' value='" + String(numero_canal) + "'><br>";
    html += "Offset Calibracion: <input type='text' name='offset_calibracion' value='" + String(offset_calibracion) + "'><br>";
    html += "Tiempo de Envio (segundos): <input type='text' name='tiempo_envio' value='" + String(tiempo_envio) + "'><br>";
    html += "<input type='submit' value='Guardar'>";
    html += "</form>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  // Página de guardar configuración
server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!isAuthenticated) {
        request->redirect("/login");
        return;
    }
    
    // Guardar parámetros enviados desde el formulario
    if (request->hasParam("limite_malo", true)) {
        strcpy(limite_malo, request->getParam("limite_malo", true)->value().c_str());
    }
    if (request->hasParam("limite_bueno", true)) {
        strcpy(limite_bueno, request->getParam("limite_bueno", true)->value().c_str());
    }
    if (request->hasParam("api_key", true)) {
        strcpy(api_key, request->getParam("api_key", true)->value().c_str());
    }
    if (request->hasParam("numero_canal", true)) {
        strcpy(numero_canal, request->getParam("numero_canal", true)->value().c_str());
    }
    if (request->hasParam("offset_calibracion", true)) {
        strcpy(offset_calibracion, request->getParam("offset_calibracion", true)->value().c_str());
    }
    if (request->hasParam("tiempo_envio", true)) {
        strcpy(tiempo_envio, request->getParam("tiempo_envio", true)->value().c_str());
    }

    shouldSaveConfig = true;
    if (shouldSaveConfig) {
        saveConfig();
        shouldSaveConfig = false;
    }

    // Página de confirmación mejorada con estilos consistentes
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f2f2f2; text-align: center; margin: 0; padding: 0; }";
    
    // Navbar estilos mejorados para coincidir con la otra página
    html += ".navbar { overflow: hidden; background-color: #00C853; padding: 10px 0; margin-bottom: 20px; }";
    html += ".navbar a { float: left; display: block; color: #f2f2f2; text-align: center; padding: 14px 20px; text-decoration: none; font-size: 17px; }";
    html += ".navbar a:hover { background-color: #ddd; color: black; }";
    
    // Estilos del contenido principal
    html += ".container { padding-top: 50px; text-align: center; }";
    html += "h1 { color: #4CAF50; font-size: 2.5em; margin-bottom: 20px; }";
    html += "a { font-size: 1.2em; color: #4CAF50; text-decoration: none; border-bottom: 1px solid #4CAF50; }";
    html += "a:hover { color: #388E3C; border-bottom: 1px solid #388E3C; }";
    html += "</style></head><body>";
    
    // Navbar
    html += "<div class='navbar'>";
    html += "<a href='/config'>Configuración</a>";
    html += "<a href='/datos'>Datos</a>";
    html += "</div>";

    // Contenido principal
    html += "<div class='container'>";
    html += "<h1>¡Configuración Guardada!</h1>";
    html += "<a href='/config'>Volver</a>";
    html += "</div>";
    
    html += "</body></html>";
    
    request->send(200, "text/html", html);
});


  // Página de mostrar datos
  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><head><meta charset='UTF-8'><style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f2f2f2; text-align: center; margin: 0; padding: 0; }";
    html += ".navbar { overflow: hidden; background-color: #00C853; padding: 10px 0; margin-bottom: 20px; }";
    html += ".navbar a { float: left; display: block; color: #f2f2f2; text-align: center; padding: 14px 20px; text-decoration: none; font-size: 17px; }";
    html += ".navbar a:hover { background-color: #ddd; color: black; }";
    html += ".iframe-container { display: flex; justify-content: center; align-items: center; padding: 20px 0; }";
    html += "iframe { max-width: 100%; width: 80%; height: 500px; border: none; }";
    html += ".container { padding-top: 20px; text-align: center; max-width: 100%; margin: 0 auto; }";
    html += "h1 { color: #4CAF50; font-size: 2.5em; margin-bottom: 20px; }";
    html += "</style></head><body>";
    html += "<div class='navbar'>";
    html += "<a href='/config'>Configuración</a>";
    html += "<a href='/datos'>Datos</a>";
    html += "</div>";
    html += "<div class='container'>";
    html += "<h1>Gráfica de Datos de CO2</h1>";
    html += "<div class='iframe-container'><iframe src='https://thingspeak.mathworks.com/channels/2760798/charts/1?bgcolor=%23ffffff&color=%23d62020&dynamic=true&results=60&type=line&update=15'></iframe></div>";
    html += "</div>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  server.begin();
  Serial.println("Server started");
  
  ThingSpeak.begin(client);
}

void loop() {
  timer.run();

  int tiempo_envio_segundos = atoi(tiempo_envio);
  int limite_malo_convertido = atoi(limite_malo);
  int limite_bueno_convertido = atoi(limite_bueno);
  int offset_calibracion_convertido = atoi(offset_calibracion);
  int offstet_mq_135 = 400;

  unsigned long numero_canal_convertido = strtoul(numero_canal, NULL, 10);
  
  int medicion = analogRead(34);  // Read sensor value and stores in a variable medicion
  int t = medicion + offstet_mq_135 + offset_calibracion_convertido;
  int icono;
  Serial.print("Medicion raw = ");
  Serial.println(medicion);
  Serial.print("Offset = ");
  Serial.println(offset_calibracion_convertido);
  Serial.print("Co2 ppm = ");
  Serial.println(t);
  String analisis;

  if (t <= limite_bueno_convertido) {
    Serial.println("Aire puro");
    analisis = "BUENO ";
    icono = 0x02;
  } else if (t >= limite_bueno_convertido && t <= limite_malo_convertido) {
    Serial.println("Aire cargandose");
    analisis = "REGULAR ";
    icono = 0x01;
  } else if (t >= limite_malo_convertido) {
    Serial.println("Aire viciado");
    analisis = "MALO ";
    icono = 0x13;
  }
  
  // Enviar datos a ThingSpeak
  ThingSpeak.setField(1, t); //co2, ppm
  int x = ThingSpeak.writeFields(numero_canal_convertido, api_key);

  if (x == 200) {
    Serial.println("Channel update successful.");
  } else {
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }

  // Mostrar datos en pantalla OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Cantidad de CO2");

  // Mostrar barras de señal WiFi (RSSI)
  long rssi = WiFi.RSSI();
  int bars;
  if (rssi >= -55) {
    bars = 5;
  } else if (rssi <= -55 && rssi >= -65) {
    bars = 4;
  } else if (rssi <= -65 && rssi >= -70) {
    bars = 3;
  } else if (rssi <= -70 && rssi >= -78) {
    bars = 2;
  } else if (rssi <= -78 && rssi >= -82) {
    bars = 1;
  } else {
    bars = 0;
  }

  for (int b = 0; b <= bars; b++) {
    display.fillRect(95 + (b * 5), 10 - (b * 2), 3, b * 2, WHITE);
  }

  display.setTextSize(4);
  display.setCursor(0, 17);
  display.print(t);

  display.setTextSize(1);
  display.setCursor(100, 17);
  display.println(F("PPM"));

  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  display.setCursor(0, 55);
  display.print(" AIRE ");
  display.println(analisis);

  display.setTextSize(4);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(100, 32);
  display.write(icono);

  display.display();

  delay(tiempo_envio_segundos * 1000);
}
