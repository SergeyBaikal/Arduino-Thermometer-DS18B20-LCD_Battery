// ==================== НАСТРОЙКИ ====================
#define ONE_WIRE_PIN 2       // Пин датчика DS18B20
#define BUZZER_PIN 3         // Пин пищалки
#define BATTERY_PIN A0       // Пин делителя батареи
#define STABLE_OK 3          // Количество одинаковых измерений для стабилизации
#define INTERVAL_SEC 10      // Интервал между измерениями в секундах
#define MAX_MEAS 999         // Максимальное количество измерений (после — сброс)
#define DRIFT 0.1            // Минимальная разница для стабильности, °C
#define BIP_COUNT 1          // Количество сигналов при стабилизации, кроме трёх - три это ошибка.

// Калибровка батареи:
// 1. Зарядите аккумулятор до 4.2V
// 2. Измерьте мультиметром напряжение на пине A0 (между A0 и GND)
// 3. Впишите это значение сюда (в вольтах), например 1.05
#define BAT_CORRECT_VOLTAGE 0.87  // Реальное напряжение на пине A0 при полной зарядке (В)
// ===================================================

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <GyverDS18.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
GyverDS18Single ds(ONE_WIRE_PIN);

// Коды ошибок датчика
#define SENSOR_OK   0
#define ERR_WAIT    1
#define ERR_CRC     2
#define ERR_VAL     3

float stableTemp = 0.0;
uint16_t measCount = 0;
bool stableReached = false;
float prevTemps[STABLE_OK];
uint8_t stableCount = 0;
float batteryVoltage = 0.0;
uint8_t batteryPercentValue = 0;

// Коэффициент коррекции: во сколько раз реальное напряжение отличается от измеренного
float Bat_correct = 1.0;

void beep(int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    if (count > 1) delay(100);
  }
}

uint8_t readTemperature(float &temp) {
  ds.requestTemp();
  if (!ds.waitReady()) return ERR_WAIT;
  if (!ds.readTemp()) return ERR_CRC;
  temp = ds.getTemp();
  if (temp == -127.0 || temp == 85.0) return ERR_VAL;
  return SENSOR_OK;
}

const char* sensorErrorStr(uint8_t err) {
  switch (err) {
    case ERR_WAIT: return "ERR_WAIT";
    case ERR_CRC:  return "ERR_CRC";
    case ERR_VAL:  return "ERR_VAL";
    default:       return "OK";
  }
}

float measureBattery() {
  int adc = analogRead(BATTERY_PIN);
  float voltage = adc * (5.0f / 1023.0f);
  // Применяем коррекцию
  voltage *= Bat_correct;
  // Делитель: 100к к Vbat, 27к к GND
  // Vbat = voltage * (100 + 27) / 27 = voltage * 4.704
  return voltage * 4.704f;
}

uint8_t batteryPercent(float voltage) {
  // Li-Ion 18650: 4.2 В = 100%, 3.0 В = 0%
  if (voltage >= 4.15) return 100;
  if (voltage >= 4.05) return 90;
  if (voltage >= 3.95) return 80;
  if (voltage >= 3.85) return 70;
  if (voltage >= 3.75) return 60;
  if (voltage >= 3.65) return 50;
  if (voltage >= 3.55) return 40;
  if (voltage >= 3.45) return 30;
  if (voltage >= 3.35) return 20;
  if (voltage >= 3.25) return 10;
  return 5;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(F("=== THERMOMETER v.1 ==="));

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  ds.setResolution(12);

  // Калибровка батареи при запуске
  // Измеряем напряжение на A0 и сравниваем с тем, что должно быть при 4.2V
  int adc = analogRead(BATTERY_PIN);
  float measuredVoltage = adc * (5.0f / 1023.0f);
  
  // При полной зарядке (4.2V) на A0 должно быть: 4.2 * 27 / (100 + 27) = 0.893V
  Bat_correct = 0.893f / BAT_CORRECT_VOLTAGE;
  
  Serial.print(F("ADC: "));
  Serial.print(adc);
  Serial.print(F(", Measured voltage on A0: "));
  Serial.print(measuredVoltage, 3);
  Serial.print(F("V, Correct voltage: "));
  Serial.print(BAT_CORRECT_VOLTAGE, 3);
  Serial.print(F("V, Bat_correct: "));
  Serial.println(Bat_correct, 4);

  // Замер батареи при старте
  batteryVoltage = measureBattery();
  batteryPercentValue = batteryPercent(batteryVoltage);

  Serial.print(F("Battery: "));
  Serial.print(batteryVoltage, 2);
  Serial.print(F("V ("));
  Serial.print(batteryPercentValue);
  Serial.println(F("%)"));

  // Заставка 3 секунды
  lcd.setCursor(0, 0);
  lcd.print(F("Thermometer v.1"));
  lcd.setCursor(0, 1);
  lcd.print(F("Battery "));
  lcd.print(batteryPercentValue);
  lcd.print(F("%"));
  delay(3000);
  lcd.clear();

  // Инициализация массива предыдущих температур
  for (int i = 0; i < STABLE_OK; i++) {
    prevTemps[i] = NAN;
  }

  Serial.println(F("Setup done"));
}

void loop() {
  // Сброс счётчика после MAX_MEAS
  if (measCount >= MAX_MEAS) {
    measCount = 0;
  }
  measCount++;

// Верхняя строка: Meas и обратный отсчёт
lcd.setCursor(0, 0);
lcd.print(F("Meas "));
if (measCount < 100) lcd.print(' ');
if (measCount < 10) lcd.print(' ');
lcd.print(measCount);
lcd.print(F(" Sec "));

// Обратный отсчёт
for (int sec = INTERVAL_SEC; sec > 0; sec--) {
  lcd.setCursor(14, 0);
  if (sec < 10) lcd.print(' ');
  lcd.print(sec);
  
  Serial.print(F("Countdown: "));
  Serial.println(sec);
  delay(1000);
}

  // Измерение
  float temp;
  uint8_t err = readTemperature(temp);

  Serial.print(F("Meas "));
  Serial.print(measCount);
  Serial.print(F(": "));

  if (err != SENSOR_OK) {
    // Ошибка датчика
    Serial.println(sensorErrorStr(err));

    lcd.setCursor(0, 1);
    lcd.print(sensorErrorStr(err));
    lcd.print(F("                "));

    beep(3);
    delay(1000);

    // Сброс стабилизации при ошибке
    stableReached = false;
    stableCount = 0;
    for (int i = 0; i < STABLE_OK; i++) {
      prevTemps[i] = NAN;
    }
    return;
  }

  // Успешное измерение
  Serial.print(temp, 2);
  Serial.println(F("°C"));

  // Проверка: если была стабилизация и температура изменилась — сброс
  if (stableReached && abs(temp - stableTemp) > DRIFT) {
    stableReached = false;
    stableCount = 0;
    for (int i = 0; i < STABLE_OK; i++) {
      prevTemps[i] = NAN;
    }
    Serial.println(F("Stable lost - new measurement"));
  }

  // Сдвигаем массив предыдущих температур
  for (int i = STABLE_OK - 1; i > 0; i--) {
    prevTemps[i] = prevTemps[i - 1];
  }
  prevTemps[0] = temp;

  // Проверяем стабильность
  if (!stableReached && measCount >= STABLE_OK) {
    bool stable = true;
    for (int i = 0; i < STABLE_OK; i++) {
      if (isnan(prevTemps[i])) {
        stable = false;
        break;
      }
      if (abs(prevTemps[0] - prevTemps[i]) > DRIFT) {
        stable = false;
        break;
      }
    }

    if (stable) {
      stableReached = true;
      stableTemp = temp;

      Serial.print(F("STABLE: "));
      Serial.print(temp, 2);
      Serial.println(F("°C"));

      lcd.setCursor(0, 1);
      lcd.print(F("Temp "));
      if (temp >= 0 && temp < 10) lcd.print(' ');
      if (temp > -10 && temp < 0) lcd.print(' ');
      lcd.print(temp, 2);
      lcd.print(F(" OK   "));

      beep(BIP_COUNT);
      return;
    }
  }

  // Вывод текущей температуры
  lcd.setCursor(0, 1);
  lcd.print(F("Temp "));
  if (temp >= 0 && temp < 10) lcd.print(' ');
  if (temp > -10 && temp < 0) lcd.print(' ');
  lcd.print(temp, 2);

  if (stableReached) {
    lcd.print(F(" OK   "));
  } else {
    lcd.print(F("      "));
  }
}