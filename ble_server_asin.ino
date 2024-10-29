/* 
Fuente del ble: https://randomnerdtutorials.com/esp32-bluetooth-low-energy-ble-arduino-ide/
*/
#include <BLEDevice.h>         // Biblioteca para el manejo de dispositivos BLE
#include <BLEServer.h>         // Biblioteca para manejar servidores BLE
#include <BLEUtils.h>          // Utilidades adicionales para BLE
#include <BLE2902.h>           // Biblioteca para manejo de descriptores BLE (permite notificaciones)
#include <WiFi.h>              // Biblioteca para conectar a redes Wi-Fi
#include <Preferences.h>       // Biblioteca para almacenamiento persistente (flash)
#include <Ticker.h>            // Biblioteca para manejo de temporizadores no bloqueantes

// Definición de pines y constantes
#define BUTTON_PIN  14         // Pin del botón para activar el modo configuración
#define LED_PIN  2             // Pin del LED azul
#define LONG_PRESS_TIME 5000   // Tiempo de presión larga del botón para activar modo configuración (5 segundos)

// UUIDs para identificar los servicios y características BLE
#define SERVICE_UUID           "12345678-1234-5678-1234-56789abcdef0"
#define SSID_CHAR_UUID         "87654321-4321-6789-4321-fedcba987654"  // UUID para la característica SSID
#define PASSWORD_CHAR_UUID     "fedcba98-7654-4321-5678-123456789abc"  // UUID para la característica Contraseña
#define CHATID_CHAR_UUID       "abcdef12-1234-5678-4321-fedcba987654"  // UUID para la característica Chat ID
#define LOCATION_CHAR_UUID     "12345678-8765-4321-8765-1234567890ab"  // UUID para la característica Ubicación
#define STATUS_CHAR_UUID       "abcdef12-3456-7890-abcd-ef1234567890"  // UUID para la característica de Estado

// Variables globales para control de tiempos y estados
unsigned long configStartTime = 0;          // Tiempo de inicio del modo configuración
const unsigned long configTimeout = 300000; // Tiempo de espera para el modo configuración (5 minutos)
//const unsigned long configTimeout = 120000; // Tiempo de espera para el modo configuración (2 minutos)
unsigned long lastPrintTime = 0;            // Control de tiempo para impresión de estados
unsigned long resetTimer = 0;               // Control de tiempo para reinicio
bool resetScheduled = false;                // Bandera para programar el reinicio

// Punteros para los objetos BLE
BLEServer *pServer = nullptr;               // Puntero al servidor BLE
BLECharacteristic *pSSIDCharacteristic = nullptr;      // Característica para SSID
BLECharacteristic *pPasswordCharacteristic = nullptr;  // Característica para la contraseña
BLECharacteristic *pChatIDCharacteristic = nullptr;    // Característica para Chat ID
BLECharacteristic *pStatusCharacteristic = nullptr;    // Característica para el estado de la conexión
BLECharacteristic *pLocationCharacteristic = nullptr;  // Característica para la ubicación
BLEAdvertising *pAdvertising = nullptr;                // Puntero para la publicidad BLE

// Variables de estado
bool configMode = false;                    // Modo de configuración activo o no
unsigned long buttonPressStart = 0;         // Tiempo de inicio de la presión del botón

// Variables para almacenar credenciales y otros datos
String ssid = "";                           // Almacena el SSID de la red Wi-Fi
String password = "";                       // Almacena la contraseña de la red Wi-Fi
String chatid = "";                         // Almacena el Chat ID
String location = "";                       // Almacena la ubicación

// Banderas para verificar si las características fueron recibidas
bool locationReceived = false;
bool ssidReceived = false;
bool passwordReceived = false;
bool chatidReceived = false;

Preferences preferences;                    // Objeto para almacenamiento persistente de credenciales

Ticker ledTicker;                           // Ticker para manejar el parpadeo del LED

// Función para conectar a la red Wi-Fi de manera asincrónica
void connectToWiFi() {
  WiFi.begin(ssid.c_str(), password.c_str());  // Inicia la conexión a Wi-Fi con los credenciales recibidos
  Serial.println("Intentando conectar al Wi-Fi...");
  
  unsigned long startAttemptTime = millis();  // Tiempo de inicio para el intento de conexión
  unsigned long timeout = 20000;              // 20 segundos de timeout

  // Bucle no bloqueante para intentar la conexión Wi-Fi
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
    delay(1);  // Pequeña espera para ceder el control al sistema operativo
  }

  Serial.println(".");

  if (WiFi.status() == WL_CONNECTED) {  // Si la conexión fue exitosa
    Serial.println("Conectado a Wi-Fi.");
    pStatusCharacteristic->setValue("Wi-Fi Connected");  // Notifica que la conexión fue exitosa
    pStatusCharacteristic->notify();

    if (pAdvertising) pAdvertising->stop();  // Detiene la publicidad BLE
    configMode = false;                      // Desactiva el modo configuración
    ledTicker.detach();                      // Detiene el parpadeo del LED
    digitalWrite(LED_PIN, LOW);              // Apaga el LED

    // Programar reinicio tras la conexión exitosa usando millis
    Serial.println("Reiniciando el ESP32 en 5 segundos para aplicar las configuraciones...");
    resetTimer = millis();                   // Guarda el tiempo actual
    resetScheduled = true;                   // Establece la bandera para programar el reinicio

  } else {  // Si no se pudo conectar al Wi-Fi
    Serial.println("Error al conectar a Wi-Fi.");
    pStatusCharacteristic->setValue("Wi-Fi Connection Failed");  // Notifica que la conexión falló
    pStatusCharacteristic->notify();
  }
}

// Clase de devolución de llamada para manejar las escrituras de características
class CharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {  // Método que se llama cuando se escribe en una característica
    String value = pCharacteristic->getValue();  // Obtiene el valor escrito

    if (pCharacteristic == pSSIDCharacteristic) {  // Si la característica es el SSID
      ssid = value;                                // Almacena el SSID recibido
      ssidReceived = true;                         // Marca que el SSID fue recibido
      Serial.print("SSID recibido: ");
      Serial.println(ssid);
    } else if (pCharacteristic == pPasswordCharacteristic) {  // Si la característica es la contraseña
      password = value;                                       
      passwordReceived = true;                                
      Serial.print("Contraseña recibida: ");
      Serial.println(password);
    } else if (pCharacteristic == pChatIDCharacteristic) {   // Si la característica es el Chat ID
      chatid = value;
      chatidReceived = true;
      Serial.print("ChatID recibido: ");
      Serial.println(chatid);
    } else if (pCharacteristic == pLocationCharacteristic) { // Si la característica es la ubicación
      location = value;
      locationReceived = true;
      Serial.print("Ubicación recibida: ");
      Serial.println(location);
    }

    // Verifica si todas las características han sido recibidas
    if (ssidReceived && passwordReceived && chatidReceived && locationReceived) {
      preferences.begin("wifi-creds", false);     // Inicia la sección de preferencias para almacenamiento
      preferences.putString("ssid", ssid);        // Guarda el SSID
      preferences.putString("password", password);// Guarda la contraseña
      preferences.putString("chatid", chatid);    // Guarda el Chat ID
      preferences.putString("location", location);// Guarda la ubicación
      preferences.end();                          // Finaliza el almacenamiento en preferencias
      connectToWiFi();                            // Intenta conectar al Wi-Fi con los nuevos credenciales

      // Restablece las banderas de recepción
      ssidReceived = false;
      passwordReceived = false;
      chatidReceived = false;
      locationReceived = false;
    }
  }
};

// Función para iniciar el modo de configuración BLE
void startConfigMode() {
  
  BLEDevice::init("ESP32_GATT_Server");  // Inicializa el dispositivo BLE con el nombre "ESP32_GATT_Server"

  pServer = BLEDevice::createServer();   // Crea un servidor BLE
  BLEService *pService = pServer->createService(SERVICE_UUID);  // Crea un servicio con el UUID definido

  // Configura las características BLE con sus UUIDs y propiedades
  pSSIDCharacteristic = pService->createCharacteristic(
    SSID_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pSSIDCharacteristic->setValue(ssid);   // Establece un valor inicial para SSID
  pSSIDCharacteristic->setCallbacks(new CharacteristicCallbacks());  // Establece las devoluciones de llamada

  pPasswordCharacteristic = pService->createCharacteristic(
    PASSWORD_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pPasswordCharacteristic->setValue(password);  // Establece un valor inicial para la contraseña
  pPasswordCharacteristic->setCallbacks(new CharacteristicCallbacks());  // Establece las devoluciones de llamada

  pChatIDCharacteristic = pService->createCharacteristic(
    CHATID_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pChatIDCharacteristic->setValue(chatid);  // Establece un valor inicial para el Chat ID
  pChatIDCharacteristic->setCallbacks(new CharacteristicCallbacks());  // Establece las devoluciones de llamada

  pLocationCharacteristic = pService->createCharacteristic(
    LOCATION_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pLocationCharacteristic->setValue(location);  // Establece un valor inicial para la ubicación
  pLocationCharacteristic->setCallbacks(new CharacteristicCallbacks());  // Establece las devoluciones de llamada

  pStatusCharacteristic = pService->createCharacteristic(
    STATUS_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pStatusCharacteristic->addDescriptor(new BLE2902());  // Agrega un descriptor para permitir notificaciones

  // Inicia el servicio y la publicidad BLE
  pService->start();
  pAdvertising = pServer->getAdvertising();
  pAdvertising->start();

  configStartTime = millis();  // Guarda el tiempo actual al inicio del modo configuración
  configMode = true;           // Establece el modo configuración activo
  ledTicker.attach(0.5, []() { // Inicia un ticker para parpadear el LED cada 0.5 segundos
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));  // Cambia el estado del LED
  });

  Serial.println("Modo de configuración BLE iniciado.");
}

void initconfig(){
  pinMode(BUTTON_PIN, INPUT_PULLUP);     // Configura el pin del botón como entrada con pull-up interno
  pinMode(LED_PIN, OUTPUT);              // Configura el pin del LED como salida
  digitalWrite(LED_PIN, LOW);            // Apaga el LED inicialmente
  preferences.begin("wifi-creds", false); // Inicia las preferencias para leer las credenciales almacenadas
  ssid = preferences.getString("ssid", "");  // Recupera el SSID almacenado o vacío si no existe
  password = preferences.getString("password", ""); // Recupera la contraseña almacenada o vacío si no existe
  chatid = preferences.getString("chatid", ""); // Recupera el Chat ID almacenado o vacío si no existe
  location = preferences.getString("location", ""); // Recupera la ubicación almacenada o vacío si no existe
  preferences.end();  // Finaliza el uso de las preferencias
}

// Configuración inicial del dispositivo
void setup() {
  Serial.begin(115200);
  Serial.println("Alarma encendida");               // Inicia la comunicación serial a 115200 baudios
  initconfig();
}
// Bucle principal del programa
void loop() {
  // Verifica si se está presionando el botón
  if (digitalRead(BUTTON_PIN) == LOW) {  // Si el botón está presionado (LOW porque está en modo pull-up)
    if (buttonPressStart == 0) {         // Si es la primera vez que se detecta la presión
      buttonPressStart = millis();       // Registra el tiempo de inicio
    } else if (millis() - buttonPressStart >= LONG_PRESS_TIME) { // Si se ha presionado por suficiente tiempo
      Serial.println("Botón presionado por largo tiempo. Iniciando modo configuración.");
      startConfigMode();                 // Inicia el modo configuración BLE
      buttonPressStart = 0;              // Reinicia el contador de presión del botón
    }
  } else {  // Si el botón no está presionado
    buttonPressStart = 0;                // Reinicia el contador de presión del botón
  }

  // Verifica si el modo configuración BLE está activo
  if (configMode) {
    if (millis() - configStartTime >= configTimeout) {  // Si ha pasado el tiempo de espera del modo configuración
      Serial.println("Tiempo de configuración agotado.");
      // Detener el servidor BLE y la publicidad
      BLEDevice::deinit();
      configMode = false;
      ledTicker.detach(); // Apaga el parpadeo del LED
      digitalWrite(LED_PIN, LOW); // Apaga el LED
    }
  }

  // Programa el reinicio después de una conexión exitosa al Wi-Fi
  if (resetScheduled && millis() - resetTimer >= 5000) { // Si el reinicio está programado y han pasado 5 segundos
    Serial.println("Reiniciando el ESP32 para aplicar configuraciones...");
    ESP.restart();  // Reinicia el ESP32
  }
}
