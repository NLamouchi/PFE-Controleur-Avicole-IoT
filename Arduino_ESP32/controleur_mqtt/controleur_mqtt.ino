/*
  Contrôleur de la maquette avicole — ESP32 + MQTT (application web, Node-RED en arrière-plan)
  ------------------------------------------------------------------------
  Matériel (câblage détaillé dans README.md) :
    ESP32 DevKit V1, capteur BME280 ou BMP280 en I2C, module 4 relais 5 V
    Relais 1 (D26) : chauffage CTP 24 V
    Relais 2 (D27) : ventilateur 24 V
    Relais 3 (D25) : pompe 24 V du pad-cooling

  L'ESP32 régule seule. Si le Wi-Fi ou le broker tombe, la régulation continue et
  un éventuel mode manuel repasse en automatique. L'application web (dossier application_web)
  sert à surveiller et à commander ; Node-RED enregistre les mesures et évalue l'alerte sanitaire.

  Régulation automatique (entrée dans le mode / sortie du mode, hystérésis de 0,5 °C) :
    CHAUFFAGE        T <= consigne - 0,5 / T >= consigne       chauffage proportionnel (PWM lent sur 20 s)
    NEUTRE           aucune autre condition remplie            tout arrêté
    VENTILATION      T >= consigne + 2   / T <= consigne + 1,5 ventilateur
    REFROIDISSEMENT  T >= consigne + 4   / T <= consigne + 3,5 ventilateur + pompe (marche sous 80 % HR,
                                                               arrêt au-dessus de 85 %)
    DEFAUT CAPTEUR   3 lectures ratées   / 1re lecture valide  tout arrêté
  Le chauffage n'est jamais actif en même temps que le ventilateur ou la pompe.

  Sujets MQTT (broker Mosquitto ; l'application web s'y connecte par WebSocket, port 9001) :
    ESP32 -> application et Node-RED
                        avicole/etat          JSON, toutes les 2 s et après chaque commande
                        avicole/statut        "en ligne" / "hors ligne" (message retenu)
    application -> ESP32
                        avicole/cmd/consigne  valeur de 15 à 35 (°C)
                        avicole/cmd/auto      1 = automatique, 0 = manuel
                        avicole/cmd/chauffage, avicole/cmd/ventilateur, avicole/cmd/pompe
                                              1 = marche, 0 = arrêt (mode manuel seulement)

  Bibliothèques (gestionnaire de bibliothèques de l'IDE Arduino) :
    PubSubClient (Nick O'Leary), Adafruit BME280 Library, Adafruit BMP280 Library
  Carte : « DOIT ESP32 DEVKIT V1 » (paquet « esp32 » d'Espressif)

  Mohamed Nadim LAMOUCHI — PFE Polytech Intl / STE TAADIC
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>

// ================= À ADAPTER =================
const char *WIFI_SSID = "NOM_DU_RESEAU";   // partage de connexion de l'iPhone : activer « Maximiser la compatibilité » (2,4 GHz)
const char *WIFI_MDP  = "MOT_DE_PASSE_WIFI";   // à remplacer par le mot de passe du partage de connexion
const char *MQTT_HOTE = "ADRESSE_IPV4_DU_PC";   // IPv4 du PC où tourne Mosquitto (commande ipconfig, carte Wi-Fi)
                                           // sur un partage iPhone, le PC reçoit en général une adresse 172.20.10.x
const uint16_t MQTT_PORT = 1883;

// ================= Broches =================
const int PIN_SDA       = 21;
const int PIN_SCL       = 22;
const int PIN_CHAUFFAGE = 26;   // relais IN1
const int PIN_VENTILO   = 27;   // relais IN2
const int PIN_POMPE     = 25;   // relais IN3

// Le module relais est actif à l'état bas : LOW = relais collé
const int RELAIS_ON  = LOW;
const int RELAIS_OFF = HIGH;

// ================= Réglages =================
float consigne = 26.0;                          // °C, modifiable depuis l'application web
const float ECART_VENT   = 2.0;                 // ventilation au-dessus de consigne + 2 °C
const float ECART_PAD    = 4.0;                 // pad-cooling au-dessus de consigne + 4 °C
const float HYST         = 0.5;                 // hystérésis de chaque seuil (°C)
const float BANDE_CHAUF  = 2.0;                 // chauffage à 100 % à consigne - 2 °C
const float HR_MAX_PAD   = 85.0;                // pas de pad-cooling si l'air est déjà humide (%)
const float T_MAX_MANUEL = 40.0;                // en manuel, chauffage coupé au-delà (°C)
const unsigned long PERIODE_CHAUF_MS  = 20000;  // période du PWM lent du chauffage
const unsigned long IMPULSION_MIN_MS  = 2000;   // pas de marche ni d'arrêt plus court
const unsigned long PERIODE_MESURE_MS = 2000;
const unsigned long RECONNEXION_MS    = 10000;  // délai entre deux tentatives Wi-Fi / MQTT

// ================= MQTT =================
const char *TOPIC_ETAT   = "avicole/etat";
const char *TOPIC_STATUT = "avicole/statut";
const char *TOPIC_CMD    = "avicole/cmd/#";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ================= État =================
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
int echecs = 0;

bool modeAuto = true;                           // false = commande manuelle depuis l'application
bool manuChauffage = false, manuVentilo = false, manuPompe = false;

float fractionChauffe = 0;                      // 0 à 1
bool chauffageOn = false, ventiloOn = false, pompeOn = false;
unsigned long debutFenetre = 0, derniereMesure = 0;

// ================= Capteur =================
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

// ================= Régulation =================
Mode choisirMode(float t, Mode m) {
  const float sPad  = consigne + ECART_PAD;
  const float sVent = consigne + ECART_VENT;
  if (t >= sPad || (m == REFROIDISSEMENT && t > sPad - HYST)) return REFROIDISSEMENT;
  if (t >= sVent || ((m == VENTILATION || m == REFROIDISSEMENT) && t > sVent - HYST)) return VENTILATION;
  if (t <= consigne - HYST || (m == CHAUFFAGE && t < consigne)) return CHAUFFAGE;
  return NEUTRE;
}

void mesurer() {
  if (lireCapteur()) {
    echecs = 0;
    mode = choisirMode(temperature, mode);
  } else {
    echecs++;
    if (echecs >= 3) mode = DEFAUT;             // 3 lectures ratées : on arrête tout
    if (echecs % 5 == 0) initCapteur();         // nouvelle tentative de connexion au capteur
  }
}

void calculerSorties() {
  if (!modeAuto) {                              // commande manuelle depuis l'application
    bool securite = (mode == DEFAUT) || temperature >= T_MAX_MANUEL;
    if (securite) manuChauffage = false;        // refus mémorisé : l'opérateur doit relancer
    fractionChauffe = manuChauffage ? 1.0f : 0.0f;
    ventiloOn = manuVentilo;
    pompeOn = manuPompe;
    return;
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
    default:                                    // NEUTRE ou DEFAUT : tout arrêté
      fractionChauffe = 0;
      ventiloOn = false;
      pompeOn = false;
  }
}

// ================= Sorties =================
void commander(int pin, bool marche) {
  digitalWrite(pin, marche ? RELAIS_ON : RELAIS_OFF);
}

// Le chauffage est modulé par un PWM lent : sur chaque période de 20 s, le relais reste
// collé pendant fractionChauffe × 20 s. Un relais ne supporte pas un PWM rapide.
void appliquerSorties() {
  static unsigned long dureeMarche = 0;
  static bool demandePrecedente = false;
  unsigned long maintenant = millis();

  bool demande = fractionChauffe > 0;
  if (demande && !demandePrecedente)
    debutFenetre = maintenant - PERIODE_CHAUF_MS;   // nouvelle demande : la période démarre tout de suite
  demandePrecedente = demande;

  if (maintenant - debutFenetre >= PERIODE_CHAUF_MS) {
    debutFenetre = maintenant;
    dureeMarche = (unsigned long)(fractionChauffe * PERIODE_CHAUF_MS);
    if (dureeMarche < IMPULSION_MIN_MS) dureeMarche = 0;
    if (dureeMarche > PERIODE_CHAUF_MS - IMPULSION_MIN_MS) dureeMarche = PERIODE_CHAUF_MS;
  }
  chauffageOn = demande && (maintenant - debutFenetre) < dureeMarche;

  commander(PIN_CHAUFFAGE, chauffageOn);
  commander(PIN_VENTILO, ventiloOn);
  commander(PIN_POMPE, pompeOn);
}

// ================= MQTT =================
void formater(char *dest, size_t taille, float v) {
  if (isnan(v)) snprintf(dest, taille, "null");   // JSON n'accepte pas "nan"
  else snprintf(dest, taille, "%.1f", v);
}

// Construit l'état en JSON, l'affiche sur le moniteur série et le publie sur le broker
void publierEtat() {
  char t[12], hr[12], json[256];
  formater(t, sizeof(t), temperature);
  formater(hr, sizeof(hr), humidite);
  const char *texteMode = (mode == DEFAUT) ? NOM_MODE[DEFAUT] : (modeAuto ? NOM_MODE[mode] : "MANUEL");

  snprintf(json, sizeof(json),
           "{\"t\":%s,\"hr\":%s,\"consigne\":%.1f,\"mode\":\"%s\",\"auto\":%s,"
           "\"chauffage\":%d,\"puissance\":%d,\"ventilateur\":%d,\"pompe\":%d}",
           t, hr, consigne, texteMode, modeAuto ? "true" : "false",
           chauffageOn, (int)(fractionChauffe * 100), ventiloOn, pompeOn);

  Serial.println(json);
  if (mqtt.connected()) mqtt.publish(TOPIC_ETAT, json);
}

// Commandes reçues de l'application web, par le broker
void recevoir(char *topic, byte *contenu, unsigned int longueur) {
  char texte[16];
  if (longueur >= sizeof(texte)) longueur = sizeof(texte) - 1;
  memcpy(texte, contenu, longueur);
  texte[longueur] = '\0';
  bool vrai = (texte[0] == '1' || texte[0] == 't');   // accepte "1" ou "true"

  if (strcmp(topic, "avicole/cmd/consigne") == 0) {
    float v = atof(texte);
    if (v >= 15 && v <= 35) consigne = v;
  } else if (strcmp(topic, "avicole/cmd/auto") == 0) {
    modeAuto = vrai;
    manuChauffage = manuVentilo = manuPompe = false;  // en manuel, on repart de tout arrêté
  } else if (!modeAuto) {
    if (strcmp(topic, "avicole/cmd/chauffage") == 0) {
      manuChauffage = vrai;
      if (vrai) manuVentilo = manuPompe = false;       // jamais chauffage + ventilation
    } else if (strcmp(topic, "avicole/cmd/ventilateur") == 0) {
      manuVentilo = vrai;
      if (vrai) manuChauffage = false;
    } else if (strcmp(topic, "avicole/cmd/pompe") == 0) {
      manuPompe = vrai;
      if (vrai) manuChauffage = false;
    }
  }

  calculerSorties();
  appliquerSorties();
  publierEtat();                                      // retour immédiat vers l'application
}

void maintenirConnexion() {
  static unsigned long dernierEssai = 0;
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }

  if (!modeAuto) {                                    // plus de broker : retour en automatique
    modeAuto = true;
    manuChauffage = manuVentilo = manuPompe = false;
  }

  if (millis() - dernierEssai < RECONNEXION_MS) return;
  dernierEssai = millis();

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    return;
  }
  // Bloque au plus 1 à 2 s si le broker est injoignable ; les relais gardent leur état pendant ce temps
  if (mqtt.connect("esp32-avicole", nullptr, nullptr, TOPIC_STATUT, 0, true, "hors ligne")) {
    mqtt.publish(TOPIC_STATUT, "en ligne", true);
    mqtt.subscribe(TOPIC_CMD);
    Serial.printf("MQTT connecté (ESP32 : %s)\n", WiFi.localIP().toString().c_str());
  }
}

// ================= Programme principal =================
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

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_MDP);                    // connexion en arrière-plan : la régulation n'attend pas
  mqtt.setServer(MQTT_HOTE, MQTT_PORT);
  mqtt.setCallback(recevoir);
  mqtt.setBufferSize(512);
  wifiClient.setConnectionTimeout(1000);              // broker absent : 1 s de blocage au lieu de 3 s
  mqtt.setSocketTimeout(2);                           // attente de la réponse du broker : 2 s au lieu de 15 s

  derniereMesure = millis() - PERIODE_MESURE_MS;      // première mesure immédiate
}

void loop() {
  maintenirConnexion();

  if (millis() - derniereMesure >= PERIODE_MESURE_MS) {
    derniereMesure = millis();
    mesurer();
    calculerSorties();
    appliquerSorties();
    publierEtat();
  }

  appliquerSorties();                                 // PWM lent du chauffage
  delay(20);
}
