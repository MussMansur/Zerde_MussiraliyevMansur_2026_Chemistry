/*
  Умный полив — управление клапаном 2.
  Arduino Uno: A0 — влажность почвы, A1 — TDS, A2 — мутность,
  A3 — pH резервуара 3, D7 — вход IN2 реле клапана 2.
  Пороговые значения воды приведены в физических единицах.
  Калибровка датчиков обязательна до включения полива.
*/
const byte SOIL_PIN = A0;
const byte TDS_PIN = A1;
const byte TURBIDITY_PIN = A2;
const byte PH_PIN = A3;
const byte VALVE_2_RELAY_PIN = 7;

// Для реле с активным уровнем HIGH поменяйте значения местами.
const byte RELAY_ON = LOW;
const byte RELAY_OFF = HIGH;

const unsigned long WATERING_TIME = 20UL * 1000UL;
const unsigned long NORMAL_CHECK_INTERVAL = 10UL * 60UL * 1000UL;
const unsigned long AFTER_WATERING_INTERVAL = 2UL * 60UL * 1000UL;

// Измерьте показания датчика в сухой и влажной почве.
const int SOIL_DRY_ADC = 800;
const int SOIL_WET_ADC = 350;
const int SOIL_MIN_PERCENT = 40;

// Ориентиры для полива; их можно изменить для конкретного растения.
const float TDS_MAX_MG_L = 450.0;      // строго ниже 450 мг/л
const float TURBIDITY_MAX_NTU = 10.0; // предварительный порог: ниже 10 NTU
const float PH_MIN = 6.5;
const float PH_MAX = 8.4;

// ОБЯЗАТЕЛЬНА калибровка каждым датчиком на двух эталонах.
// Укажите реальные ADC и значения эталонов, затем измените флаг на true.
// Заглушки 0, 0 намеренно не являются калибровкой.
// Линейная интерполяция допустима только если она подтверждена для модели
// датчика в измеряемом диапазоне. Для TDS важна температура воды.
// Опорные точки должны охватывать применяемый диапазон: показания вне
// двух точек отвергаются. Измерения и калибровку выполняйте при одинаковой
// температуре; иначе нужна предусмотренная изготовителем компенсация.
const bool WATER_SENSORS_CALIBRATED = false;
struct Calibration {
  int adc1;
  float value1;
  int adc2;
  float value2;
};
const Calibration TDS_CAL = {0, 0.0, 0, 0.0};       // мг/л
const Calibration TURBIDITY_CAL = {0, 0.0, 0, 0.0}; // NTU
const Calibration PH_CAL = {0, 0.0, 0, 0.0};        // pH

enum State { CHECK_SENSORS, WATERING, WAITING };
State state = CHECK_SENSORS;
unsigned long stateStartedAt = 0;
unsigned long waitInterval = 0;

int readAverage(byte pin) {
  long sum = 0;
  for (byte i = 0; i < 10; i++) {
    sum += analogRead(pin);
    delay(10);
  }
  return sum / 10;
}

int readSoilPercent() {
  int raw = readAverage(SOIL_PIN);
  long percent = map(raw, SOIL_DRY_ADC, SOIL_WET_ADC, 0, 100);
  return constrain(percent, 0, 100);
}

// Reject uncalibrated, saturated, and out-of-calibration readings.
bool calibratedValue(int raw, const Calibration &cal, float &value) {
  if (cal.adc1 <= 0 || cal.adc1 >= 1023 ||
      cal.adc2 <= 0 || cal.adc2 >= 1023 || cal.adc1 == cal.adc2 ||
      cal.value1 == cal.value2 || raw <= 0 || raw >= 1023) return false;
  int low = min(cal.adc1, cal.adc2);
  int high = max(cal.adc1, cal.adc2);
  if (raw < low || raw > high) return false;
  value = cal.value1 + (float)(raw - cal.adc1) *
          (cal.value2 - cal.value1) / (cal.adc2 - cal.adc1);
  return true;
}

bool waterIsAcceptable() {
  int tdsRaw = readAverage(TDS_PIN);
  int turbidityRaw = readAverage(TURBIDITY_PIN);
  int phRaw = readAverage(PH_PIN);
  Serial.print("TDS ADC: "); Serial.print(tdsRaw);
  Serial.print(" | Turbidity ADC: "); Serial.print(turbidityRaw);
  Serial.print(" | pH ADC: "); Serial.println(phRaw);

  if (!WATER_SENSORS_CALIBRATED) {
    Serial.println("Water sensors not calibrated; valve stays closed.");
    return false;
  }
  float tds, turbidity, ph;
  if (!calibratedValue(tdsRaw, TDS_CAL, tds) ||
      !calibratedValue(turbidityRaw, TURBIDITY_CAL, turbidity) ||
      !calibratedValue(phRaw, PH_CAL, ph)) {
    Serial.println("Invalid reading or outside calibration range.");
    return false;
  }
  Serial.print("TDS mg/L: "); Serial.print(tds);
  Serial.print(" | Turbidity NTU: "); Serial.print(turbidity);
  Serial.print(" | pH: "); Serial.println(ph);
  return tds >= 0.0 && tds < TDS_MAX_MG_L &&
         turbidity >= 0.0 && turbidity < TURBIDITY_MAX_NTU &&
         ph >= PH_MIN && ph <= PH_MAX;
}

void openValve() {
  digitalWrite(VALVE_2_RELAY_PIN, RELAY_ON);
}

void closeValve() {
  digitalWrite(VALVE_2_RELAY_PIN, RELAY_OFF);
}

void startWaiting(unsigned long interval) {
  closeValve();
  waitInterval = interval;
  stateStartedAt = millis();
  state = WAITING;
}

void setup() {
  digitalWrite(VALVE_2_RELAY_PIN, RELAY_OFF);
  pinMode(VALVE_2_RELAY_PIN, OUTPUT);

  Serial.begin(9600);
  Serial.println("Система полива запущена.");
  Serial.println("Проверьте калибровку датчиков перед использованием.");
}

void loop() {
  unsigned long now = millis();

  switch (state) {
    case CHECK_SENSORS: {
      closeValve();
      int soilPercent = readSoilPercent();

      Serial.print("Влажность почвы: ");
      Serial.print(soilPercent);
      Serial.println(" %");

      if (soilPercent >= SOIL_MIN_PERCENT) {
        Serial.println("Влажность достаточная. Следующая проверка через 10 минут.");
        startWaiting(NORMAL_CHECK_INTERVAL);
        break;
      }

      Serial.println("Почва сухая. Проверяем воду в резервуаре 3.");
      if (waterIsAcceptable()) {
        Serial.println("Вода в норме. Открываем клапан 2 на 20 секунд.");
        openValve();
        stateStartedAt = millis();
        state = WATERING;
      } else {
        Serial.println("Вода вне заданных пределов. Клапан закрыт.");
        startWaiting(NORMAL_CHECK_INTERVAL);
      }
      break;
    }

    case WATERING:
      if (now - stateStartedAt >= WATERING_TIME) {
        closeValve();
        Serial.println("Клапан закрыт. Проверка почвы через 2 минуты.");
        startWaiting(AFTER_WATERING_INTERVAL);
      }
      break;

    case WAITING:
      if (now - stateStartedAt >= waitInterval) {
        state = CHECK_SENSORS;
      }
      break;
  }
}
