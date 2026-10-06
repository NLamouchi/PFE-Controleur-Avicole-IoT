/*
  Prototype de régulation thermique — maquette de bâtiment avicole
  ESP32 DevKit V1 + capteur BME280 ou BMP280 (I2C) + module 4 relais 5 V

  Relais 1 (IN1, D26) : chauffage (résistance CTP 24 V)
  Relais 2 (IN2, D27) : ventilateur (extraction)
  Relais 3 (IN3, D25) : pompe du pad-cooling
  Relais 4 (IN4)      : libre (2e ventilateur si besoin)

  Quatre modes, du plus froid au plus chaud :
    CHAUFFAGE        T < consigne            chauffage proportionnel (PWM lent)
    NEUTRE           consigne .. consigne+2  tout arrêté
    VENTILATION      T > consigne + 2 °C     ventilateur
    REFROIDISSEMENT  T > consigne + 4 °C     ventilateur + pompe du pad
  Le chauffage ne fonctionne jamais en même temps que le ventilateur ou la pompe.

  Moniteur série à 115200 bauds : affiche l'état toutes les 2 s.
  Taper un nombre entre 15 et 35 puis Entrée pour changer la consigne.

  Bibliothèques : « Adafruit BME280 Library » et « Adafruit BMP280 Library »
  (le gestionnaire de bibliothèques installe aussi leurs dépendances).
  Carte : « DOIT ESP32 DEVKIT V1 » (paquet « esp32 » d'Espressif).

  Mohamed Nadim LAMOUCHI — PFE Polytech Intl / STE TAADIC
*/

#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>

// ---------- Broches ----------
const int PIN_SDA       = 21;
const int PIN_SCL       = 22;
const int PIN_CHAUFFAGE = 26;   // IN1
const int PIN_VENTILO   = 27;   // IN2
const int PIN_POMPE     = 25;   // IN3

// Le module relais est actif à l'état bas : LOW = relais collé
const int RELAIS_ON  = LOW;
const int RELAIS_OFF = HIGH;

// ---------- Réglages ----------
float consigne = 26.0;                          // °C, modifiable par le moniteur série
const float ECART_VENT  = 2.0;                  // ventilation au-dessus de consigne + 2 °C
const float ECART_PAD   = 4.0;                  // pad-cooling au-dessus de consigne + 4 °C
const float HYST        = 0.5;                  // hystérésis de chaque seuil (°C)
const float BANDE_CHAUF = 2.0;                  // chauffage à 100 % à consigne - 2 °C
const float HR_MAX_PAD  = 85.0;                 // pas de pad-cooling si l'air est déjà humide (%)
const unsigned long PERIODE_CHAUF_MS  = 20000;  // période du PWM lent du chauffage
const unsigned long IMPULSION_MIN_MS  = 2000;   // pas de marche ni d'arrêt plus court
const unsigned long PERIODE_MESURE_MS = 2000;

// ---------- État ----------
enum Mode { CHAUFFAGE, NEUTRE, VENTILATION, REFROIDISSEMENT, DEFAUT };
const char *NOM_MODE[] = { "CHAUFFAGE", "NEUTRE", "VENTILATION", "REFROIDISSEMENT", "DEFAUT CAPTEUR" };

enum TypeCapteur { CAPTEUR_AUCUN, CAPTEUR_BME, CAPTEUR_BMP };

Adafruit_BME280 bme;
Adafruit_BMP280 bmp;
TypeCapteur capteur = CAPTEUR_AUCUN;
uint8_t adresseCapteur = 0;

Mode mode = NEUTRE;
float temperature = NAN;
float humidite = NAN;                           // reste NAN avec un BMP280 (pas d'humidité)
float fractionChauffe = 0;                      // 0 à 1
bool chauffageOn = false, ventiloOn = false, pompeOn = false;
unsigned long debutFenetre = 0, derniereMesure = 0;
int echecs = 0;

// ---------- Capteur ----------
bool initCapteur() {
  const uint8_t adresses[] = { 0x76, 0x77 };
  for (uint8_t a : adresses) {
    if (bme.begin(a, &Wire)) { capteur = CAPTEUR_BME; adresseCapteur = a; return true; }
    if (bmp.begin(a))        { capteur = CAPTEUR_BMP; adresseCapteur = a; return true; }
  }
  capteur = CAPTEUR_AUCUN;
  return false;
}

bool lireCapteur() {
  if (capteur == CAPTEUR_AUCUN) return false;

  // Le capteur répond-il, et mesure-t-il toujours ? Après une micro-coupure de son alimentation,
  // il redémarre en veille et renverrait sans fin une température figée d'environ 26 °C.
  Wire.beginTransmission(adresseCapteur);
  Wire.write(0xF4);                             // registre ctrl_meas (même adresse sur BME280 et BMP280)
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)adresseCapteur, 1) != 1) return false;
  if ((Wire.read() & 0x03) != 0x03) {           // repassé en veille : on le réinitialise
    initCapteur();
    return false;
  }

  float t, h = NAN;
  if (capteur == CAPTEUR_BME) { t = bme.readTemperature(); h = bme.readHumidity(); }
  else                        { t = bmp.readTemperature(); }
  if (isnan(t) || t < -10 || t > 85) return false;

  temperature = t;
  humidite = h;
  return true;
}

// ---------- Régulation ----------
Mode choisirMode(float t, Mode m) {
  const float sPad  = consigne + ECART_PAD;
  const float sVent = consigne + ECART_VENT;
  if (t >= sPad || (m == REFROIDISSEMENT && t > sPad - HYST)) return REFROIDISSEMENT;
  if (t >= sVent || ((m == VENTILATION || m == REFROIDISSEMENT) && t > sVent - HYST)) return VENTILATION;
  if (t <= consigne - HYST || (m == CHAUFFAGE && t < consigne)) return CHAUFFAGE;
  return NEUTRE;
}

void reguler() {
  if (lireCapteur()) {
    echecs = 0;
    Mode nouveau = choisirMode(temperature, mode);
    if (nouveau == CHAUFFAGE && mode != CHAUFFAGE)
      debutFenetre = millis() - PERIODE_CHAUF_MS;   // démarre une période de chauffe tout de suite
    mode = nouveau;
  } else {
    echecs++;
    if (echecs >= 3) mode = DEFAUT;                 // 3 lectures ratées : on arrête tout
    if (echecs % 5 == 0) initCapteur();             // nouvelle tentative de connexion
  }

  switch (mode) {
    case CHAUFFAGE:
      fractionChauffe = constrain((consigne - temperature) / BANDE_CHAUF, 0.0f, 1.0f);
      if (fractionChauffe < 0.1f) fractionChauffe = 0;        // moins de 2 s sur 20 s : relais non actionné
      else if (fractionChauffe > 0.9f) fractionChauffe = 1;   // arrêt de moins de 2 s : chauffage continu
      ventiloOn = false;
      pompeOn = false;
      break;
    case VENTILATION:
      fractionChauffe = 0;
      ventiloOn = true;
      pompeOn = false;
      break;
    case REFROIDISSEMENT:
      fractionChauffe = 0;
      ventiloOn = true;
      // pompe arrêtée au-dessus de 85 % HR, relancée sous 80 % ; toujours autorisée sans mesure d'humidité
      pompeOn = isnan(humidite) || humidite < (pompeOn ? HR_MAX_PAD : HR_MAX_PAD - 5);
      break;
    default:                                        // NEUTRE ou DEFAUT : tout arrêté
      fractionChauffe = 0;
      ventiloOn = false;
      pompeOn = false;
  }
}

// ---------- Sorties ----------
void commander(int pin, bool marche) {
  digitalWrite(pin, marche ? RELAIS_ON : RELAIS_OFF);
}

// Le chauffage est modulé par un PWM lent : sur chaque période de 20 s, le relais reste
// collé pendant fractionChauffe × 20 s. Un relais ne supporte pas un PWM rapide.
void appliquerSorties() {
  static unsigned long dureeMarche = 0;
  unsigned long maintenant = millis();
  if (maintenant - debutFenetre >= PERIODE_CHAUF_MS) {
    debutFenetre = maintenant;
    dureeMarche = (unsigned long)(fractionChauffe * PERIODE_CHAUF_MS);
    if (dureeMarche < IMPULSION_MIN_MS) dureeMarche = 0;
    if (dureeMarche > PERIODE_CHAUF_MS - IMPULSION_MIN_MS) dureeMarche = PERIODE_CHAUF_MS;
  }
  chauffageOn = fractionChauffe > 0 && (maintenant - debutFenetre) < dureeMarche;

  commander(PIN_CHAUFFAGE, chauffageOn);
  commander(PIN_VENTILO, ventiloOn);
  commander(PIN_POMPE, pompeOn);
}

// ---------- Affichage et consigne ----------
void afficher() {
  Serial.printf("[%5lu s] T = %.1f °C | HR = ", millis() / 1000, temperature);
  if (isnan(humidite)) Serial.print("--");
  else Serial.printf("%.0f %%", humidite);
  Serial.printf(" | consigne %.1f °C | %-15s | chauffage %3d %% (%s) | ventilateur %s | pompe %s\n",
                consigne, NOM_MODE[mode], (int)(fractionChauffe * 100), chauffageOn ? "ON" : "off",
                ventiloOn ? "ON" : "off", pompeOn ? "ON" : "off");
}

void lireConsigne() {
  if (!Serial.available()) return;
  float v = Serial.parseFloat();
  while (Serial.available()) Serial.read();         // vide le reste de la ligne
  if (v >= 15 && v <= 35) {
    consigne = v;
    Serial.printf(">> nouvelle consigne : %.1f °C\n", consigne);
  } else {
    Serial.println(">> consigne refusée (valeur entre 15 et 35 °C)");
  }
}

// ---------- Programme principal ----------
void setup() {
  Serial.begin(115200);

  // Relais au repos avant toute autre chose
  for (int pin : { PIN_CHAUFFAGE, PIN_VENTILO, PIN_POMPE }) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, RELAIS_OFF);
  }

  Wire.begin(PIN_SDA, PIN_SCL);
  if (initCapteur())
    Serial.printf("Capteur %s trouvé à l'adresse 0x%02X\n",
                  capteur == CAPTEUR_BME ? "BME280 (température + humidité)" : "BMP280 (température seule)",
                  adresseCapteur);
  else
    Serial.println("ERREUR : aucun BME280/BMP280 détecté (vérifier SDA = D21, SCL = D22, 3V3 et GND)");
  Serial.println("Tapez une consigne entre 15 et 35 puis Entrée pour la changer.");

  derniereMesure = millis() - PERIODE_MESURE_MS;     // première mesure immédiate
  debutFenetre = millis() - PERIODE_CHAUF_MS;
}

void loop() {
  lireConsigne();

  bool nouvelleMesure = false;
  if (millis() - derniereMesure >= PERIODE_MESURE_MS) {
    derniereMesure = millis();
    reguler();
    nouvelleMesure = true;
  }

  appliquerSorties();
  if (nouvelleMesure) afficher();
  delay(50);
}
