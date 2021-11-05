#include <main.h>


uint32_t state = 0;
uint32_t battery = 0;
uint32_t sleep_hour = 19;
uint32_t wakeup_hour = 7;
uint32_t error_code = 0;
uint32_t battery_threshold = -1;
bool request_sigfox_time = false;
bool request_sigfox_data = false;
uint32_t data_sigfox = 0;
RTCZero rtc;
byte read_reg = 0;

TimeChangeRule Rule_France_summer = {"RHEE", Last, Sun, Mar, 2, 120}; // Règle de passage à l'heure d'été pour la France
TimeChangeRule Rule_France_winter = {"RHHE", Last, Sun, Oct, 3, 60}; // Règle de passage à l'heure d'hiver la France
Timezone Convert_to_France(Rule_France_summer, Rule_France_winter); // Objet de conversion d'heure avec les caractéristiques de la métropole française

void sendI2C(){
  switch (read_reg)
  {
    case REG_SLEEP:
      Wire.write(sleep_hour);
      break;

    case REG_WAKE:
      Wire.write(wakeup_hour);
      break;

    case REG_ERROR:
      Wire.write(error_code);
      break;

    case REG_TIME:
      {
      u_int32_t time = rtc.getEpoch();
      uint8_t data[] = {time >> 24, time >> 16, time >> 8, time};
      Wire.write(data,(uint8_t)4);
      }
      break;

    case REG_STATE:
      Wire.write(state);
      break;

    case REG_BATTERY:
      Wire.write(battery);
      break;

    default:
      bitSet(error_code, ERROR_I2C_REG_NOT_FOUND);
      break;
  }

}

void receiveI2C(int packetSize)
{
  if (packetSize == 1) // READ OPERATION
  {
    byte read_reg = Wire.read();
  }
  else if (packetSize > 1)  // WRITE OPERATION
  {
    byte reg = Wire.read();
    uint32_t val = 0;
    int8_t i = packetSize - 2;
    while(i >= 0){
      val = val | Wire.read() << 8*i;
      i--;
    }
    switch (reg)
    {
      case REG_SLEEP:
        sleep_hour = val;
        break;

      case REG_WAKE:
        wakeup_hour = val;
        break;

      case REG_ERROR:
        error_code = val;
        break;

      case REG_STATE:
        state = val;
        break;

      case REG_TIME:
        if(val == 0){
          request_sigfox_time = true;
        }else{
          setRTCTime(val);
        }
        break;

      case REG_DATA:
        data_sigfox = val;
        request_sigfox_data = true;
        break;

      case REG_BATTERY_THRESHOLD:
        battery_threshold = val;
        break;

      default:
        bitSet(error_code, ERROR_I2C_REG_NOT_FOUND);
        break;
    }
  }
}


void sendDataToSigfox(uint32_t data){
  if(!SigFox.begin()){
      bitSet(error_code, ERROR_SIGFOX_BEGIN);
  }
  delay(100);
  SigFox.beginPacket();
  SigFox.write(data);
  int ret = SigFox.endPacket(); 
  if (ret > 0) {
    bitSet(error_code, ERROR_SIGFOX_TRANSMIT);
  }
  SigFox.end();
  bitSet(state, FLAG_SIGFOX_TRANSMITTED);
}

uint32_t getTimeFromSigfox(){
  if(!SigFox.begin()){
    bitSet(error_code, ERROR_SIGFOX_BEGIN);
  }
  delay(100);
  SigFox.beginPacket();
  SigFox.write(0);
  int ret = SigFox.endPacket(true); 
  if (ret > 0) {
    bitSet(error_code, ERROR_SIGFOX_TRANSMIT);
  }

  uint8_t time_buf[8] = {0,0,0,0,0,0,0,0};
  uint8_t i = 0;
  if (SigFox.parsePacket()) {
    while (SigFox.available()) {
      time_buf[i] = SigFox.read(); 
      i++;
    }
  }
  SigFox.end();
  uint32_t time = (uint32_t) (time_buf[0] << 24 | time_buf[1] << 16 | time_buf[2] << 8 | time_buf[3]);
  return  (uint32_t) Convert_to_France.toLocal(time);
}

void setRTCTime(uint32_t unix_time){
  time_t t = unix_time;
  rtc.setTime(hour(t), minute(t), second(t));
  rtc.setDate(day(t), month(t), year(t)-2000);
  bitSet(state, FLAG_TIME_REFRESHED);
}

void setAlarmForNextCycle(){
  SigFox.begin();
  delay(200);
  SigFox.end();
  delay(200);
  rtc.detachInterrupt();
  rtc.attachInterrupt(alarmNextCycle);
  rtc.setAlarmTime(00, 00, (rtc.getSeconds()+10)%60);
  rtc.enableAlarm(rtc.MATCH_SS);
  rtc.standbyMode();
}

void setAlarmForNextDay(){
  rtc.detachInterrupt();
  rtc.attachInterrupt(alarmFirstCycle);
  rtc.setAlarmTime(wakeup_hour, 00, 00);
  rtc.enableAlarm(rtc.MATCH_HHMMSS);
  rtc.standbyMode();
}

void powerUpRPI(){
  digitalWrite(PIN_POWER_5V, HIGH); // On active l'alimentation du RPI
  while(bitRead(state, FLAG_RPI_POWER) == 0){}; // On attends que le RPI s'initialise
}

void powerDownRPI(){
  delay(5000);
  digitalWrite(PIN_POWER_5V, LOW);
}

void alarmFirstCycle(){
    bitSet(state, FLAG_FIRST_CYCLE);
}

void alarmNextCycle(){
    bitClear(state, FLAG_FIRST_CYCLE);
}


// FIRST STARTUP
void setup()
{
  // SERIAL INIT
  Serial.begin(9600);
  while (!Serial){};

  // SigFox INIT
  SigFox.begin();
  delay(200);
  SigFox.end();
  delay(200);

  // I2C INIT
  Wire.begin(MKRFOX_ADDR);
  Wire.onReceive(receiveI2C); // register event
  Wire.onRequest(sendI2C);

  // RTC INIT
  rtc.begin(false);

  // PIN INIT
  pinMode(PIN_POWER_5V, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BATTERY, INPUT);

  // REGISTER INIT
  state = 0; 
  battery = 0;
  battery_threshold = 11;
  sleep_hour = 19;
  wakeup_hour = 7;
  error_code = 0;

  setRTCTime(1634293107);

}

void loop()
{
   
  state = 0;


}

