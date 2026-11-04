/*
  Умный полив — управление клапаном 2.
  Arduino Uno: A0 — влажность почвы, A1 — TDS, A2 — мутность,
  A3 — pH резервуара 3, D7 — вход IN2 реле клапана 2.
  Пределы воды и калибровка почвы ниже приведены только как пример.
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

// Примеры значений АЦП (0–1023), не ppm, NTU или pH.
// Замените после калибровки каждого датчика.
const int TDS_MIN_ADC = 100;
const int TDS_MAX_ADC = 700;
const int TURBIDITY_MIN_ADC = 300;
const int TURBIDITY_MAX_ADC = 900;
const int PH_MIN_ADC = 300;
const int PH_MAX_ADC = 700;

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

bool inRange(int value, int minimum, int maximum) {
  return value >= minimum && value <= maximum;
}

bool waterIsAcceptable() {
  int tds = readAverage(TDS_PIN);
  int turbidity = readAverage(TURBIDITY_PIN);
  int ph = readAverage(PH_PIN);

  Serial.print("TDS ADC: ");
  Serial.print(tds);
  Serial.print(" | Мутность ADC: ");
  Serial.print(turbidity);
  Serial.print(" | pH ADC: ");
  Serial.println(ph);

  return inRange(tds, TDS_MIN_ADC, TDS_MAX_ADC) &&
         inRange(turbidity, TURBIDITY_MIN_ADC, TURBIDITY_MAX_ADC) &&
         inRange(ph, PH_MIN_ADC, PH_MAX_ADC);
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
