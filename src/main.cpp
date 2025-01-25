#include <ArduinoJson.h>  // Inclua a biblioteca ArduinoJson
#include <NTPClient.h>
#include <PubSubClient.h>
#include <Update.h>
#include <WiFi.h>

#include "cJSON.h"
#include "mainConf.h"
#include "myFS.h"

WiFiClient espClient;
PubSubClient _mqtt(espClient);
WiFiUDP _ntpUDP;
NTPClient _timeClient(_ntpUDP, "pool.ntp.org", -10800);
#define SerialDebug Serial

// Task handles
TaskHandle_t sensorTaskHandle;
TaskHandle_t serialTaskHandle;

// Configurações do ThingsBoard
const char *ssid;
const char *password;
const char *mqtt_server = "demo.thingsboard.io";

void mqttTask(void *parameter);
void readSensorTask(void *parameter);
void readLevel(unsigned long &t, float &distOld, bool &b);
std::string payloadBuilder(float lvl, String timeStamp, bool debug = false);
void reconnectMQTT();
bool connectToWifi();
void manageMQTT();
void manageWiFi();
void getWifiData(bool serial, int index);
float waterLevelRead = 0, waterLevelOld = 0;
unsigned long t[5];
bool newValue;
float inMin = 20.0, inMax = 110.0;

void setup() {
    Serial.begin(115200);
    // Criação das tasks
    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }
    xTaskCreatePinnedToCore(readSensorTask,     // Task para ler o sensor
                            "Read Sensor",      // Nome da task
                            10000,              // Stack size
                            NULL,               // Parâmetro
                            1,                  // Prioridade
                            &sensorTaskHandle,  // Handle da task
                            1);                 // Núcleo 1 (CPU 1)

    xTaskCreatePinnedToCore(mqttTask,           // Task para imprimir no serial
                            "Print Serial",     // Nome da task
                            10000,              // Stack size
                            NULL,               // Parâmetro
                            1,                  // Prioridade
                            &serialTaskHandle,  // Handle da task
                            0);                 // Núcleo 0 (CPU 0)

    SerialDebug.println("Finish Setup");
}

void loop() {}
// TSK readSensorTask
void readSensorTask(void *parameter) {
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    while (1) {
        unsigned long readSensortick = millis();
        readLevel(t[0], waterLevelRead, newValue);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    // Delay for stability
}

// TSK mqttTask
void mqttTask(void *parameter) {
    connectToWifi();
    _timeClient.begin();
    _timeClient.update();
    manageMQTT();
    float teste[5];
    teste[0] = 0.0;
    while (true) {
        manageWiFi();
        if (!_mqtt.connected()) {
            manageMQTT();
        } else {
            _mqtt.loop();
            _timeClient.update();
            if (millis() - t[1] > retornaSegundo(30) || newValue) {
                if (_mqtt.publish("v1/devices/me/telemetry", payloadBuilder(waterLevelRead, _timeClient.getFormattedTime(), true).c_str())) {
                    newValue = false;
                    t[1] = millis();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));  // Pequeno delay para não ocupar 100% da CPU
    }
}

void manageWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        connectToWifi();
        digitalWrite(WiFi_LED, LOW);  // Acende LED WiFi indicando desconexão
    } else {
        digitalWrite(WiFi_LED, HIGH);  // Acende LED WiFi indicando conexão
    }
}

void readLevel(unsigned long &t, float &distOld, bool &b) {
    unsigned long dur;
    float distNew;
    if (millis() - t > 2000) {
        // Clears the trigPin condition
        digitalWrite(trigPin, LOW);
        delayMicroseconds(5);
        // Sets the trigPin HIGH (ACTIVE) for 1500 microseconds
        digitalWrite(trigPin, HIGH);
        delayMicroseconds(10);
        digitalWrite(trigPin, LOW);
        // Reads the echoPin, returns the sound wave travel time in microseconds
        dur = pulseIn(echoPin, HIGH);
        // Calculating the distance
        distNew = dur * 0.034 / 2;  // Speed of sound wave divided by 2 (go and back)
        if (distNew != distOld) {
            if (abs(abs(distNew) - abs(distOld)) > 2.0) {
                distOld = distNew;
                b = true;
                Serial.printf("Valor lido em cm: %.2f\n", distOld);
            }
        }
        t = millis();
    }
}

std::string payloadBuilder(float lvl, String timeStamp, bool debug) {
    cJSON *json = cJSON_CreateObject();
    float nivel = map(lvl, inMin, inMax, 100.0, 0.0);
    cJSON_AddNumberToObject(json, "valor_bruto", lvl);
    cJSON_AddNumberToObject(json, "nivel", nivel);
    cJSON_AddStringToObject(json, "timeStamp", timeStamp.c_str());
    char *jsonString = cJSON_PrintUnformatted(json);
    if (debug) {
        Serial.printf("[DEBUG] JSON Payload: %s\n", jsonString);
    }
    std::string payload = jsonString;
    cJSON_free(jsonString);
    cJSON_Delete(json);
    return payload;
}

bool checkDiff(float &f0, float f1) {
    if (f0 != f1) {
        if (f0 - f1 > 10) {
            f0 = f1;
            return true;
        } else {
            return false;
        }
    }
    return false;
}

void manageMQTT() {
    _mqtt.setServer(mqtt_server, 1883);
    reconnectMQTT();
}
void reconnectMQTT() {
    int contadorMQTT = 0;
    while (!_mqtt.connected() && contadorMQTT < 15) {
        Serial.print("Tentando conectar ao MQTT...");
        if (_mqtt.connect("ESP32Client", BombaTeste, NULL)) {
            Serial.println("Conectado");
            _mqtt.subscribe("v1/devices/me/rpc/request/+");
            contadorMQTT = 0;
        } else {
            Serial.print("falhou, rc=");
            Serial.print(_mqtt.state());
            Serial.println(" tente novamente em 5 segundos");
            delay(5000);
            contadorMQTT++;
        }
    }
}
bool connectToWifi() {
    int maxAttemptsPerNetwork = MAX_ATTEMPTS;
    bool notConnected = true;
    String jsonString = readFile(LittleFS, RedeData, false);
    delay(2000);
    Serial.println("jsonString:");
    Serial.println(jsonString);
    cJSON *json = cJSON_Parse(jsonString.c_str());
    if (json == NULL) {
        Serial.println("Erro ao analisar JSON.");
    }
    // Verifica se o campo 'networks' existe e é um array
    cJSON *networks = cJSON_GetObjectItemCaseSensitive(json, "networks");
    // if (!cJSON_IsArray(networks)) {
    //     Serial.println("Campo 'networks' não encontrado ou não é um array.");
    //     cJSON_Delete(json);
    // }
    // Obtém o tamanho do array
    int networkCount = cJSON_GetArraySize(networks);
    Serial.print("networkCount: ");
    Serial.println(networkCount);
    WiFi.mode(WIFI_MODE_STA);
    std::string ssid_str, pwd_str;
    // Itera sobre as redes para garantir que os dados estejam corretos
    for (int i = 0; i < networkCount; i++) {
        int contador = 0;
        cJSON *network = cJSON_GetArrayItem(networks, i);
        if (cJSON_IsObject(network)) {
            cJSON *ssid = cJSON_GetObjectItemCaseSensitive(network, "SSID");
            cJSON *pwd = cJSON_GetObjectItemCaseSensitive(network, "PWD");

            if (cJSON_IsString(ssid) && cJSON_IsString(pwd)) {
                ssid_str = ssid->valuestring;  // Atribui o valor do JSON
                pwd_str = pwd->valuestring;    // Atribui o valor do JSON

                Serial.print("SSID: ");
                Serial.println(ssid_str.c_str());
                Serial.print("PWD: ");
                Serial.println(pwd_str.c_str());

                WiFi.begin(ssid_str.c_str(), pwd_str.c_str());
                unsigned long startTime = millis();
                while (WiFi.status() != WL_CONNECTED && contador <= maxAttemptsPerNetwork) {
                    Serial.print(".");
                    contador++;
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println();
                    Serial.println("WiFi connected");
                    Serial.print("IP address: ");
                    Serial.println(WiFi.localIP());
                    notConnected = false;
                    cJSON_Delete(json);
                    return true;  // Conectado com sucesso
                } else {
                    Serial.println();
                    Serial.println("Não foi possível conectar à WiFi, tentando próxima rede.");
                    notConnected = true;
                }
            }
        }
    }
    cJSON_Delete(json);
    return false;  // Isso nunca será executado devido ao ESP.restart()
}

void getWifiData(bool serial, int index) {
    // Parse o JSON
    cJSON *file = cJSON_Parse(readFile(LittleFS, RedeData, false).c_str());
    if (file == NULL) {
        Serial.println("Erro ao parsear JSON!");
        return;
    }

    // Obtenha o vetor de redes
    cJSON *networks = cJSON_GetObjectItemCaseSensitive(file, "networks");
    if (!cJSON_IsArray(networks)) {
        Serial.println("O JSON não contém um vetor 'networks' válido!");
        cJSON_Delete(file);
        return;
    }

    // Obtenha o objeto de rede no índice fornecido
    cJSON *network = cJSON_GetArrayItem(networks, index);
    if (network == NULL) {
        Serial.println("Índice de rede fora do intervalo!");
        cJSON_Delete(file);
        return;
    }

    // Obtenha os campos SSID e PWD
    cJSON *SSID = cJSON_GetObjectItemCaseSensitive(network, "SSID");
    cJSON *PWD = cJSON_GetObjectItemCaseSensitive(network, "PWD");

    if (cJSON_IsString(SSID) && (SSID->valuestring != NULL)) {
        ssid = SSID->valuestring;
    } else {
        Serial.println("SSID inválido!");
    }

    if (cJSON_IsString(PWD) && (PWD->valuestring != NULL)) {
        password = PWD->valuestring;
    } else {
        Serial.println("PWD inválido!");
    }

    // Debugging output
    if (serial) {
        Serial.print("SSID:");
        Serial.print(ssid);
        Serial.print("|| PWD:");
        Serial.println(password);
    }

    // Libere a memória
    cJSON_Delete(file);
}
