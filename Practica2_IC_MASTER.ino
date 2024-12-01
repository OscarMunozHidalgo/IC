#include <Arduino.h>

constexpr const uint32_t serial_monitor_bauds = 115200;
constexpr const uint32_t serial1_bauds = 9600;
constexpr const uint32_t pseudo_period_ms = 1000;

#define SRF02_DEVICE_1_ADDRESS byte(0xE4)
#define SRF02_DEVICE_2_ADDRESS byte(0xE8)
#define HANDSHAKE 0xAA
#define ACK 0xDD //221
#define MAX_WORDS 4
#define BYE 0x77
#define pseudo_period_ms 1000

#define one_shot 0x12
#define off 0x14
#define unit_inc 0x15
#define unit_cm 0x16
#define unit_ms 0x17
#define status 0x19

uint32_t last_ms = 0;

void setup() {
  Serial.begin(serial_monitor_bauds);
  while (!Serial);

  Serial1.begin(serial1_bauds); // Serial1.write para escribir y .read para leer, siempre dentro de un serial1.available>0
}

void sendHandshake() {
  Serial1.write(HANDSHAKE);
}

void sendBye() {
  Serial1.write(BYE);
}

void command_help() {
  Serial.println("\nLista de comandos:");
  Serial.println("help: Muestra la lista de comandos y su sintaxis");
  Serial.println("us: Muestra la relación de sensores de ultrasonidos disponibles en el dispositivo sensor.");
  Serial.println("us <srf02> one-shot: El sensor con dirección <srf02> se programa para producir un disparo único.");
  Serial.println("us <srf02> on <period_ms>: El sensor con dirección <srf02> se programa para producir periódicamente disparos cada <period_ms> milisegundos.");
  Serial.println("us <srf02> off: El sensor con dirección <srf02> se apaga si este estaba disparando periódicamente.");
  Serial.println("us <srf02> unit {inc | cm | ms}: Modifica la unidad de medida devuelta por el sensor <srf02>.");
  Serial.println("us <srf02> delay <ms>: Establece el retardo mínimo que debe haber entre dos disparos consecutivos del sensor <srf02>.");
  Serial.println("us <srf02> status: Proporciona información de configuración del sensor <srf02>.");
}

void command_us() {
  Serial.println("Proporcionando información sobre sensores de ultrasonidos...");
}

void command_one_shot(String *words) {
  uint32_t last_ms=millis();
  sendHandshake();

  while(millis()-last_ms<pseudo_period_ms) 
  { 
    if(Serial1.available()>0) 
    {
      uint8_t data=Serial1.read();
      if (data != ACK) { Serial.println("Error al enviar el comando"); return; }

      Serial1.write(one_shot); //Comando status
      unsigned long address = strtoul(words[1].c_str(), NULL, 16); //Enviar la dirección
      Serial1.write(address);
      sendBye();
    }
  }
  if(millis()-last_ms<pseudo_period_ms) delay(pseudo_period_ms-(millis()-last_ms));
}

void command_on_period(String *words) {
  Serial.print("Setting period to ");
  Serial.println(words[3]);
}

void command_off(String *words) {
  uint32_t last_ms=millis();
  sendHandshake();
  while(millis()-last_ms<pseudo_period_ms) 
  { 
    if(Serial1.available()>0) 
    {
      uint8_t data=Serial1.read();
      if (data != ACK) { Serial.println("Error al enviar el comando"); return; }

      Serial1.write(off); //Comando off
      unsigned long address = strtoul(words[1].c_str(), NULL, 16); //Enviar la dirección
      Serial1.write(address);
      sendBye();
    }
  }
  if(millis()-last_ms<pseudo_period_ms) delay(pseudo_period_ms-(millis()-last_ms));
}

void command_unit(String *words) {
  Serial.print("Setting unit to ");
  Serial.println(words[3]);
}

void command_delay(String *words) {
  Serial.print("Setting delay to ");
  Serial.println(words[3]);
}

void command_status(String *words) {
  uint32_t last_ms=millis();
  sendHandshake();
  while(millis()-last_ms<pseudo_period_ms) 
  { 
    if(Serial1.available()>0) 
    {
      uint8_t data=Serial1.read();
      if (data != ACK) { Serial.println("Error al enviar el comando"); return; }

      Serial1.write(status); //Comando status
      unsigned long address = strtoul(words[1].c_str(), NULL, 16); //Enviar la dirección
      Serial1.write(address);

      sendBye();
    }
  }
  if(millis()-last_ms<pseudo_period_ms) delay(pseudo_period_ms-(millis()-last_ms));
}

int validateSRF02Dir(String word) {
  if (word.indexOf("0") != 0 || word.indexOf("x") != 1 || word.length() != 4) return -1;
  return 0;
}

void validateCommand(String *words, int wordCount) {
  if (validateSRF02Dir(words[1]) != 0) { Serial.println("Error: Dirección del sensor incorrecta. Es de la forma 0x.."); return; }

  if (words[2] == "one-shot") command_one_shot(words);
  else if (words[2] == "on") command_on_period(words);
  else if (words[2] == "off") command_off(words);

  else if (words[2] == "unit") command_unit(words);
  else if (words[2] == "delay") command_delay(words);
  else if (words[2] == "status") command_status(words);

  else unrecognizedCommand();
}

void splitCommand(String command) {
  String words[MAX_WORDS];
  int wordCount = 0;

  int startIndex = 0;
  int spaceIndex = 0;

  while ((spaceIndex = command.indexOf(' ', startIndex)) != -1 && wordCount < MAX_WORDS) {
    words[wordCount++] = command.substring(startIndex, spaceIndex);
    startIndex = spaceIndex + 1;
  }

  if (startIndex < command.length() && wordCount < MAX_WORDS) {
    words[wordCount++] = command.substring(startIndex);
  }

  validateCommand(words, wordCount);
}

void us_commandFamily(String command) {
  if (command.length() == 2) command_us();
  else splitCommand(command);
}

void unrecognizedCommand() {
  Serial.println("Error: Comando no reconocido. Use 'help' para ver una lista de los comandos y su sintaxis.");
}

void checkFirstWord(String command) {
  command.trim();
  int index = command.indexOf(" ");
  String firstWord = index == -1 ? command : command.substring(0, index);

  if (firstWord == "help") command_help();
  else if (firstWord == "us") us_commandFamily(command);
  else unrecognizedCommand();
}

void readCommand() {
  String inputCommand = Serial.readStringUntil('\n');
  inputCommand.trim();
  if (inputCommand.length() == 0) {
    Serial.println("Error: Comando vacío. Use 'help' para ver una lista de los comandos y su sintaxis.");
    return;
  }

  checkFirstWord(inputCommand);
}

void readData() {
  if (Serial1.available()>0) {
    uint8_t data = Serial1.read();
    Serial.println(data);
  }
}

void loop() {
  if (Serial.available() > 0) {
    readCommand();
  }
}
