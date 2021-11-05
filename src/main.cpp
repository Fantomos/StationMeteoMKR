#include <main.h>


uint32_t state;
uint16_t battery;
uint32_t sleep_hour;
uint32_t wakeup_hour;
uint32_t error_code;
uint32_t battery_threshold;
bool request_sigfox_time;
bool request_sigfox_data;
uint32_t data_sigfox;
uint8_t read_reg = 0;
RTCZero rtc;
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
  rtc.setAlarmTime(00, (rtc.getMinutes()+CYCLE_TIME)%60, 00);
  rtc.enableAlarm(rtc.MATCH_MMSS);
  rtc.standbyMode();
}

void setAlarmForNextDay(){
  SigFox.begin();
  delay(200);
  SigFox.end();
  delay(200);
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

void powerDownRPI()
{
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
  // I2C INIT
  Wire.begin(MKRFOX_ADDR);
  Wire.onReceive(receiveI2C); // register event
  Wire.onRequest(sendI2C);

  // RTC INIT
  rtc.begin(false);

  // PIN INIT
  pinMode(PIN_POWER_5V, OUTPUT);
  pinMode(PIN_BATTERY, INPUT);

  // REGISTER INIT
  state = 0; 
  error_code = 0;
  battery = 0;
  sleep_hour = DEFAULT_SLEEP_HOUR;
  wakeup_hour = DEFAULT_WAKEUP_HOUR;
  battery_threshold = DEFAULT_BATTERY_THRESHOLD;
  request_sigfox_time = false;
  request_sigfox_data = false;
  data_sigfox = 0;

 

  // RTC INIT
  rtc.setHours(12); // Lors du 1er démarrage on règle l'heure dans la plage de fonctionnement
  rtc.attachInterrupt(alarmFirstCycle);
  rtc.setAlarmTime(00, (rtc.getMinutes()+CYCLE_TIME)%60, 00);
  rtc.enableAlarm(rtc.MATCH_MMSS);
  rtc.standbyMode();


}

void loop()
{
    rtc.disableAlarm();
    if(rtc.getHours() < sleep_hour && rtc.getHours() > wakeup_hour){
      battery = analogRead(PIN_BATTERY)*BATTERY_CONSTANT;
      if(battery > battery_threshold){
        powerUpRPI(); // On allume le RPI
        while(bitRead(state, FLAG_RPI_POWER) == 1){ // Tant que le RPI n'a pas terminé, on continue à répondre aux commandes I2C
            if(request_sigfox_time){
              setRTCTime(getTimeFromSigfox());
              request_sigfox_time = false;
            }
            if(request_sigfox_data){
              sendDataToSigfox(data_sigfox);
              request_sigfox_data = false;
            }
        };
        powerDownRPI(); // On éteints le RPI
      }
      setAlarmForNextCycle(); // On prépare le prochain révéil pour dans 10 min
    }else{
      setAlarmForNextDay(); // On prépare le prochain révéil pour le jour suivant
    }

}