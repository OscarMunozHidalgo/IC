#include <time.h>
#include <RTCZero.h>
#include <Arduino_MKRMEM.h>
#include <ArduinoLowPower.h>

// rtc object
RTCZero rtc;

volatile uint32_t _period_sec = 0;
volatile uint16_t _rtcFlag = 0;
volatile uint16_t is_rtc_interrupt = 0;

Arduino_W25Q16DV flash(SPI1, FLASH_CS);
char filename[] = "datos.txt";
const int externalPin = 5;
volatile int alarmIterations = 0;

// Macro para medir el tiempo transcurrido en milisegundos
#define elapsedMilliseconds(since_ms) (uint32_t)(millis() - since_ms)

void setup() 
{
  pinMode(LORA_RESET, OUTPUT);
  digitalWrite(LORA_RESET, LOW); 

  // Registramos el tiempo de comienzo
  uint32_t t_start_ms = millis();

  SerialUSB.begin(9600);
  while(!SerialUSB) {;}

  flash.begin();

  // Montamos el sistema de archivos
  int res = filesystem.mount();
  if(res != SPIFFS_OK && res != SPIFFS_ERR_NOT_A_FS) {
    SerialUSB.println("mount() failed with error code "); 
    SerialUSB.println(res); 
    exit(EXIT_FAILURE);
  }

  // Creamos un nuevo fichero
  File file = filesystem.open(filename,  CREATE | TRUNCATE);
  if (!file) {
    SerialUSB.print("Creation of file ");
    SerialUSB.print(filename);
    SerialUSB.print(" failed. Aborting ...");
    on_exit_with_error_do();
  }
  file.close();

  // Habilitamos el uso del rtc
  rtc.begin();

  // Analizamos las dos cadenas para extraer fecha y hora y fijar el RTC
  if (!setDateTime(__DATE__, __TIME__))
  {
    SerialUSB.println("setDateTime() failed!\nExiting ...");
    while (1) { ; }
  }

  pinMode(externalPin, INPUT_PULLUP); 
  LowPower.attachInterruptWakeup(externalPin, externalCallback, FALLING);
  LowPower.attachInterruptWakeup(RTC_ALARM_WAKEUP, alarmCallback, CHANGE);
  // Activar la alarma cada 10 segundos a partir de 5 secs
  setPeriodicAlarm(10, 5);

  // Limpiamos _rtcFlag
  _rtcFlag = 0;
  
  // Activamos la rutina de atención
  rtc.attachInterrupt(alarmCallback);

  USBDevice.detach();
  LowPower.sleep();
}

void loop()
{
    // Si se activó la bandera de interrupciones (ya sea por RTC o por pin externo)
    if (_rtcFlag) {
        // Conectar USB para enviar datos
        USBDevice.attach();
        delay(1500);
        SerialUSB.begin(9600);
        while(!SerialUSB) {;}

        // Obtener la fecha y hora actual
        char* dateTime = getDateTime();
        writeInFile(dateTime);
        readFile();
        _rtcFlag--;

        // Advertencia si hay eventos de interrupción sin atender
        if (_rtcFlag > 0) {
            SerialUSB.println("WARNING: Unattended RTC alarm events!");
        }

        // Desconectar el USB y dormir el sistema
        USBDevice.detach();
        LowPower.sleep();
    }
}

bool setDateTime(const char * date_str, const char * time_str)
{
  char month_str[4];
  char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  uint16_t i, mday, month, hour, min, sec, year;

  if (sscanf(date_str, "%3s %hu %hu", month_str, &mday, &year) != 3) return false;
  if (sscanf(time_str, "%hu:%hu:%hu", &hour, &min, &sec) != 3) return false;

  for (i = 0; i < 12; i++) {
    if (!strncmp(month_str, months[i], 3)) {
      month = i + 1;
      break;
    }
  }
  if (i == 12) return false;
  
  rtc.setTime((uint8_t)hour, (uint8_t)min, (uint8_t)sec);
  rtc.setDate((uint8_t)mday, (uint8_t)month, (uint8_t)(year - 2000));
  return true;
}

// --------------------------------------------------------------------------------
// Imprime la hora y fecha en un formato internacional estándar
// --------------------------------------------------------------------------------
char* getDateTime()
{
  const char *weekDay[7] = { "Sun", "Mon", "Tue", "Wed", "Thr", "Fri", "Sat" };
  
  // Obtenemos el tiempo Epoch, segundos desde el 1 de enero de 1970
  time_t epoch = rtc.getEpoch();

  // Convertimos a la forma habitual de fecha y hora
  struct tm stm;
  gmtime_r(&epoch, &stm);
  
  // Generamos e imprimimos la fecha y la hora
  static char dateTime[32]; 
  snprintf(dateTime, sizeof(dateTime),"%s %4u/%02u/%02u %02u:%02u:%02u\n",
           weekDay[stm.tm_wday], 
           stm.tm_year + 1900, stm.tm_mon + 1, stm.tm_mday, 
           stm.tm_hour, stm.tm_min, stm.tm_sec);

  return dateTime;
}

// --------------------------------------------------------------------------------
// Programa la alarma del RTC para que se active en period_sec segundos a 
// partir de "offset" en segundos desde el instante actual
// --------------------------------------------------------------------------------
void setPeriodicAlarm(uint32_t period_sec, uint32_t offsetFromNow_sec)
{
  //Se le añade un -1 porque hay un delay de 1 segundo a la hora de imprimir el tiempo
  _period_sec = period_sec-1;
  rtc.setAlarmEpoch(rtc.getEpoch() + offsetFromNow_sec);

  // Ver enum Alarm_Match en RTCZero.h
  rtc.enableAlarm(rtc.MATCH_YYMMDDHHMMSS);
}

// --------------------------------------------------------------------------------
// Rutina de servicio asociada a la interrupción provocada por la expiración de la 
// alarma.
// --------------------------------------------------------------------------------

void alarmCallback()
{
    is_rtc_interrupt = 0;
    _rtcFlag++;
    alarmIterations++;

    // Reprogramar la alarma usando el mismo periodo
    rtc.setAlarmEpoch(rtc.getEpoch() + _period_sec);
}

void writeInFile(const char* dateTime)
{
    File file = filesystem.open(filename, WRITE_ONLY | APPEND);
    if (!file) {
        SerialUSB.print("Opening file ");
        SerialUSB.print(filename);
        SerialUSB.print(" failed for appending. Aborting ...");
        on_exit_with_error_do();
    }

    // Escribir el tipo de interrupción que ocurrió
    const char* message;
    if (is_rtc_interrupt == 0) {
        message = "RTC interruption: ";
    } else if (is_rtc_interrupt == 1) {
        message = "External pin interruption: ";
    }

    int messageLength = strlen(message);
    int bytesWrittenMessage = file.write((void *)message, messageLength);
    if (bytesWrittenMessage != messageLength) {
        SerialUSB.println("Failed to write interruption message. Aborting ...");
        on_exit_with_error_do();
    }
    
    // Escribir la fecha y hora
    int const bytes_to_write = strlen(dateTime);
    int const bytes_written = file.write((void *)dateTime, bytes_to_write);
    if (bytes_to_write != bytes_written) {
        SerialUSB.print("write() failed with error code "); 
        SerialUSB.println(filesystem.err());
        SerialUSB.println("Aborting ...");
        on_exit_with_error_do();
    }

    // Cerramos el fichero
    file.close();
}
void readFile()
{
          // Abrimos el fichero para lectura
    File file = filesystem.open(filename,  READ_ONLY);
    if (!file) {
      SerialUSB.print("Opening file ");
      SerialUSB.print(filename);
      SerialUSB.print(" failed for reading. Aborting ...");
      on_exit_with_error_do();
    }    
    SerialUSB.print("\nReading file contents:\n");
    
    // Leemos el contenido del fichero hasta alcanzar la marca EOF
    while(!file.eof()) {
      char c;
      int const bytes_read = file.read(&c, sizeof(c));
      if (bytes_read) {
        SerialUSB.print(c);
      }
    }

    // Cerramos el fichero
    file.close();
}

void externalCallback()
{
    is_rtc_interrupt = 1;
    _rtcFlag++;
}

void on_exit_with_error_do()
{
  filesystem.unmount();
  exit(EXIT_FAILURE);
}