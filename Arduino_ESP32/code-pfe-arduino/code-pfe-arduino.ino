#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <Preferences.h>
#include "HX711.h"

// =====================================================
// DHT11
// =====================================================

#define DHTPIN 4
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

// =====================================================
// HX711 - BALANCE
// =====================================================

#define HX711_DOUT 32
#define HX711_SCK  33

HX711 scale;

const float calibrationFactor = 413000.0;

float poids = 0.00;
bool balanceOK = false;

// Moyenne progressive:
// une lecture environ toutes les 200 ms
// 10 lectures = environ 2 secondes

float sommePoids = 0.0;
int nombreLecturesPoids = 0;

unsigned long previousWeightMillis = 0;
const unsigned long weightSampleInterval = 200;

const int nombreLecturesMoyenne = 10;

// Zone morte:
// tout poids inférieur à 50 g = 0.00 kg

const float seuilZeroPoids = 0.050;


// =====================================================
// RELAYS
// =====================================================

// Relay 1 = Ventilation
#define RELAY_FAN 18

// Relay 2 = Chauffage
#define RELAY_HEATER 19

// Relay 3 = Peltier
#define RELAY_PELTIER 21

// Relay 4 = Pompe
#define RELAY_PUMP 22

#define RELAY_ON LOW
#define RELAY_OFF HIGH

bool fanState = false;
bool heaterState = false;
bool peltierState = false;
bool pumpState = false;


// =====================================================
// WIFI
// =====================================================

const char* ssid = "NOM_DU_RESEAU";
const char* password = "MOT_DE_PASSE";   // identifiant retiré de la copie archivée

// =====================================================
// WEB SERVER
// =====================================================

WebServer server(80);

// =====================================================
// PREFERENCES
// =====================================================

Preferences preferences;

// =====================================================
// MODE
// =====================================================

bool autoMode = false;

// =====================================================
// CONFIGURATION
// =====================================================

float seuilChauffage = 22.0;
float seuilVentilation = 28.0;
float seuilPompeTemp = 30.0;
float seuilHumMin = 50.0;
float seuilPeltier = 32.0;

// Hystérésis
const float hystTemp = 0.5;
const float hystHum = 2.0;


// =====================================================
// DHT11 DATA
// =====================================================

float temperature = 26.0;
float humidite = 55.0;

bool capteurOK = false;
bool fallbackMode = false;

int dhtErrors = 0;
const int maxDhtErrors = 3;

unsigned long previousSensorMillis = 0;
const unsigned long sensorInterval = 2000;


// =====================================================
// AUTOMATIQUE
// =====================================================

unsigned long previousAutoMillis = 0;
const unsigned long autoInterval = 500;


// =====================================================
// ACTIONNEURS
// =====================================================

void setFan(bool state) {

  fanState = state;

  digitalWrite(
    RELAY_FAN,
    state ? RELAY_ON : RELAY_OFF
  );
}


void setHeater(bool state) {

  heaterState = state;

  digitalWrite(
    RELAY_HEATER,
    state ? RELAY_ON : RELAY_OFF
  );
}


void setPeltier(bool state) {

  peltierState = state;

  digitalWrite(
    RELAY_PELTIER,
    state ? RELAY_ON : RELAY_OFF
  );
}


void setPump(bool state) {

  pumpState = state;

  digitalWrite(
    RELAY_PUMP,
    state ? RELAY_ON : RELAY_OFF
  );
}


// =====================================================
// BALANCE - INITIALISATION
// =====================================================

void initialiserBalance() {

  Serial.println();
  Serial.println("==============================");
  Serial.println("INITIALISATION BALANCE");
  Serial.println("==============================");

  scale.begin(
    HX711_DOUT,
    HX711_SCK
  );

  Serial.println(
    "Laissez la plateforme VIDE..."
  );

  // Attendre que HX711 soit prêt
  unsigned long debut =
  millis();

  while(
    !scale.is_ready()
    &&
    millis() - debut < 5000
  ) {

    delay(100);

  }


  if(
    scale.is_ready()
  ) {

    Serial.println(
      "HX711 detecte"
    );

    scale.set_scale(
      calibrationFactor
    );

    Serial.println(
      "Tare en cours..."
    );

    // Plateforme plastique incluse
    // automatiquement dans le zéro

    scale.tare(30);

    poids = 0.00;

    sommePoids = 0.0;

    nombreLecturesPoids = 0;

    balanceOK = true;

    Serial.println(
      "Tare terminee"
    );

    Serial.println(
      "Balance operationnelle"
    );

  }

  else {

    balanceOK = false;

    poids = 0.00;

    Serial.println(
      "HX711 non detecte"
    );

  }

}


// =====================================================
// BALANCE - LECTURE
// =====================================================

void lireBalance() {

  if(
    balanceOK
    ==
    false
  ) {

    return;

  }


  unsigned long currentMillis =
  millis();


  if(
    currentMillis
    -
    previousWeightMillis
    <
    weightSampleInterval
  ) {

    return;

  }


  previousWeightMillis =
  currentMillis;


  // Ne pas bloquer le programme
  // si HX711 n'est momentanément pas prêt

  if(
    !scale.is_ready()
  ) {

    return;

  }


  float lecture =
  scale.get_units(1);


  // Vérification basique

  if(
    isnan(lecture)
    ||
    isinf(lecture)
  ) {

    return;

  }


  sommePoids +=
  lecture;


  nombreLecturesPoids++;


  // =====================================================
  // MOYENNE ENVIRON TOUTES LES 2 SECONDES
  // =====================================================

  if(
    nombreLecturesPoids
    >=
    nombreLecturesMoyenne
  ) {


    float moyenne =
    sommePoids
    /
    nombreLecturesPoids;


    // =====================================================
    // ZONE MORTE 50 g
    // =====================================================

    if(
      fabs(moyenne)
      <
      seuilZeroPoids
    ) {

      moyenne =
      0.00;

    }


    // Une valeur négative réelle
    // n'a pas de sens pour notre balance

    if(
      moyenne
      <
      0
    ) {

      moyenne =
      0.00;

    }


    poids =
    moyenne;


    Serial.print(
      "Poids moyen : "
    );

    Serial.print(
      poids,
      2
    );

    Serial.println(
      " kg"
    );


    // Réinitialiser la moyenne

    sommePoids =
    0.0;


    nombreLecturesPoids =
    0;

  }

}


// =====================================================
// WEB PAGE
// =====================================================

const char webpage[] PROGMEM = R"rawliteral(

<!DOCTYPE html>

<html lang="fr">

<head>

<meta charset="UTF-8">

<meta
name="viewport"
content="width=device-width, initial-scale=1.0">

<title>Poulailler Intelligent</title>

<style>

:root {

  --bg1:#07111f;
  --bg2:#0d1b2a;
  --bg3:#142438;

  --glass:rgba(255,255,255,0.075);
  --glass2:rgba(255,255,255,0.11);

  --border:rgba(255,255,255,0.12);

  --text:#f8fafc;
  --muted:#9fb0c5;

  --green:#22c55e;
  --greenSoft:rgba(34,197,94,0.13);

  --blue:#38bdf8;
  --blueSoft:rgba(56,189,248,0.15);

  --orange:#fb923c;
  --orangeSoft:rgba(251,146,60,0.15);

  --red:#ef4444;
  --redSoft:rgba(239,68,68,0.13);

  --purple:#a78bfa;
  --purpleSoft:rgba(167,139,250,0.15);

  --shadow:
  0 20px 50px rgba(0,0,0,0.30);

  --radius:24px;
}


* {

  box-sizing:border-box;

}


body {

  margin:0;

  min-height:100vh;

  font-family:
  Arial,
  Helvetica,
  sans-serif;

  color:var(--text);

  background:

  radial-gradient(
    circle at top right,
    #193454 0%,
    transparent 32%
  ),

  radial-gradient(
    circle at bottom left,
    #122b3d 0%,
    transparent 34%
  ),

  linear-gradient(
    145deg,
    var(--bg1),
    var(--bg2),
    var(--bg3)
  );

  background-attachment:fixed;

}


.glass {

  background:var(--glass);

  backdrop-filter:blur(18px);

  border:1px solid var(--border);

  box-shadow:var(--shadow);

}


/* HEADER */

.topbar {

  background:rgba(7,17,31,0.72);

  backdrop-filter:blur(18px);

  border-bottom:1px solid var(--border);

}


.topbar-inner {

  max-width:1050px;

  margin:auto;

  padding:21px 18px;

  display:flex;

  justify-content:space-between;

  align-items:center;

  flex-wrap:wrap;

  gap:15px;

}


.brand {

  display:flex;

  align-items:center;

  gap:14px;

}


.brand-logo {

  width:56px;
  height:56px;

  border-radius:18px;

  display:flex;

  justify-content:center;

  align-items:center;

  font-size:28px;

  background:
  linear-gradient(
    135deg,
    #2dd4bf,
    #0f766e
  );

}


.brand h1 {

  margin:0;

  font-size:27px;

}


.brand p {

  margin:6px 0 0;

  color:var(--muted);

  font-size:14px;

}


.online {

  display:flex;

  align-items:center;

  gap:8px;

  padding:10px 15px;

  border-radius:999px;

  background:rgba(34,197,94,0.16);

  color:#86efac;

  font-size:13px;

  font-weight:bold;

}


.online-dot {

  width:9px;
  height:9px;

  border-radius:50%;

  background:#22c55e;

  box-shadow:
  0 0 12px #22c55e;

}


/* CONTENT */

.container {

  max-width:1050px;

  margin:auto;

  padding:30px 18px 120px;

}


.section-title {

  margin:30px 0 17px;

}


.section-title h2 {

  margin:0;

  font-size:21px;

}


.section-title p {

  margin:6px 0 0;

  color:var(--muted);

  font-size:14px;

}


/* SUMMARY */

.summary-panel {

  border-radius:var(--radius);

  padding:25px;

}


.summary-heading {

  font-size:18px;

  font-weight:bold;

  margin-bottom:20px;

}


.summary-main {

  display:grid;

  grid-template-columns:
  repeat(3,1fr);

  gap:16px;

}


.summary-main-card {

  background:
  rgba(255,255,255,0.055);

  border:
  1px solid var(--border);

  border-radius:19px;

  padding:20px;

  display:flex;

  align-items:center;

  gap:15px;

}


.summary-icon {

  width:49px;
  height:49px;

  border-radius:15px;

  display:flex;

  justify-content:center;

  align-items:center;

  font-size:24px;

}


.summary-temp {

  background:var(--orangeSoft);

}


.summary-hum {

  background:var(--blueSoft);

}


.summary-air {

  background:
  rgba(34,197,94,0.15);

}


.summary-label {

  font-size:13px;

  color:var(--muted);

}


.summary-value {

  margin-top:5px;

  font-size:26px;

  font-weight:bold;

}


.air-good {

  margin-top:5px;

  color:#ffffff;

  font-size:26px;

  font-weight:bold;

}


/* ACTIONNEURS */

.action-title {

  margin-top:22px;

  margin-bottom:12px;

  font-size:13px;

  color:var(--muted);

  text-transform:uppercase;

  letter-spacing:1px;

}


.action-grid {

  display:grid;

  grid-template-columns:1fr 1fr;

  gap:12px;

}


.action-card {

  display:flex;

  justify-content:space-between;

  align-items:center;

  gap:12px;

  padding:15px 16px;

  border-radius:17px;

  background:
  rgba(255,255,255,0.045);

  border:
  1px solid var(--border);

}


.action-left {

  display:flex;

  align-items:center;

  gap:11px;

  font-size:14px;

  font-weight:bold;

}


.action-icon {

  width:37px;
  height:37px;

  display:flex;

  justify-content:center;

  align-items:center;

  border-radius:12px;

  background:
  rgba(255,255,255,0.06);

}


.badge {

  padding:7px 11px;

  border-radius:999px;

  font-size:12px;

  font-weight:bold;

}


.badge-off {

  background:
  rgba(148,163,184,0.12);

  color:#cbd5e1;

}


.badge-on {

  background:
  rgba(34,197,94,0.16);

  color:#86efac;

}


/* AMBIANCE */

.ambiance-grid {

  display:grid;

  grid-template-columns:1fr 1fr;

  gap:20px;

}


.ambiance-card {

  text-align:center;

  border-radius:var(--radius);

  padding:32px 20px;

}


.ambiance-icon {

  width:60px;
  height:60px;

  border-radius:19px;

  display:flex;

  justify-content:center;

  align-items:center;

  margin:0 auto 16px;

  font-size:29px;

}


.temp-icon {

  background:var(--orangeSoft);

}


.hum-icon {

  background:var(--blueSoft);

}


.ambiance-label {

  color:var(--muted);

  font-size:15px;

}


.ambiance-value {

  margin-top:10px;

  font-size:50px;

  font-weight:bold;

}


.unit {

  font-size:20px;

  color:var(--muted);

  font-weight:normal;

}


/* BOTTOM NAV */

.bottom-nav {

  position:fixed;

  z-index:1000;

  left:50%;

  bottom:18px;

  transform:
  translateX(-50%);

  display:grid;

  grid-template-columns:
  repeat(3,1fr);

  gap:8px;

  width:
  min(
    720px,
    calc(100% - 28px)
  );

  padding:8px;

  border-radius:22px;

  background:
  rgba(11,25,43,0.92);

  backdrop-filter:
  blur(20px);

  border:
  1px solid var(--border);

  box-shadow:
  0 18px 50px
  rgba(0,0,0,0.40);

}


.nav-button {

  border:none;

  min-height:54px;

  border-radius:16px;

  color:#e2e8f0;

  background:
  rgba(255,255,255,0.055);

  cursor:pointer;

  font-size:13px;

  font-weight:bold;

}


/* SUB PAGES */

.sub-page {

  position:fixed;

  inset:0;

  z-index:2000;

  display:none;

  overflow-y:auto;

  background:

  radial-gradient(
    circle at top right,
    #193454,
    transparent 35%
  ),

  linear-gradient(
    145deg,
    #07111f,
    #0d1b2a,
    #142438
  );

}


.sub-page.show {

  display:block;

}


.page-container {

  max-width:1050px;

  margin:auto;

  padding:27px 18px 50px;

}


.page-header {

  display:flex;

  justify-content:space-between;

  align-items:center;

  gap:15px;

  flex-wrap:wrap;

  margin-bottom:25px;

}


.page-heading h2 {

  margin:0;

  font-size:28px;

}


.page-heading p {

  margin:6px 0 0;

  color:var(--muted);

  font-size:14px;

}


.back-button {

  border:
  1px solid var(--border);

  background:
  var(--glass2);

  color:white;

  border-radius:14px;

  padding:12px 16px;

  font-weight:bold;

  cursor:pointer;

}


/* COMMAND CARDS */

.command-grid {

  display:grid;

  grid-template-columns:1fr 1fr;

  gap:20px;

}


.control-card {

  border-radius:24px;

  padding:24px;

}


.control-header {

  display:flex;

  justify-content:space-between;

  align-items:flex-start;

}


.control-icon {

  width:60px;
  height:60px;

  border-radius:19px;

  display:flex;

  justify-content:center;

  align-items:center;

  font-size:29px;

}


.fan-icon {

  background:var(--blueSoft);

}


.heat-icon {

  background:var(--orangeSoft);

}


.pump-icon {

  background:
  rgba(34,197,94,0.16);

}


.peltier-icon {

  background:
  rgba(56,189,248,0.13);

}


.active-label {

  padding:8px 12px;

  border-radius:999px;

  background:
  rgba(34,197,94,0.16);

  color:#86efac;

  font-size:12px;

  font-weight:bold;

}


.control-name {

  margin-top:18px;

  font-size:21px;

  font-weight:bold;

}


.control-description {

  margin-top:8px;

  color:var(--muted);

  line-height:1.6;

  font-size:14px;

}


/* MANUAL BUTTONS */

.manual-buttons {

  margin-top:22px;

  display:grid;

  grid-template-columns:
  1fr 1fr;

  gap:12px;

}


.manual-btn {

  border-radius:15px;

  padding:15px;

  font-size:14px;

  font-weight:bold;

  cursor:pointer;

}


.manual-start {

  background:
  rgba(34,197,94,0.08);

  border:
  1px solid
  rgba(34,197,94,0.22);

  color:#86efac;

}


.manual-stop {

  background:
  rgba(239,68,68,0.08);

  border:
  1px solid
  rgba(239,68,68,0.22);

  color:#fca5a5;

}


.manual-start.active {

  background:
  linear-gradient(
    135deg,
    #22c55e,
    #16a34a
  );

  border-color:#22c55e;

  color:white;

}


.manual-stop.active {

  background:
  linear-gradient(
    135deg,
    #ef4444,
    #dc2626
  );

  border-color:#ef4444;

  color:white;

}


/* CONFIGURATION */

.mode-panel {

  border-radius:22px;

  padding:22px;

  margin-bottom:20px;

}


.mode-title {

  font-size:17px;

  font-weight:bold;

  margin-bottom:15px;

}


.mode-buttons {

  display:grid;

  grid-template-columns:1fr 1fr;

  gap:12px;

}


.mode-btn {

  padding:15px;

  border-radius:15px;

  font-size:14px;

  font-weight:bold;

  cursor:pointer;

  border:
  1px solid var(--border);

  background:
  rgba(255,255,255,0.05);

  color:#cbd5e1;

}


.mode-btn.active {

  background:
  rgba(34,197,94,0.18);

  color:#86efac;

  border:
  1px solid
  rgba(34,197,94,0.40);

}


.config-grid {

  display:grid;

  grid-template-columns:
  1fr 1fr;

  gap:18px;

}


.config-card {

  border-radius:22px;

  padding:22px;

}


.config-top {

  display:flex;

  align-items:center;

  gap:13px;

}


.config-icon {

  width:48px;
  height:48px;

  border-radius:15px;

  display:flex;

  justify-content:center;

  align-items:center;

  font-size:23px;

  background:
  rgba(255,255,255,0.07);

}


.config-name {

  font-size:17px;

  font-weight:bold;

}


.config-description {

  margin-top:5px;

  color:var(--muted);

  font-size:13px;

}


.config-input-row {

  margin-top:20px;

  display:flex;

  align-items:center;

  gap:10px;

}


.config-input {

  width:100%;

  padding:14px;

  border-radius:14px;

  border:
  1px solid var(--border);

  outline:none;

  background:
  rgba(255,255,255,0.055);

  color:white;

  font-size:18px;

  font-weight:bold;

}


.input-unit {

  color:var(--muted);

  font-size:14px;

}


.save-button {

  margin-top:20px;

  width:100%;

  border:none;

  border-radius:16px;

  padding:16px;

  background:
  linear-gradient(
    135deg,
    #22c55e,
    #16a34a
  );

  color:white;

  font-size:15px;

  font-weight:bold;

  cursor:pointer;

}


.save-message {

  text-align:center;

  margin-top:14px;

  min-height:20px;

  color:#86efac;

  font-size:13px;

}


/* WEIGHT */

.weight-main {

  max-width:650px;

  margin:30px auto 0;

  border-radius:30px;

  padding:38px 25px;

  text-align:center;

}


.weight-icon {

  width:78px;
  height:78px;

  margin:0 auto 20px;

  border-radius:24px;

  display:flex;

  justify-content:center;

  align-items:center;

  font-size:38px;

  background:
  var(--purpleSoft);

}


.weight-label {

  color:var(--muted);

  font-size:15px;

}


.weight-value {

  margin-top:14px;

  font-size:68px;

  font-weight:bold;

}


.weight-unit {

  font-size:24px;

  color:var(--muted);

}


.weight-status {

  display:inline-block;

  margin-top:22px;

  padding:10px 16px;

  border-radius:999px;

  background:
  rgba(34,197,94,0.14);

  color:#86efac;

  font-size:13px;

  font-weight:bold;

}


.weight-help {

  max-width:650px;

  margin:18px auto 0;

  border-radius:20px;

  padding:20px;

  color:var(--muted);

  font-size:14px;

  line-height:1.7;

}


/* MOBILE */

@media(max-width:750px) {

  .summary-main,
  .action-grid,
  .ambiance-grid,
  .command-grid,
  .config-grid {

    grid-template-columns:1fr;

  }


  .brand h1 {

    font-size:22px;

  }


  .weight-value {

    font-size:52px;

  }


  .nav-button {

    font-size:11px;

  }

}

</style>

</head>


<body>


<!-- DASHBOARD -->

<div id="mainPage">


<div class="topbar">

<div class="topbar-inner">


<div class="brand">

<div class="brand-logo">

🐔

</div>


<div>

<h1>
Poulailler Intelligent
</h1>

<p>
Système de contrôle environnemental ESP32
</p>

</div>

</div>


<div class="online">

<span class="online-dot"></span>

ESP32 connecté

</div>


</div>

</div>


<div class="container">


<div class="summary-panel glass">


<div class="summary-heading">

Résumé du système

</div>


<div class="summary-main">


<div class="summary-main-card">

<div class="summary-icon summary-temp">
🌡️
</div>

<div>

<div class="summary-label">
Température
</div>

<div class="summary-value">
<span id="summaryTemp">--</span> °C
</div>

</div>

</div>


<div class="summary-main-card">

<div class="summary-icon summary-hum">
💧
</div>

<div>

<div class="summary-label">
Humidité
</div>

<div class="summary-value">
<span id="summaryHum">--</span> %
</div>

</div>

</div>


<div class="summary-main-card">

<div class="summary-icon summary-air">
🌿
</div>

<div>

<div class="summary-label">
Quality Air
</div>

<div class="air-good">
Good
</div>

</div>

</div>


</div>


<div class="action-title">

État des actionneurs

</div>


<div class="action-grid">


<div class="action-card">

<div class="action-left">
<div class="action-icon">💨</div>
Ventilation
</div>

<div
id="summaryFan"
class="badge badge-off">
OFF
</div>

</div>


<div class="action-card">

<div class="action-left">
<div class="action-icon">🔥</div>
Chauffage
</div>

<div
id="summaryHeat"
class="badge badge-off">
OFF
</div>

</div>


<div class="action-card">

<div class="action-left">
<div class="action-icon">💧</div>
Pompe d'eau
</div>

<div
id="summaryPump"
class="badge badge-off">
OFF
</div>

</div>


<div class="action-card">

<div class="action-left">
<div class="action-icon">❄️</div>
Peltier
</div>

<div
id="summaryPeltier"
class="badge badge-off">
OFF
</div>

</div>


</div>

</div>


<div class="section-title">

<h2>
Ambiance du bâtiment
</h2>

<p>
Paramètres environnementaux en temps réel
</p>

</div>


<div class="ambiance-grid">


<div class="ambiance-card glass">

<div class="ambiance-icon temp-icon">
🌡️
</div>

<div class="ambiance-label">
Température
</div>

<div class="ambiance-value">

<span id="temperature">--</span>

<span class="unit">
°C
</span>

</div>

</div>


<div class="ambiance-card glass">

<div class="ambiance-icon hum-icon">
💧
</div>

<div class="ambiance-label">
Humidité relative
</div>

<div class="ambiance-value">

<span id="humidite">--</span>

<span class="unit">
%
</span>

</div>

</div>


</div>


</div>


<!-- NAV -->

<div class="bottom-nav">


<button
class="nav-button"
onclick="openPage('manualPage')">

🎛️<br>
Commande manuelle

</button>


<button
class="nav-button"
onclick="openPage('configPage')">

⚙️<br>
Configuration

</button>


<button
class="nav-button"
onclick="openPage('weightPage')">

⚖️<br>
Pesée

</button>


</div>


</div>


<!-- =====================================================
     MANUAL PAGE
     ===================================================== -->

<div
id="manualPage"
class="sub-page">


<div class="page-container">


<div class="page-header">

<button
class="back-button"
onclick="closePage('manualPage')">
← Retour
</button>

<div class="page-heading">

<h2>
Commande manuelle
</h2>

<p>
Pilotage direct des équipements
</p>

</div>

</div>


<div class="command-grid">


<!-- VENTILATION -->

<div class="control-card glass">

<div class="control-header">

<div class="control-icon fan-icon">
💨
</div>

<div class="active-label">
OPÉRATIONNEL
</div>

</div>

<div class="control-name">
Ventilation
</div>

<div class="control-description">
Commande manuelle du système
de ventilation via Relay 1.
</div>

<div class="manual-buttons">

<button
id="fanStartButton"
class="manual-btn manual-start"
onclick="fanOn()">
▶ Démarrer
</button>

<button
id="fanStopButton"
class="manual-btn manual-stop"
onclick="fanOff()">
■ Arrêter
</button>

</div>

</div>


<!-- CHAUFFAGE -->

<div class="control-card glass">

<div class="control-header">

<div class="control-icon heat-icon">
🔥
</div>

<div class="active-label">
OPÉRATIONNEL
</div>

</div>

<div class="control-name">
Chauffage
</div>

<div class="control-description">
Commande manuelle du chauffage
via Relay 2.
</div>

<div class="manual-buttons">

<button
id="heaterStartButton"
class="manual-btn manual-start"
onclick="heaterOn()">
▶ Démarrer
</button>

<button
id="heaterStopButton"
class="manual-btn manual-stop"
onclick="heaterOff()">
■ Arrêter
</button>

</div>

</div>


<!-- POMPE -->

<div class="control-card glass">

<div class="control-header">

<div class="control-icon pump-icon">
💧
</div>

<div class="active-label">
OPÉRATIONNEL
</div>

</div>

<div class="control-name">
Pompe d'eau
</div>

<div class="control-description">
Commande manuelle de la pompe
via Relay 4.
</div>

<div class="manual-buttons">

<button
id="pumpStartButton"
class="manual-btn manual-start"
onclick="pumpOn()">
▶ Démarrer
</button>

<button
id="pumpStopButton"
class="manual-btn manual-stop"
onclick="pumpOff()">
■ Arrêter
</button>

</div>

</div>


<!-- PELTIER -->

<div class="control-card glass">

<div class="control-header">

<div class="control-icon peltier-icon">
❄️
</div>

<div class="active-label">
OPÉRATIONNEL
</div>

</div>

<div class="control-name">
Refroidissement Peltier
</div>

<div class="control-description">
Commande manuelle du module
TEC1-12705 via Relay 3.
</div>

<div class="manual-buttons">

<button
id="peltierStartButton"
class="manual-btn manual-start"
onclick="peltierOn()">
▶ Démarrer
</button>

<button
id="peltierStopButton"
class="manual-btn manual-stop"
onclick="peltierOff()">
■ Arrêter
</button>

</div>

</div>


</div>

</div>

</div>


<!-- =====================================================
     CONFIGURATION PAGE
     ===================================================== -->

<div
id="configPage"
class="sub-page">


<div class="page-container">


<div class="page-header">

<button
class="back-button"
onclick="closePage('configPage')">
← Retour
</button>

<div class="page-heading">

<h2>
Configuration
</h2>

<p>
Réglage des seuils du mode automatique
</p>

</div>

</div>


<div class="mode-panel glass">

<div class="mode-title">
Mode de fonctionnement
</div>

<div class="mode-buttons">

<button
id="manualModeButton"
class="mode-btn"
onclick="setManualMode()">

🎛️ MODE MANUEL

</button>

<button
id="autoModeButton"
class="mode-btn"
onclick="setAutoMode()">

🤖 MODE AUTOMATIQUE

</button>

</div>

</div>


<div class="config-grid">


<div class="config-card glass">

<div class="config-top">

<div class="config-icon">
🔥
</div>

<div>

<div class="config-name">
Seuil de chauffage
</div>

<div class="config-description">
Le chauffage démarre sous cette température.
</div>

</div>

</div>

<div class="config-input-row">

<input
id="cfgHeat"
class="config-input"
type="number"
step="0.1">

<span class="input-unit">
°C
</span>

</div>

</div>


<div class="config-card glass">

<div class="config-top">

<div class="config-icon">
💨
</div>

<div>

<div class="config-name">
Seuil de ventilation
</div>

<div class="config-description">
Les ventilateurs démarrent à partir de cette température.
</div>

</div>

</div>

<div class="config-input-row">

<input
id="cfgFan"
class="config-input"
type="number"
step="0.1">

<span class="input-unit">
°C
</span>

</div>

</div>


<div class="config-card glass">

<div class="config-top">

<div class="config-icon">
🌡️💧
</div>

<div>

<div class="config-name">
Pompe — Température
</div>

<div class="config-description">
La pompe démarre si la température dépasse ce seuil.
</div>

</div>

</div>

<div class="config-input-row">

<input
id="cfgPumpTemp"
class="config-input"
type="number"
step="0.1">

<span class="input-unit">
°C
</span>

</div>

</div>


<div class="config-card glass">

<div class="config-top">

<div class="config-icon">
💧
</div>

<div>

<div class="config-name">
Humidité minimale
</div>

<div class="config-description">
La pompe démarre si l'humidité descend sous ce seuil.
</div>

</div>

</div>

<div class="config-input-row">

<input
id="cfgHum"
class="config-input"
type="number"
step="1">

<span class="input-unit">
%
</span>

</div>

</div>


<div class="config-card glass">

<div class="config-top">

<div class="config-icon">
❄️
</div>

<div>

<div class="config-name">
Seuil Peltier
</div>

<div class="config-description">
Le refroidissement Peltier démarre à partir de cette température.
</div>

</div>

</div>

<div class="config-input-row">

<input
id="cfgPeltier"
class="config-input"
type="number"
step="0.1">

<span class="input-unit">
°C
</span>

</div>

</div>


</div>


<button
class="save-button"
onclick="saveConfiguration()">

💾 Enregistrer la configuration

</button>


<div
id="saveMessage"
class="save-message">
</div>


</div>

</div>


<!-- =====================================================
     WEIGHT PAGE
     ===================================================== -->

<div
id="weightPage"
class="sub-page">


<div class="page-container">


<div class="page-header">

<button
class="back-button"
onclick="closePage('weightPage')">
← Retour
</button>

<div class="page-heading">

<h2>
Pesée en temps réel
</h2>

<p>
Mesure du poids individuel de la volaille
</p>

</div>

</div>


<div class="weight-main glass">

<div class="weight-icon">
⚖️
</div>

<div class="weight-label">
Poids actuel
</div>

<div class="weight-value">

<span id="weightValue">
0.00
</span>

<span class="weight-unit">
kg
</span>

</div>


<div
id="weightStatus"
class="weight-status">

Initialisation...

</div>

</div>


<div class="weight-help glass">

Placez la volaille ou l'objet sur la plateforme.
La mesure est filtrée et mise à jour automatiquement.
Les très petites variations inférieures à 50 g sont considérées comme zéro.

</div>


</div>

</div>


<script>

// =====================================================
// NAVIGATION
// =====================================================

function openPage(id) {

  document
  .getElementById(id)
  .classList
  .add("show");

}


function closePage(id) {

  document
  .getElementById(id)
  .classList
  .remove("show");

}


// =====================================================
// BADGES
// =====================================================

function setBadge(id, state) {

  const element =
  document.getElementById(id);

  if(state === true) {

    element.innerHTML =
    "ON";

    element.className =
    "badge badge-on";

  }

  else {

    element.innerHTML =
    "OFF";

    element.className =
    "badge badge-off";

  }

}


// =====================================================
// MANUAL BUTTONS
// =====================================================

function setManualButtons(
  startId,
  stopId,
  state
) {

  const startButton =
  document.getElementById(startId);

  const stopButton =
  document.getElementById(stopId);

  if(state === true) {

    startButton
    .classList
    .add("active");

    stopButton
    .classList
    .remove("active");

  }

  else {

    startButton
    .classList
    .remove("active");

    stopButton
    .classList
    .add("active");

  }

}


// =====================================================
// MODE
// =====================================================

function updateModeButtons(autoMode) {

  const manual =
  document.getElementById(
    "manualModeButton"
  );

  const auto =
  document.getElementById(
    "autoModeButton"
  );

  manual.classList.remove("active");

  auto.classList.remove("active");

  if(autoMode === true) {

    auto.classList.add("active");

  }

  else {

    manual.classList.add("active");

  }

}


function setManualMode() {

  fetch("/mode/manual")
  .then(() => updateData());

}


function setAutoMode() {

  fetch("/mode/auto")
  .then(() => updateData());

}


// =====================================================
// SAVE CONFIG
// =====================================================

function saveConfiguration() {

  const heat =
  document.getElementById("cfgHeat").value;

  const fan =
  document.getElementById("cfgFan").value;

  const pumpTemp =
  document.getElementById("cfgPumpTemp").value;

  const hum =
  document.getElementById("cfgHum").value;

  const peltier =
  document.getElementById("cfgPeltier").value;


  const url =
  "/config/save"
  +
  "?heat=" + encodeURIComponent(heat)
  +
  "&fan=" + encodeURIComponent(fan)
  +
  "&pumpTemp=" + encodeURIComponent(pumpTemp)
  +
  "&hum=" + encodeURIComponent(hum)
  +
  "&peltier=" + encodeURIComponent(peltier);


  fetch(url)

  .then(response => response.text())

  .then(() => {

    document
    .getElementById("saveMessage")
    .innerHTML =
    "✓ Configuration enregistrée";

    setTimeout(
      () => {

        document
        .getElementById("saveMessage")
        .innerHTML =
        "";

      },
      2500
    );

    updateData();

  });

}


// =====================================================
// UPDATE DATA
// =====================================================

function updateData() {

fetch("/data")

.then(
  response =>
  response.json()
)

.then(
  data => {


    // Temperature

    document
    .getElementById("temperature")
    .innerHTML =
    data.temperature.toFixed(1);


    document
    .getElementById("summaryTemp")
    .innerHTML =
    data.temperature.toFixed(1);


    // Humidité

    document
    .getElementById("humidite")
    .innerHTML =
    data.humidite.toFixed(0);


    document
    .getElementById("summaryHum")
    .innerHTML =
    data.humidite.toFixed(0);


    // Actionneurs

    setBadge(
      "summaryFan",
      data.fan
    );


    setBadge(
      "summaryHeat",
      data.heater
    );


    setBadge(
      "summaryPump",
      data.pump
    );


    setBadge(
      "summaryPeltier",
      data.peltier
    );


    // Commande manuelle

    setManualButtons(
      "fanStartButton",
      "fanStopButton",
      data.fan
    );


    setManualButtons(
      "heaterStartButton",
      "heaterStopButton",
      data.heater
    );


    setManualButtons(
      "pumpStartButton",
      "pumpStopButton",
      data.pump
    );


    setManualButtons(
      "peltierStartButton",
      "peltierStopButton",
      data.peltier
    );


    // Mode

    updateModeButtons(
      data.autoMode
    );


    // Configuration

    if(
      document.activeElement.id
      !==
      "cfgHeat"
    ) {

      document
      .getElementById("cfgHeat")
      .value =
      data.cfgHeat.toFixed(1);

    }


    if(
      document.activeElement.id
      !==
      "cfgFan"
    ) {

      document
      .getElementById("cfgFan")
      .value =
      data.cfgFan.toFixed(1);

    }


    if(
      document.activeElement.id
      !==
      "cfgPumpTemp"
    ) {

      document
      .getElementById("cfgPumpTemp")
      .value =
      data.cfgPumpTemp.toFixed(1);

    }


    if(
      document.activeElement.id
      !==
      "cfgHum"
    ) {

      document
      .getElementById("cfgHum")
      .value =
      data.cfgHum.toFixed(0);

    }


    if(
      document.activeElement.id
      !==
      "cfgPeltier"
    ) {

      document
      .getElementById("cfgPeltier")
      .value =
      data.cfgPeltier.toFixed(1);

    }


    // =================================================
    // BALANCE
    // =================================================

    if(
      data.balance
      ===
      true
    ) {

      document
      .getElementById("weightValue")
      .innerHTML =
      data.weight.toFixed(2);


      document
      .getElementById("weightStatus")
      .innerHTML =
      "Balance opérationnelle";

    }

    else {

      document
      .getElementById("weightValue")
      .innerHTML =
      "0.00";


      document
      .getElementById("weightStatus")
      .innerHTML =
      "Balance non disponible";

    }


  }

)

.catch(

  error => {

    console.log(
      "Erreur communication ESP32"
    );

  }

);

}


// =====================================================
// MANUAL COMMANDS
// =====================================================

function fanOn() {

  fetch("/fan/on")
  .then(() => updateData());

}


function fanOff() {

  fetch("/fan/off")
  .then(() => updateData());

}


function heaterOn() {

  fetch("/heater/on")
  .then(() => updateData());

}


function heaterOff() {

  fetch("/heater/off")
  .then(() => updateData());

}


function pumpOn() {

  fetch("/pump/on")
  .then(() => updateData());

}


function pumpOff() {

  fetch("/pump/off")
  .then(() => updateData());

}


function peltierOn() {

  fetch("/peltier/on")
  .then(() => updateData());

}


function peltierOff() {

  fetch("/peltier/off")
  .then(() => updateData());

}


// =====================================================
// START
// =====================================================

updateData();

setInterval(
  updateData,
  1000
);

</script>


</body>

</html>

)rawliteral";


// =====================================================
// DHT11
// =====================================================

void lireDHT11() {

  if(
    fallbackMode
  ) {

    return;

  }


  float h =
  dht.readHumidity();


  float t =
  dht.readTemperature();


  if(
    isnan(h)
    ||
    isnan(t)
  ) {

    dhtErrors++;

    capteurOK =
    false;

    temperature =
    26.0;

    humidite =
    55.0;


    Serial.print(
      "Erreur lecture DHT11 : "
    );

    Serial.print(
      dhtErrors
    );

    Serial.print(
      "/"
    );

    Serial.println(
      maxDhtErrors
    );


    if(
      dhtErrors
      >=
      maxDhtErrors
    ) {

      fallbackMode =
      true;

      Serial.println(
        "DHT11 DESACTIVE"
      );

      Serial.println(
        "Temperature : 26.0 C"
      );

      Serial.println(
        "Humidite : 55 %"
      );

      Serial.println(
        "Les actionneurs restent operationnels."
      );

    }

    return;

  }


  temperature =
  t;


  humidite =
  h;


  capteurOK =
  true;


  dhtErrors =
  0;


  Serial.print(
    "Temperature : "
  );


  Serial.print(
    temperature
  );


  Serial.print(
    " C | Humidite : "
  );


  Serial.print(
    humidite
  );


  Serial.println(
    " %"
  );

}


// =====================================================
// AUTOMATIC CONTROL
// =====================================================

void controleAutomatique() {

  if(
    autoMode
    ==
    false
  ) {

    return;

  }


  // Chauffage

  if(
    temperature
    <=
    seuilChauffage
  ) {

    setHeater(true);

    setFan(false);

    setPeltier(false);

  }

  else if(
    temperature
    >=
    seuilChauffage
    +
    hystTemp
  ) {

    setHeater(false);

  }


  // Ventilation

  if(
    heaterState
    ==
    false
  ) {

    if(
      temperature
      >=
      seuilVentilation
    ) {

      setFan(true);

    }

    else if(
      temperature
      <=
      seuilVentilation
      -
      hystTemp
    ) {

      setFan(false);

    }

  }


  // Peltier

  if(
    heaterState
    ==
    false
  ) {

    if(
      temperature
      >=
      seuilPeltier
    ) {

      setPeltier(true);

    }

    else if(
      temperature
      <=
      seuilPeltier
      -
      hystTemp
    ) {

      setPeltier(false);

    }

  }


  // Pompe

  if(
    temperature
    >=
    seuilPompeTemp
    ||
    humidite
    <=
    seuilHumMin
  ) {

    setPump(true);

  }

  else if(
    temperature
    <=
    seuilPompeTemp
    -
    hystTemp
    &&
    humidite
    >=
    seuilHumMin
    +
    hystHum
  ) {

    setPump(false);

  }

}


// =====================================================
// ROOT
// =====================================================

void handleRoot() {

  server.send_P(
    200,
    "text/html",
    webpage
  );

}


// =====================================================
// DATA JSON
// =====================================================

void handleData() {

  String json =
  "{";


  json +=
  "\"temperature\":"
  +
  String(
    temperature,
    1
  );


  json +=
  ",\"humidite\":"
  +
  String(
    humidite,
    1
  );


  json +=
  ",\"fan\":"
  +
  String(
    fanState
    ?
    "true"
    :
    "false"
  );


  json +=
  ",\"heater\":"
  +
  String(
    heaterState
    ?
    "true"
    :
    "false"
  );


  json +=
  ",\"pump\":"
  +
  String(
    pumpState
    ?
    "true"
    :
    "false"
  );


  json +=
  ",\"peltier\":"
  +
  String(
    peltierState
    ?
    "true"
    :
    "false"
  );


  json +=
  ",\"autoMode\":"
  +
  String(
    autoMode
    ?
    "true"
    :
    "false"
  );


  json +=
  ",\"cfgHeat\":"
  +
  String(
    seuilChauffage,
    1
  );


  json +=
  ",\"cfgFan\":"
  +
  String(
    seuilVentilation,
    1
  );


  json +=
  ",\"cfgPumpTemp\":"
  +
  String(
    seuilPompeTemp,
    1
  );


  json +=
  ",\"cfgHum\":"
  +
  String(
    seuilHumMin,
    1
  );


  json +=
  ",\"cfgPeltier\":"
  +
  String(
    seuilPeltier,
    1
  );


  // =====================================================
  // POIDS
  // =====================================================

  json +=
  ",\"weight\":"
  +
  String(
    poids,
    2
  );


  json +=
  ",\"balance\":"
  +
  String(
    balanceOK
    ?
    "true"
    :
    "false"
  );


  json +=
  "}";


  server.send(
    200,
    "application/json",
    json
  );

}


// =====================================================
// MANUAL CONTROL AUTHORIZATION
// =====================================================

bool manualAllowed() {

  if(
    autoMode
  ) {

    server.send(
      409,
      "text/plain",
      "Mode automatique actif"
    );

    return false;

  }

  return true;

}


// =====================================================
// FAN
// =====================================================

void handleFanOn() {

  if(!manualAllowed()) return;

  setFan(true);

  Serial.println(
    "Ventilation : ON"
  );

  server.send(
    200,
    "text/plain",
    "OK"
  );

}


void handleFanOff() {

  if(!manualAllowed()) return;

  setFan(false);

  Serial.println(
    "Ventilation : OFF"
  );

  server.send(
    200,
    "text/plain",
    "OK"
  );

}


// =====================================================
// HEATER
// =====================================================

void handleHeaterOn() {

  if(!manualAllowed()) return;

  setHeater(true);

  Serial.println(
    "Chauffage : ON"
  );

  server.send(
    200,
    "text/plain",
    "OK"
  );

}


void handleHeaterOff() {

  if(!manualAllowed()) return;

  setHeater(false);

  Serial.println(
    "Chauffage : OFF"
  );

  server.send(
    200,
    "text/plain",
    "OK"
  );

}


// =====================================================
// PUMP
// =====================================================

void handlePumpOn() {

  if(!manualAllowed()) return;

  setPump(true);

  Serial.println(
    "Pompe : ON"
  );

  server.send(
    200,
    "text/plain",
    "OK"
  );

}


void handlePumpOff() {

  if(!manualAllowed()) return;

  setPump(false);

  Serial.println(
    "Pompe : OFF"
  );

  server.send(
    200,
    "text/plain",
    "OK"
  );

}


// =====================================================
// PELTIER
// =====================================================

void handlePeltierOn() {

  if(!manualAllowed()) return;

  setPeltier(true);

  Serial.println(
    "Peltier : ON"
  );

  server.send(
    200,
    "text/plain",
    "OK"
  );

}


void handlePeltierOff() {

  if(!manualAllowed()) return;

  setPeltier(false);

  Serial.println(
    "Peltier : OFF"
  );

  server.send(
    200,
    "text/plain",
    "OK"
  );

}


// =====================================================
// MODES
// =====================================================

void handleManualMode() {

  autoMode =
  false;


  preferences.putBool(
    "auto",
    false
  );


  Serial.println(
    "MODE MANUEL"
  );


  server.send(
    200,
    "text/plain",
    "MANUEL"
  );

}


void handleAutoMode() {

  autoMode =
  true;


  preferences.putBool(
    "auto",
    true
  );


  Serial.println(
    "MODE AUTOMATIQUE"
  );


  controleAutomatique();


  server.send(
    200,
    "text/plain",
    "AUTO"
  );

}


// =====================================================
// SAVE CONFIG
// =====================================================

void handleSaveConfig() {

  if(
    server.hasArg("heat")
  ) {

    seuilChauffage =
    server.arg("heat").toFloat();

  }


  if(
    server.hasArg("fan")
  ) {

    seuilVentilation =
    server.arg("fan").toFloat();

  }


  if(
    server.hasArg("pumpTemp")
  ) {

    seuilPompeTemp =
    server.arg("pumpTemp").toFloat();

  }


  if(
    server.hasArg("hum")
  ) {

    seuilHumMin =
    server.arg("hum").toFloat();

  }


  if(
    server.hasArg("peltier")
  ) {

    seuilPeltier =
    server.arg("peltier").toFloat();

  }


  preferences.putFloat(
    "heat",
    seuilChauffage
  );


  preferences.putFloat(
    "fan",
    seuilVentilation
  );


  preferences.putFloat(
    "pumpTemp",
    seuilPompeTemp
  );


  preferences.putFloat(
    "hum",
    seuilHumMin
  );


  preferences.putFloat(
    "peltier",
    seuilPeltier
  );


  Serial.println(
    "Configuration enregistree"
  );


  if(
    autoMode
  ) {

    controleAutomatique();

  }


  server.send(
    200,
    "text/plain",
    "OK"
  );

}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(
    115200
  );


  delay(
    1000
  );


  Serial.println();


  Serial.println(
    "================================"
  );


  Serial.println(
    "POULAILLER INTELLIGENT"
  );


  Serial.println(
    "================================"
  );


  // =====================================================
  // RELAYS
  // =====================================================

  pinMode(
    RELAY_FAN,
    OUTPUT
  );


  pinMode(
    RELAY_HEATER,
    OUTPUT
  );


  pinMode(
    RELAY_PELTIER,
    OUTPUT
  );


  pinMode(
    RELAY_PUMP,
    OUTPUT
  );


  setFan(false);
  setHeater(false);
  setPeltier(false);
  setPump(false);


  // =====================================================
  // MEMORY
  // =====================================================

  preferences.begin(
    "poulailler",
    false
  );


  seuilChauffage =
  preferences.getFloat(
    "heat",
    22.0
  );


  seuilVentilation =
  preferences.getFloat(
    "fan",
    28.0
  );


  seuilPompeTemp =
  preferences.getFloat(
    "pumpTemp",
    30.0
  );


  seuilHumMin =
  preferences.getFloat(
    "hum",
    50.0
  );


  seuilPeltier =
  preferences.getFloat(
    "peltier",
    32.0
  );


  autoMode =
  preferences.getBool(
    "auto",
    false
  );


  // =====================================================
  // BALANCE
  // =====================================================

  // IMPORTANT :
  // plateforme vide pendant cette étape.

  initialiserBalance();


  // =====================================================
  // DHT11
  // =====================================================

  dht.begin();


  temperature =
  26.0;


  humidite =
  55.0;


  fallbackMode =
  false;


  dhtErrors =
  0;


  delay(
    2000
  );


  lireDHT11();


  // =====================================================
  // WIFI
  // =====================================================

  WiFi.mode(
    WIFI_STA
  );


  WiFi.begin(
    ssid,
    password
  );


  Serial.print(
    "Connexion WiFi"
  );


  while(
    WiFi.status()
    !=
    WL_CONNECTED
  ) {

    delay(
      500
    );

    Serial.print(
      "."
    );

  }


  Serial.println();


  Serial.println(
    "WiFi connecte !"
  );


  Serial.print(
    "Adresse IP : "
  );


  Serial.println(
    WiFi.localIP()
  );


  // =====================================================
  // ROUTES
  // =====================================================

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );


  server.on(
    "/data",
    HTTP_GET,
    handleData
  );


  // Ventilation

  server.on(
    "/fan/on",
    HTTP_GET,
    handleFanOn
  );


  server.on(
    "/fan/off",
    HTTP_GET,
    handleFanOff
  );


  // Chauffage

  server.on(
    "/heater/on",
    HTTP_GET,
    handleHeaterOn
  );


  server.on(
    "/heater/off",
    HTTP_GET,
    handleHeaterOff
  );


  // Pompe

  server.on(
    "/pump/on",
    HTTP_GET,
    handlePumpOn
  );


  server.on(
    "/pump/off",
    HTTP_GET,
    handlePumpOff
  );


  // Peltier

  server.on(
    "/peltier/on",
    HTTP_GET,
    handlePeltierOn
  );


  server.on(
    "/peltier/off",
    HTTP_GET,
    handlePeltierOff
  );


  // Modes

  server.on(
    "/mode/manual",
    HTTP_GET,
    handleManualMode
  );


  server.on(
    "/mode/auto",
    HTTP_GET,
    handleAutoMode
  );


  // Configuration

  server.on(
    "/config/save",
    HTTP_GET,
    handleSaveConfig
  );


  // =====================================================
  // SERVER
  // =====================================================

  server.begin();


  Serial.println(
    "Serveur Web demarre"
  );


  if(
    autoMode
  ) {

    controleAutomatique();

  }

}


// =====================================================
// LOOP
// =====================================================

void loop() {

  server.handleClient();


  unsigned long currentMillis =
  millis();


  // =====================================================
  // BALANCE
  // =====================================================

  lireBalance();


  // =====================================================
  // DHT11
  // =====================================================

  if(
    fallbackMode
    ==
    false
  ) {

    if(
      currentMillis
      -
      previousSensorMillis
      >=
      sensorInterval
    ) {

      previousSensorMillis =
      currentMillis;

      lireDHT11();

    }

  }


  // =====================================================
  // AUTOMATIC CONTROL
  // =====================================================

  if(
    autoMode
    &&
    currentMillis
    -
    previousAutoMillis
    >=
    autoInterval
  ) {

    previousAutoMillis =
    currentMillis;

    controleAutomatique();

  }

}