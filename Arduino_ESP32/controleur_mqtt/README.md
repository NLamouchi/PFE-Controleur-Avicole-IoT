# Contrôleur ESP32 — maquette avicole (application web + Node-RED en arrière-plan)

- Programme ESP32 : `controleur_mqtt.ino`.
- Interface de conduite : **application web** `../../Application_Web_PWA/` (HTML, CSS, JavaScript,
  bibliothèque Paho en copie locale), servie par Python sur le port 8000.
- Flux Node-RED (arrière-plan : historique CSV et alerte sanitaire) : `../../Node-RED_Supervision/flux_avicole.json`.
- Communication : MQTT. Le broker est **Mosquitto** : port 1883 pour l'ESP32 et Node-RED, port 9001
  (MQTT sur WebSocket) pour le navigateur. Configurations : `../../Node-RED_Supervision/mosquitto.conf` (Linux)
  et `../../Application_Web_PWA/mosquitto_windows.conf` (Windows).

```
ESP32 ──Wi-Fi──► PC : Mosquitto 1883 ◄──── Node-RED (historique CSV, alerte sanitaire)
  ▲                       │ 9001 (WebSocket)
  │                       ▼
  └── commandes ──── application web http://<IP du PC>:8000 (téléphone ou PC sur le même Wi-Fi)
```

L'ESP32 régule seule. L'application sert à surveiller et à commander ; Node-RED n'envoie aucune
commande. Si le PC ou le Wi-Fi s'arrête, la régulation continue en automatique.

## 1. Matériel et câblage

| Élément | Référence | Alimentation |
|---|---|---|
| Carte | ESP32 DevKit V1, 30 broches | USB du PC (essais) ou VIN 5 V (autonome) — jamais les deux |
| Capteur | Module violet BME280 (T + HR) ou BMP280 (T seule), I2C | 3,3 V de l'ESP32 |
| Relais | Module 4 relais Songle SRD-05VDC-SL-C : 7 A / 28 V DC résistif, 3 A / 28 V DC inductif ; bobine 5 V, 71 mA ; actif à l'état bas | 5 V du convertisseur, cavalier JD-VCC en place |
| Convertisseur | Abaisseur réglable (type LM2596), **réglé à 5,0 V avant tout branchement** | 24 V |
| Chauffage | Résistance CTP 24 V avec ventilateur (30 ou 50 W) + thermostat KSD301 NF 60 °C en série | 24 V par relais 1 |
| Ventilateur | 24 V DC, 40 à 60 mm, monté en extraction | 24 V par relais 2 |
| Pompe du pad | Mini-pompe immergée 24 V DC + diode 1N4007 (hors de l'eau, voir ci-dessous) | 24 V par relais 3 |
| Alimentation | Bloc secteur 24 V DC dimensionné sur le courant d'appel de la CTP (tableau ci-dessous) + fusible temporisé en sortie | secteur |

**Dimensionner le bloc 24 V sur le courant d'appel de la CTP.** À froid, une CTP appelle nettement plus
que son courant nominal : le fabricant DBK indique 7,5 A au maximum pour une CTP de 60 W sous 24 V, soit
trois fois le courant nominal. Si l'alimentation ne peut pas fournir ce courant, le 24 V s'effondre, les
relais décollent et l'ESP32 redémarre. Demander le courant d'appel au vendeur de la CTP ; à défaut :

| CTP | Courant nominal | Courant d'appel estimé | Alimentation 24 V | Fusible |
|---|---|---|---|---|
| 30 W | 1,25 A | ≈ 4 A | 6 A (150 W) | T4 A |
| 50 W (retenue) | 2,1 A | ≈ 6 à 7 A | 10,4 A (250 W) | T6,3 A |

L'alimentation 24 V – 10,4 A proposée localement est un module à bornes à vis : son raccordement au
230 V se fait par un cordon avec terre, bornes sous capot, vérifié par une personne habilitée.

Au premier chauffage à froid, garder le multimètre sur la sortie du convertisseur : elle doit rester à 5 V.

### Broches de l'ESP32

| Broche | Relié à | Rôle |
|---|---|---|
| D21 | Capteur SDA | I2C données |
| D22 | Capteur SCL | I2C horloge |
| D26 | Relais IN1 | Chauffage (PWM lent logiciel, période 20 s) |
| D27 | Relais IN2 | Ventilateur (tout-ou-rien) |
| D25 | Relais IN3 | Pompe du pad-cooling (tout-ou-rien) |
| 3V3 | Capteur VCC et CSB | Alimentation du capteur, mode I2C |
| GND | Capteur GND et SDO, relais GND, convertisseur OUT− | Masse commune (SDO à GND : adresse 0x76) |
| VIN | Convertisseur OUT+ (5 V) | Seulement en autonome, USB débranché |

### Câblage de puissance (24 V)

```
Bloc 24 V (+) ──[fusible]──┬──► convertisseur IN+
                           ├──► COM1 ── NO1 ──► KSD301 ──► CTP (+)
                           ├──► COM2 ── NO2 ──► ventilateur (+ rouge)
                           └──► COM3 ── NO3 ──► pompe (+)

Bloc 24 V (−) ─────────────► convertisseur IN−, CTP (−), ventilateur (− noir), pompe (−)

Convertisseur OUT+ (5,0 V) ──► relais VCC
Convertisseur OUT− ──────────► relais GND et ESP32 GND
```

**Diode 1N4007 de la pompe.** La placer sur le bornier de sortie, hors de la maquette, entre NO3 et
le 0 V : bague (cathode) côté NO3. Protéger les soudures avec de la gaine thermorétractable. Aucune
connexion nue près de l'eau.

**Isolation.** Avec le cavalier JD-VCC en place et la masse commune, la logique et le 24 V ne sont pas
isolés galvaniquement. Cela convient en très basse tension, mais la maquette ne doit pas être présentée
comme une démonstration de l'isolation galvanique du chapitre 2.

Le guide de montage pas à pas est dans `../prototype_thermique/GUIDE.md` (même matériel). Ce guide
change la consigne en la tapant au moniteur série : cela ne fonctionne qu'avec `prototype_thermique.ino`.
Avec `controleur_mqtt.ino`, la consigne se règle avec le curseur de l'application web.

## 2. Mise en service

### Mosquitto (broker MQTT, sur le PC)

1. Installer Mosquitto depuis mosquitto.org (Windows) ou `sudo apt install mosquitto mosquitto-clients`
   (Debian/Ubuntu).
2. Lancer le broker avec la configuration du projet. Sous Windows, le plus simple est
   `application_web/Lancer_Poulailler.bat` (Mosquitto avec `mosquitto_windows.conf` + serveur web
   Python sur le port 8000 ; corriger dans le fichier les chemins de `mosquitto.exe` et de `python.exe`
   s'ils diffèrent). Sous Linux : `mosquitto -c node-red/mosquitto.conf -v`.
   Sans fichier de configuration, Mosquitto 2.x n'écoute que la boucle locale et l'ESP32 ne peut
   pas s'y connecter.
3. Vérifier le transport, indépendamment de Node-RED :
   `mosquitto_sub -h localhost -t "avicole/#" -v`.

> Cette configuration n'est ni authentifiée ni chiffrée : elle ne convient qu'à un réseau local
> fermé, sans redirection de port vers l'extérieur. Voir la dernière section de `mosquitto.conf`.

### Node-RED (sur le PC, en arrière-plan)

1. Installer Node.js LTS depuis nodejs.org.
2. Dans un terminal : `npm install -g --unsafe-perm node-red`, puis lancer `node-red`.
3. Ouvrir l'éditeur http://localhost:1880 (réservé à la mise au point). Menu ☰ → *Import* → choisir
   `flux_avicole.json` → *Import*, puis *Deploy*. Aucune extension n'est nécessaire.
4. Pour que l'historique du lot (module d'alerte) survive à un redémarrage de Node-RED, activer le
   stockage persistant du contexte dans `settings.js` (dossier `.node-red` de l'utilisateur) :
   `contextStorage: { default: { module: "localfilesystem" } }`. Sans ce réglage, il est gardé en mémoire.
5. Dans un terminal, `ipconfig` : noter l'adresse IPv4 du PC (par exemple 192.168.1.100). Sur un
   partage de connexion d'iPhone, elle est en général de la forme 172.20.10.x.

### ESP32

1. IDE Arduino 2. Gestionnaire de cartes : installer « esp32 » (Espressif), puis choisir « DOIT ESP32 DEVKIT V1 ».
2. Gestionnaire de bibliothèques : installer `PubSubClient` (Nick O'Leary), `Adafruit BME280 Library` et
   `Adafruit BMP280 Library`.
3. En haut de `controleur_mqtt.ino`, renseigner `WIFI_SSID`, `WIFI_MDP` et `MQTT_HOTE` (l'IPv4 du PC).
   L'ESP32 ne fonctionne qu'en Wi-Fi 2,4 GHz.
4. Téléverser, puis ouvrir le moniteur série à 115200 bauds. Une ligne JSON s'affiche toutes les 2 s
   dès le démarrage. La ligne `MQTT connecté` apparaît au bout d'environ 10 s.

### Application web

- Sur le PC : http://localhost:8000.
- Sur un téléphone connecté au même Wi-Fi : `http://<IPv4 du PC>:8000`. Au premier lancement,
  Windows demande d'autoriser Python et Mosquitto sur le réseau : accepter (réseau privé).

Contenu de l'application :

- **État de la liaison** : broker joint ou non, ESP32 en ligne ou hors ligne.
- **Ambiance** : température, humidité, mode en cours, âge de la dernière mesure.
- **Consigne** : curseur de 15 à 35 °C, publié au relâchement.
- **Commandes** : interrupteur Automatique ; interrupteurs Chauffage, Ventilateur et Pompe, qui suivent
  l'état réel des relais et n'agissent qu'en manuel.
- **Alerte sanitaire** : voyant publié par Node-RED sur `avicole/alerte`.
- **Réglages et journal** : adresse et port du broker, événements de connexion.

## 3. Messages MQTT

| Sujet | Sens | Contenu |
|---|---|---|
| `avicole/etat` | ESP32 → application et Node-RED | `{"t":28.3,"hr":55.0,"consigne":26.0,"mode":"VENTILATION","auto":true,"chauffage":0,"puissance":0,"ventilateur":1,"pompe":0}` |
| `avicole/statut` | ESP32 → application et Node-RED | `en ligne` ou `hors ligne` (message retenu, envoyé par le broker si l'ESP32 disparaît) |
| `avicole/cmd/consigne` | application → ESP32 | `15` à `35` |
| `avicole/cmd/auto` | application → ESP32 | `1` automatique, `0` manuel |
| `avicole/cmd/chauffage` | application → ESP32 | `1` / `0` (manuel seulement) |
| `avicole/cmd/ventilateur` | application → ESP32 | `1` / `0` (manuel seulement) |
| `avicole/cmd/pompe` | application → ESP32 | `1` / `0` (manuel seulement) |
| `avicole/lot/jour` | données du lot → Node-RED | `{"jour":24,"eau":118.5,"poids":1.62,"stress":false}` |
| `avicole/lot/nouveau` | données du lot → Node-RED | début d'un lot : remet l'historique du module d'alerte à zéro |
| `avicole/alerte` | Node-RED → application | voyant du jour en JSON (message retenu) |

Démonstration du module d'alerte (données synthétiques) : `python node-red/rejouer_scenario.py S2`.

`hr` vaut `null` avec un BMP280, et `t` vaut `null` tant qu'aucune mesure n'a réussi.

**Enregistrement des essais.** Le flux écrit chaque message d'état dans le fichier
`mesures_maquette.csv`, chaque changement de statut dans `liaison_maquette.csv` et chaque voyant
dans `alertes_sanitaires.csv`, créés dans le dossier depuis lequel Node-RED a été lancé. Chaque ligne a le format :
horodatage UTC ; t ; hr ; consigne ; mode ; auto ; chauffage ; puissance ; ventilateur ; pompe.
Il suffit d'ouvrir ce fichier dans un tableur pour tracer les courbes des essais.

## 4. Règles de sécurité codées dans l'ESP32

- Le chauffage n'est jamais actif en même temps que le ventilateur ou la pompe, en automatique comme en manuel.
- Capteur en défaut (3 lectures ratées) : tout s'arrête en automatique, et le chauffage manuel est coupé.
- Après une micro-coupure, le capteur repasse en veille et renverrait une température figée : le
  programme le détecte, écarte la mesure et réinitialise le capteur.
- En manuel, le chauffage est coupé au-delà de 40 °C. Il faut le relancer depuis l'application web.
- Perte de la liaison MQTT : retour automatique en mode automatique.
- Au démarrage, tous les relais sont au repos et le mode est automatique.
- Le thermostat KSD301 coupe la CTP même si le programme est planté.
