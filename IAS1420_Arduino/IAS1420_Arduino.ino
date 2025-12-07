#include <WiFiS3.h>

// WiFi seaded (SCADA ja Arduino samas võrgus)
const char* WIFI_SSID = "***";
const char* WIFI_PASS = "***";

const uint16_t MB_PORT = 502;
WiFiServer mbServer(MB_PORT);

static inline uint16_t be16(const uint8_t* p){ return (uint16_t)p[0] << 8 | p[1]; }
static inline void     wbe16(uint8_t* p, uint16_t v){ p[0] = v >> 8; p[1] = v & 0xFF; }

const int enA    = 9;    // L298N ENA (PWM)
const int in1    = 8;    // Direction
const int in2    = 7;    // Direction
const int potPin = A0;   // Potentsiomeeter
const int tempPin= A1;   // Temperatuuriandur
const int ledPin = 2;    // LED

const int   PWM_MIN = 245;      // Miinimum PWM, millest alates ventilaator reaalselt käivitub
const int   PWM_MAX = 255;
const float FULL_SPEED_SPAN = 20.0;  // Kui palju °C üle seadmispunkti on vaja maksimum kiiruse saavutamiseks
const float LED_HYST       = 0.5;

#define HREG_COUNT 5

#define HR_SETPOINT 0 // HR0: setpointC * 10 (0.1°C täpsusega seadmispunkt)
#define HR_TEMP     1 // HR1: temperatureC * 10 (0.1°C täpsusega mõõdetud temperatuur)
#define HR_PWM      2 // HR2: PWM (0..255) 
#define HR_LED      3 // HR3: LED (0/1)
#define HR_MODE     4 // HR4: MODE käsk SCADA-st (0 = MANUAL, 1 = AUTO)

int   lastPWM = 0;
bool  ledOn   = false;

// HR4 (MODE) on vaikimisi 0 ehk käsirežiim
uint16_t hreg[HREG_COUNT] = {0,0,0,0,0};

// Muutuja, mis peegeldab režiimikäsku HR4-st (0 = MANUAL, 1 = AUTO)
volatile uint8_t switchCmd = 0;
uint8_t lastSwitchCmd = 255;

// updateFanAndRegs()
// Uuendab kindla sagedusega (10 Hz) ventilaatori kiirust ja LED-i vastavalt
// mõõdetud temperatuurile, seadmispunktile ja töörežiimile (MANUAL/AUTO)
// Seejärel kirjutab värsked väärtused Modbus hoideregistritesse (HR1..HR3)
void updateFanAndRegs()
{
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  if (now - lastUpdate < 100)   // 10 Hz
    return;
  lastUpdate = now;

  // 0 = MANUAL, 1 = AUTO (HR4-st)
  switchCmd = (hreg[HR_MODE] != 0) ? 1 : 0;
  bool autoMode = (switchCmd == 1);

  if (switchCmd != lastSwitchCmd) 
  {
    lastSwitchCmd = switchCmd;
    Serial.print("SWITCH CMD (from HR4) = ");
    Serial.println(switchCmd);  // 0 or 1
  }

  int potValue = analogRead(potPin);   // 0..1023 (potentsiomeeter)
  int rawTemp  = analogRead(tempPin);  // 0..1023 (temperatuuriandur)

  float temperatureC = (rawTemp * 100.0) / 1023.0;  // 0..100°C

  // Seadmispunkt loetakse alati HR0-st (SCADA-st)
  float setpointC = hreg[HR_SETPOINT] / 10.0f;   

  float error = temperatureC - setpointC;
  int   targetPWM = 0;

  if (autoMode) 
  {
    // AUTOMAATREŽIIM: ventilaatori kiirus sõltub temperatuuri veast
    if (error > 0) 
    {
      float ratio = error / FULL_SPEED_SPAN;
      if (ratio > 1.0) ratio = 1.0;
      targetPWM = PWM_MIN + (int)((PWM_MAX - PWM_MIN) * ratio);
      if (targetPWM < PWM_MIN) targetPWM = PWM_MIN;
      if (targetPWM > PWM_MAX) targetPWM = PWM_MAX;
    } 
    else 
    {
      targetPWM = 0;
    }
  } 
  else 
  {
    // KÄSIREŽIIM: ventilaatori kiirus potentsiomeetri järgi
    float ratio = potValue / 1023.0f;   // 0..1

    if (ratio < 0.05f) 
    {
      targetPWM = 0;
    } 
    else 
    {
      float r = (ratio - 0.05f) / 0.95f;
      if (r < 0)   r = 0;
      if (r > 1.0) r = 1.0;
      targetPWM = PWM_MIN + (int)((PWM_MAX - PWM_MIN) * r);
    }
  }
 
  // Kirjutame arvutatud PWM väljundisse (ventilaatorile)
  analogWrite(enA, targetPWM);
  lastPWM = targetPWM;

  // LED põleb, kui temperatuur on seadmispunktist kõrgem
  if (!ledOn && (temperatureC > setpointC + LED_HYST)) 
  {
    ledOn = true;
    digitalWrite(ledPin, HIGH);
  } 
  else if (ledOn && (temperatureC < setpointC - LED_HYST)) 
  {
    ledOn = false;
    digitalWrite(ledPin, LOW);
  }

  // Uuendame Modbus hoideregistreid SCADA jaoks
  hreg[HR_TEMP] = (uint16_t)(temperatureC * 10.0f);  // HR1
  hreg[HR_PWM]  = (uint16_t)targetPWM;              // HR2
  hreg[HR_LED]  = ledOn ? 1 : 0;                    // HR3

  // Debug
  Serial.print("MODE: ");
  Serial.print(autoMode ? "AUTO" : "MANUAL");
  Serial.print(" | SP: ");
  Serial.print(setpointC, 1);
  Serial.print(" C | T: ");
  Serial.print(temperatureC, 1);
  Serial.print(" C | PWM: ");
  Serial.print(targetPWM);
  Serial.print(" | LED: ");
  Serial.println(ledOn ? "ON" : "OFF");
}

// handleClient(WiFiClient& c)
// Teenindab ühte Modbus TCP klienti (nt Ignition SCADA).
// Loeb kliendi päringud, töötleb Modbus funktsioonikoode (FC3, FC6, FC16)
// ning saadab vastuseid. Toetab hoidereigistrite lugemist ja kirjutamist.
void handleClient(WiFiClient& c)
{
  uint8_t buf[256];

  while (c.connected())
  {
    // Iga tsükli alguses uuendame ventilaatori juhtimist ja registreid
    updateFanAndRegs();

    if (c.available() < 8)
    {
      // Ootame, kuni vähemalt Modbus TCP päis on saabunud (8 baiti)
      delay(1);
      continue;
    }

    int n = c.read(buf, sizeof(buf));
    if (n < 8) continue;

    uint16_t len   = (buf[4] << 8) | buf[5];
    uint8_t  unit  = buf[6];
    uint8_t  fc    = buf[7];

    uint8_t* out = buf;
    out[0] = buf[0]; // Transaction ID (jätame samaks)
    out[1] = buf[1];
    out[2] = 0;      // Protocol ID = 0
    out[3] = 0;
    out[6] = unit;   // Unit ID jääb samaks

    // FC3: Read Holding Registers 
    if (fc == 3 && len >= 5)
    {
      uint16_t start = be16(&buf[8]);  // Esimese registri aadress
      uint16_t qty   = be16(&buf[10]); // Loetavate registrite arv

      if (qty == 0 || (start + qty) > HREG_COUNT)
      {
        // Vigane päring -> exception 0x02 (Illegal Data Address)
        out[7] = fc | 0x80;
        out[8] = 0x02;
        wbe16(&out[4], 3);
        c.write(out, 7 + 3);
      } 
      else 
      {
        // Koostame vastuse lugemise jaoks
        out[7] = 3;
        out[8] = qty * 2; // baitide arv

        for (uint16_t i = 0; i < qty; i++)
        {
          wbe16(&out[9 + 2*i], hreg[start + i]);
        }

        wbe16(&out[4], 3 + 2*qty); // Pikkus
        c.write(out, 7 + 3 + 2*qty);
      }
      continue;
    }

    // FC6: Write Single Register 
    if (fc == 6 && len >= 5)
    {
      uint16_t addr = be16(&buf[8]);  // Registri aadress
      uint16_t val  = be16(&buf[10]); // Uus väärtus

      if (addr < HREG_COUNT)
      {
        hreg[addr] = val;

        Serial.print("FC6 write HR");
        Serial.print(addr);
        Serial.print(" = ");
        Serial.println(val);
      }

      c.write(buf, 12); // FC6 vastus on lihtsalt päringu "echo"
      continue;
    }

    // FC16: Write Multiple Registers
    if (fc == 16 && len >= 6)
    {
      uint16_t start = be16(&buf[8]);  // Esimene aadress
      uint16_t qty   = be16(&buf[10]); // Registrite arv
      uint8_t  byteCount = buf[12];    // Baitide arv andmeosas

      if (qty == 0 || (start + qty) > HREG_COUNT || byteCount != qty*2)
      {
        // Vigane päring -> exception 0x02
        out[7] = fc | 0x80;
        out[8] = 0x02;
        wbe16(&out[4], 3);
        c.write(out, 7 + 3);
        continue;
      }

      const uint8_t* p = &buf[13];

      for (uint16_t i = 0; i < qty; i++)
      {
        uint16_t v = be16(p + 2*i);
        hreg[start + i] = v;

        Serial.print("FC16 write HR");
        Serial.print(start + i);
        Serial.print(" = ");
        Serial.println(v);
      }

      // FC16 vastuses korratakse start-aadressi ja kogust
      out[7] = 16;
      out[8] = (start >> 8) & 0xFF;
      out[9] = start & 0xFF;
      out[10] = (qty >> 8) & 0xFF;
      out[11] = qty & 0xFF;

      wbe16(&out[4], 6);
      c.write(out, 12);
      continue;
    }

    // Toetamata funktsioonikood -> exception 0x01 (Illegal Function) 
    out[7] = fc | 0x80;
    out[8] = 0x01;
    wbe16(&out[4], 3);
    c.write(out, 7 + 3);
  }
}

// connectWifi()
// Ühendab Arduino WiFi võrku, kasutades ülal määratud SSID ja parooli
// Tagastab true, kui ühendus õnnestus, ja false, kui katkestati timeout'i tõttu
// Seriaalmonitori kaudu kuvatakse IP-aadress või veateade
bool connectWifi(uint32_t timeout_ms = 15000)
{
  Serial.print("Connecting to "); Serial.println(WIFI_SSID);
  WiFi.disconnect();
  delay(300);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms)
  {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi failed!");
    return false;
  }

  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

// setup()
// Käivitusfunktsioon, mida Arduino kutsub üks kord alguses
// Seadistab väljundpinnid, alustab seriaalsidet, loob WiFi ühenduse ja käivitab Modbus TCP serveri
void setup()
{
  pinMode(enA, OUTPUT);
  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);
  pinMode(ledPin, OUTPUT);
 
  // Seame L298N suuna (pidev sama suund) ja peatame ventilaatori
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);
  analogWrite(enA, 0);
  digitalWrite(ledPin, LOW);

  Serial.begin(115200);

  if (connectWifi())
  {
    mbServer.begin();
    Serial.println("Modbus TCP server started on port 502");
  }
}

// loop()
// Põhitsükkel, mida Arduino kordab lõputult
// Igas tsüklis uuendatakse ventilaatori juhtloogikat ning kontrollitakse, 
// kas Modbus TCP klient on ühendunud. Kui klient ühendub, teenindatakse seda handleClient() funktsioonis
void loop()
{
  // Uuendame juhtloogikat (ventilaator + LED + Modbus registrid)
  updateFanAndRegs();
  
  // Kontrollime, kas Modbus klient on ühendanud
  WiFiClient cli = mbServer.available();
  if (cli)
  {
    Serial.print("Client connected: ");
    Serial.println(cli.remoteIP());
    handleClient(cli);
    cli.stop();
    Serial.println("Client disconnected");
  }

  delay(5); // Väike viivitus, et vältida tarbetult kiiret tsüklit
}
