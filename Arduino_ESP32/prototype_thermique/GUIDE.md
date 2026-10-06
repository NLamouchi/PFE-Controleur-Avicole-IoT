# Guide du prototype thermique — ESP32, chauffage, ventilateur, pad-cooling

Objectif : montrer qu'une seule ESP32 pilote ensemble le chauffage, le ventilateur et le
pad-cooling de la maquette, à partir de la température mesurée.

## 1. Composants

### Déjà achetés (photo du 18/09)

| Composant | Rôle | Statut |
|---|---|---|
| ESP32 DevKit V1, 30 broches, micro-USB | Unité centrale | ✅ utilisé |
| Module 4 relais 5 V, Songle SRD-05VDC-SL-C (7 A / 28 V DC résistif, 3 A inductif), cavalier JD-VCC | Commutation des trois charges 24 V | ✅ utilisé |
| Convertisseur abaisseur réglable (condensateurs 220 µF 35 V et 100 µF 50 V) | 24 V → 5 V pour les relais et l'ESP32 | ✅ utilisé |
| Petit module violet I2C à 6 broches (BME280 ou BMP280) | Température (et humidité si BME280) | ✅ utilisé — le programme affiche lequel |
| Barrettes mâles 2,54 mm | À souder sur le module violet | ✅ utilisé |
| 2 fusibles céramique | Protection | ⚠ calibre à vérifier, porte-fusible manquant |
| GY-521 (MPU-6050) | Accéléromètre / gyroscope | ✖ sans rapport avec la thermique |
| MQ-7 | Capteur de monoxyde de carbone (CO), pas de CO2 | ✖ ne pas brancher (sortie jusqu'à 5 V) |

### À acheter

| # | Composant | Qté | Pourquoi |
|---|---|---|---|
| 1 | Alimentation 24 V DC : **10,4 A (250 W) pour la CTP de 50 W retenue** (6 A suffisent pour une CTP de 30 W) ; si c'est un module à bornes à vis, raccordement au 230 V par un cordon avec terre, bornes sous capot, vérifié par une personne habilitée | 1 | Doit fournir le courant d'appel de la CTP à froid : environ 3 fois son courant nominal (DBK : 7,5 A pour 60 W sous 24 V) |
| 2 | Résistance chauffante CTP 24 V 50 W avec ventilateur ; demander son courant d'appel au vendeur | 1 | Chauffage (besoin calculé : 39 W) |
| 3 | Thermostat bilame KSD301 normalement fermé 60 °C | 1 | Coupe le chauffage en cas de surchauffe, même si le programme plante |
| 4 | Ventilateur 24 V DC, 40 à 60 mm | 1 (2 pour utiliser IN4) | Ventilation / extraction |
| 5 | Mini-pompe à eau immergée 24 V DC + 1 m de tube silicone au diamètre de sa sortie | 1 | Arrosage du pad |
| 6 | Morceau de pad-cooling, gouttière, boîte plastique d'environ 1 L comme réservoir | 1 | Pad-cooling (chute de pad à demander à M. Fayçal) |
| 7 | Diode 1N4007 | 2 | Protection du relais contre la surtension de la pompe |
| 8 | Porte-fusible en ligne 5×20 + fusibles temporisés T4 A (CTP 30 W) ou T6,3 A (CTP 50 W) | 1 + 2 | Protection de la ligne 24 V |
| 9 | Plaque d'essai 830 points + fils Dupont mâle-mâle et mâle-femelle | 1 + 1 lot | Câblage sans soudure |
| 10 | Câble 2 × 0,75 mm² ou 2 × 1 mm² | 3 m | Liaisons 24 V |
| 11 | Câble micro-USB **de données** (si vous n'en avez pas) | 1 | Programmation |

Outils : multimètre (obligatoire pour régler le 5 V), fer à souder et étain, petit tournevis.

## 2. Schéma de branchement

### Puissance 24 V (tout éteint pendant le câblage)

```
Bloc 24 V (+) ──[Fusible T]─────┬──────────────────────────► Convertisseur IN+
                                ├──► COM relais 1 ── NO1 ──► KSD301 ──► CTP (+)
                                ├──► COM relais 2 ── NO2 ──► Ventilateur (fil rouge)
                                └──► COM relais 3 ── NO3 ──► Pompe (+)

Bloc 24 V (−) ──────────────────┬──────────────────────────► Convertisseur IN−
                                ├──────────────────────────── CTP (−)
                                ├──────────────────────────── Ventilateur (fil noir)
                                └──────────────────────────── Pompe (−)

Diode 1N4007 sur le bornier de sortie, hors de la maquette, entre NO3 et le 0 V : bague (cathode)
côté NO3. Soudures sous gaine thermorétractable, aucune connexion nue près de l'eau.
Les bornes NC des relais ne sont pas utilisées.
```

Si la CTP a 4 fils (résistance et ventilateur séparés) : les 2 fils du ventilateur de la CTP
vont directement sur NO1 et le 0 V ; seuls les 2 fils de la résistance passent par le KSD301.

### Commande 5 V / 3,3 V

```
Convertisseur OUT+ (réglé à 5,0 V) ──► Module relais VCC   (cavalier JD-VCC laissé en place)
Convertisseur OUT− ──────────┬───────► Module relais GND
                             └───────► ESP32 GND           (masse commune obligatoire)

ESP32 D26 ─────────────────────────► IN1  chauffage
ESP32 D27 ─────────────────────────► IN2  ventilateur
ESP32 D25 ─────────────────────────► IN3  pompe du pad
                                      IN4  libre

ESP32 3V3 ────┬────────────────────► Capteur VCC
              └────────────────────► Capteur CSB   (mode I2C)
ESP32 GND ────┬────────────────────► Capteur GND
              └────────────────────► Capteur SDO   (adresse 0x76)
ESP32 D21 ─────────────────────────► Capteur SDA
ESP32 D22 ─────────────────────────► Capteur SCL

ESP32 USB ─────────────────────────► PC (programmation et affichage)
```

Alimentation de l'ESP32 :

- pendant les essais, l'ESP32 est alimentée **uniquement par l'USB du PC** ; la broche VIN n'est pas branchée ;
- en fonctionnement autonome, relier VIN au +5 V du convertisseur et **débrancher l'USB** ;
- jamais l'USB et VIN en même temps.

Le capteur violet se branche **au 3V3 seulement** : la version la plus courante n'a pas de régulateur.

### Tableau des broches

| Broche ESP32 | Relié à | Type |
|---|---|---|
| D21 | Capteur SDA | I2C |
| D22 | Capteur SCL | I2C |
| D26 | Relais IN1 — chauffage | Sortie (PWM lent logiciel) |
| D27 | Relais IN2 — ventilateur | Sortie tout-ou-rien |
| D25 | Relais IN3 — pompe | Sortie tout-ou-rien |
| 3V3 | Capteur VCC et CSB | Alimentation 3,3 V |
| GND | Capteur GND et SDO, module relais GND, convertisseur OUT− | Masse commune |
| VIN | +5 V du convertisseur (mode autonome seulement) | Alimentation 5 V |

Le module relais est **actif à l'état bas** : le relais colle quand la broche est à LOW.

## 3. Logique de commande

| Mode | Condition (consigne 26 °C par défaut) | Chauffage | Ventilateur | Pompe |
|---|---|---|---|---|
| Chauffage | T < consigne | proportionnel : 100 % à consigne − 2 °C | arrêt | arrêt |
| Neutre | consigne ≤ T < consigne + 2 °C | arrêt | arrêt | arrêt |
| Ventilation | T ≥ consigne + 2 °C | arrêt | marche | arrêt |
| Refroidissement | T ≥ consigne + 4 °C | arrêt | marche | marche (si HR < 85 %) |
| Défaut capteur | 3 lectures ratées | arrêt | arrêt | arrêt |

- Chaque seuil a une hystérésis de 0,5 °C, pour que les relais ne battent pas.
- Le chauffage n'est jamais actif en même temps que le ventilateur ou la pompe.
- **PWM :** un relais ne supporte pas un PWM rapide. La puissance de chauffe est donc modulée
  par un PWM lent sur une période de 20 s. Par exemple, à 40 %, le relais reste collé 8 s puis
  décolle 12 s.
- Faire varier la vitesse du ventilateur demanderait un module MOSFET à la place d'un relais.
  Ce n'est pas nécessaire pour ce prototype.

## 4. Programme

Fichier : `prototype_thermique.ino` (dans ce dossier).

1. Installer Arduino IDE 2.
2. Gestionnaire de cartes : installer « esp32 » d'Espressif, puis choisir « DOIT ESP32 DEVKIT V1 ».
3. Gestionnaire de bibliothèques : installer « Adafruit BME280 Library » et
   « Adafruit BMP280 Library », en acceptant leurs dépendances.
4. Ouvrir le fichier, téléverser, puis ouvrir le moniteur série à **115200** bauds.
5. Pour changer la consigne, taper un nombre (par exemple `30`) puis Entrée.

## 5. Montage pas à pas

1. **Préparer.**
   - Souder une barrette de 6 broches sur le module violet.
   - Vérifier le calibre des fusibles : T4 A avec une CTP de 30 W, T6,3 A avec une CTP de 50 W.
   - Repérer au multimètre les bornes de chaque relais : au repos, COM et NC sont reliés, COM et NO sont ouverts.
2. **Capteur seul.**
   - Placer l'ESP32 et le capteur sur la plaque d'essai et les brancher.
   - Téléverser le programme. Le moniteur doit afficher « Capteur BME280 trouvé » (ou BMP280) et une température plausible.
   - Serrer le capteur entre deux doigts : la température monte.
3. **Régler le convertisseur.**
   - Brancher le bloc 24 V sur IN+ et IN− **à travers le porte-fusible**, sans rien en sortie.
   - Tourner la vis du potentiomètre jusqu'à lire **5,0 V** au multimètre sur OUT+ et OUT−.
   - Ne rien brancher en sortie avant ce réglage.
4. **Module relais, sans charge.**
   - **Débrancher le bloc 24 V**, câbler VCC, GND et IN1 à IN3 comme sur le schéma, avec la masse
     commune, puis rebrancher. Attention : un fil venu du 5 V qui touche la ligne 3V3 détruit le capteur.
   - Taper `35` : mode Chauffage à 100 %, le relais 1 reste collé.
   - Pour voir le PWM lent, taper une consigne 1 °C au-dessus de la température affichée : le relais 1
     colle 10 s puis décolle 10 s.
   - Taper `15` : mode Refroidissement directement. Le relais 1 décolle, le relais 2 colle, et le
     relais 3 colle si l'humidité est sous 80 %.
5. **Câbler la puissance 24 V**, bloc débranché :
   - fusible, puis +24 V sur les trois COM ;
   - chaque NO vers sa charge ;
   - toutes les charges reviennent au 0 V ;
   - diode 1N4007 sur la pompe, KSD301 en série avec la CTP.
6. **Installer dans la maquette.**
   - **Pad :** sur la paroi d'entrée d'air. Le tube percé est posé sur le dessus du pad, la
     gouttière en dessous, et l'eau retourne dans le réservoir où plonge la pompe.
   - **Ventilateur :** sur la paroi opposée, en extraction (il souffle vers l'extérieur) : l'air
     entre ainsi à travers le pad mouillé.
   - **Chauffage :** au centre, près du sol, sur une équerre métallique vissée sur une plaque non
     combustible (tôle ou carreau) si le fond est en bois ou en polystyrène.
     - Au moins 5 cm de tout bois ou polystyrène.
     - Rien au-dessus de la sortie d'air à moins de 10 cm.
     - Relever la température maximale de surface sur la fiche de la CTP.
     - Fixer le KSD301 à quelques centimètres de la CTP, pas contre ses ailettes.
   - **Capteur :** au centre, à environ 10 cm du sol, hors du souffle direct du chauffage et du pad.
   - **Électronique :** hors de la maquette, au-dessus du niveau de l'eau.
7. **Essais.**
   - Remplir le réservoir ; la pompe ne doit jamais tourner à sec.
   - Premier chauffage à froid : garder le multimètre sur la sortie du convertisseur. Elle doit rester à
     5 V ; sinon, le bloc 24 V est trop faible pour le courant d'appel de la CTP.
   - Consigne au-dessus de l'ambiante (par exemple `30`) : chauffage, et la température monte.
   - Consigne sous l'ambiante (par exemple `20`) : ventilation puis pad-cooling, et l'eau coule sur le pad.
   - Débrancher le fil SDA : « DEFAUT CAPTEUR » s'affiche au bout de 6 s et tout s'arrête.
   - Copier le texte du moniteur série : il servira à tracer les courbes du chapitre 4.

## 6. Sécurité

- Côté 230 V : un bloc secteur fermé évite tout câblage ; avec un module à bornes à vis, cordon avec terre,
  bornes sous capot, et vérification par une personne habilitée avant la première mise sous tension.
- Le fusible est placé juste après le bloc 24 V.
- Le KSD301 coupe la CTP indépendamment du programme.
- L'eau reste loin de l'électronique.
- Ne jamais toucher les bornes pendant que le bloc est branché.
