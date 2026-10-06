/* =============================================================================
   Poulailler intelligent : application web de supervision (MQTT sur WebSockets)
   PFE Mohamed Nadim LAMOUCHI, Polytech Intl / STE TAADIC

   Sujets (identiques au firmware firmware/controleur_mqtt/controleur_mqtt.ino) :
     avicole/etat           ESP32 -> appli   JSON toutes les 2 s et après chaque commande
     avicole/statut         ESP32 -> appli   "en ligne" / "hors ligne" (message retenu)
     avicole/cmd/consigne   appli -> ESP32   valeur de 15 à 35
     avicole/cmd/auto       appli -> ESP32   1 = automatique, 0 = manuel
     avicole/cmd/chauffage, avicole/cmd/ventilateur, avicole/cmd/pompe   1 ou 0
     avicole/alerte         Node-RED -> appli  voyant du module d'alerte sanitaire (JSON, retenu)

   Principe : l'affichage des interrupteurs suit UNIQUEMENT le JSON renvoyé par
   l'ESP32. Un appui publie la commande et marque l'interrupteur « en attente »
   jusqu'à ce que l'état reçu la confirme.
   ============================================================================= */
'use strict';

const SUJET = {
  etat: 'avicole/etat',
  statut: 'avicole/statut',
  consigne: 'avicole/cmd/consigne',
  auto: 'avicole/cmd/auto',
  chauffage: 'avicole/cmd/chauffage',
  ventilateur: 'avicole/cmd/ventilateur',
  pompe: 'avicole/cmd/pompe',
  alerte: 'avicole/alerte',
};
const PORT_WS_DEFAUT = 9001;
const DELAI_PERIME_MS = 6000;      // 3 publications manquées : données anciennes
const DELAI_REPONSE_MS = 5000;     // au-delà, la commande est considérée comme non appliquée
const ACTIONNEURS = ['chauffage', 'ventilateur', 'pompe'];
const NOMS = { auto: 'mode automatique', chauffage: 'chauffage', ventilateur: 'ventilateur', pompe: 'pompe' };

const $ = (id) => document.getElementById(id);
const fmt = (v) => v.toFixed(1).replace('.', ',');

// Bibliothèque Paho : espace de noms Paho (1.1) ou Paho.MQTT (1.0)
const PahoClient = window.Paho && (Paho.Client || (Paho.MQTT && Paho.MQTT.Client));
const PahoMessage = window.Paho && (Paho.Message || (Paho.MQTT && Paho.MQTT.Message));

// ----------------------------------------------------------------------------- état de l'appli
let client = null;
let connecte = false;
let tentative = 0;
let minuterieReconnexion = null;
let dernierEtat = null;
let instantEtat = 0;
let espEnLigne = null;              // null = inconnu, true / false d'après avicole/statut
let glisse = false;
let consigneEnAttente = null;       // { valeur, depuis }
const enAttente = {};               // actionneur -> { valeur, depuis }

// ----------------------------------------------------------------------------- réglages
function lireConfig() {
  let hote = null, port = null;
  try {
    hote = localStorage.getItem('broker_hote');
    port = localStorage.getItem('broker_port');
  } catch (e) { /* stockage indisponible : valeurs par défaut */ }
  // Par défaut, le broker tourne sur le PC qui sert cette page
  return {
    hote: hote || location.hostname || 'localhost',
    port: parseInt(port, 10) || PORT_WS_DEFAUT,
  };
}

function ecrireConfig(hote, port) {
  try {
    localStorage.setItem('broker_hote', hote);
    localStorage.setItem('broker_port', String(port));
  } catch (e) { /* sans effet si le stockage est bloqué */ }
}

// ----------------------------------------------------------------------------- journal et toast
function journal(texte) {
  const li = document.createElement('li');
  li.textContent = new Date().toLocaleTimeString('fr-FR') + '  ' + texte;
  const liste = $('journal');
  liste.prepend(li);
  while (liste.children.length > 40) liste.lastChild.remove();
}

let minuterieToast = null;
function toast(texte) {
  const t = $('toast');
  t.textContent = texte;
  t.classList.add('visible');
  clearTimeout(minuterieToast);
  minuterieToast = setTimeout(() => t.classList.remove('visible'), 3500);
}

// ----------------------------------------------------------------------------- connexion MQTT
function connecter() {
  clearTimeout(minuterieReconnexion);
  if (!PahoClient) {
    majBroker('erreur');
    $('bandeau-broker').textContent =
      "Bibliothèque MQTT non chargée : le fichier paho-mqtt.min.js manque dans le dossier de l'application.";
    $('bandeau-broker').hidden = false;
    journal('paho-mqtt.min.js introuvable dans le dossier de l\'application');
    return;
  }

  // Abandonne proprement l'ancien client
  if (client) {
    const ancien = client;
    ancien.onConnectionLost = () => {};
    ancien.onMessageArrived = () => {};
    try { if (connecte) ancien.disconnect(); } catch (e) { /* déjà fermé */ }
  }
  connecte = false;
  majBroker('connexion');
  majCommandes();

  const { hote, port } = lireConfig();
  const idClient = 'pwa_avicole_' + Math.random().toString(36).slice(2, 8);
  let c;
  try {
    c = new PahoClient(hote, port, '/mqtt', idClient);
  } catch (e) {
    journal('Adresse de broker invalide : ' + hote + ':' + port);
    majBroker('erreur');
    return;
  }
  client = c;
  journal('Connexion à ws://' + hote + ':' + port + ' (' + idClient + ')');

  c.onConnectionLost = (r) => {
    if (c !== client) return;
    connecte = false;
    journal('Connexion perdue : ' + (r && r.errorMessage ? r.errorMessage : 'motif inconnu'));
    majBroker('perdu');
    majCommandes();
    planifierReconnexion();
  };
  c.onMessageArrived = (m) => { if (c === client) recevoir(m); };

  c.connect({
    timeout: 6,
    keepAliveInterval: 20,
    cleanSession: true,
    useSSL: location.protocol === 'https:',
    onSuccess: () => {
      if (c !== client) return;
      connecte = true;
      tentative = 0;
      c.subscribe(SUJET.etat, { qos: 0 });
      c.subscribe(SUJET.statut, { qos: 0 });
      c.subscribe(SUJET.alerte, { qos: 0 });
      journal('Connecté ; abonné à avicole/etat, avicole/statut et avicole/alerte');
      majBroker('ok');
      majCommandes();
    },
    onFailure: (e) => {
      if (c !== client) return;
      journal('Échec de connexion : ' + (e && e.errorMessage ? e.errorMessage : 'broker injoignable'));
      majBroker('erreur');
      planifierReconnexion();
    },
  });
}

function planifierReconnexion() {
  clearTimeout(minuterieReconnexion);
  tentative += 1;
  const delai = Math.min(2000 * tentative, 15000);
  minuterieReconnexion = setTimeout(connecter, delai);
}

function publier(sujet, texte) {
  if (!connecte || !client) {
    toast('Pas de connexion au broker : commande non envoyée');
    return false;
  }
  const m = new PahoMessage(String(texte));
  m.destinationName = sujet;
  m.qos = 0;
  m.retained = false;       // jamais de commande retenue : elle serait rejouée au redémarrage de l'ESP32
  client.send(m);
  journal('→ ' + sujet + '  ' + texte);
  return true;
}

// ----------------------------------------------------------------------------- réception
function recevoir(message) {
  const sujet = message.destinationName;
  let texte = '';
  try { texte = message.payloadString; } catch (e) { return; }

  if (sujet === SUJET.statut) {
    espEnLigne = texte.trim().toLowerCase() === 'en ligne';
    journal('← statut : ' + texte);
    majEsp();
    majCommandes();
    return;
  }
  if (sujet === SUJET.alerte) {
    let alerte;
    try {
      alerte = JSON.parse(texte);
    } catch (e) {
      journal('Alerte illisible : ' + texte.slice(0, 60));
      return;
    }
    afficherAlerte(alerte);
    return;
  }
  if (sujet === SUJET.etat) {
    let etat;
    try {
      etat = JSON.parse(texte);
    } catch (e) {
      journal('JSON illisible : ' + texte.slice(0, 60));
      return;
    }
    appliquerEtat(etat);
  }
}

// Voyant du module d'alerte sanitaire (calculé par Node-RED, affiché tel quel)
function afficherAlerte(a) {
  const voyant = ['vert', 'orange', 'rouge'].includes(a.voyant) ? a.voyant : '';
  $('val-voyant').className = 'voyant ' + voyant;
  $('val-alerte').textContent = typeof a.message === 'string' ? a.message : 'Évaluation reçue';
  const signe = (x) => (x > 0 ? '+' : '') + fmt(x) + ' %';
  const details = [];
  if (typeof a.jour === 'number') details.push('Jour ' + a.jour + ' du lot');
  if (typeof a.ecart_eau === 'number') details.push('eau ' + signe(a.ecart_eau));
  if (typeof a.ecart_gain === 'number') details.push('gain de poids ' + signe(a.ecart_gain));
  $('val-alerte-detail').textContent = details.length
    ? details.join(' · ') + ' (écarts à la tendance des jours normaux)'
    : 'Voyant calculé par Node-RED.';
  journal('← alerte sanitaire : ' + (voyant || 'inconnu') + (typeof a.jour === 'number' ? ' (jour ' + a.jour + ')' : ''));
}

function estVrai(v) {
  return v === true || v === 1 || v === '1' || v === 'true';
}

function appliquerEtat(e) {
  dernierEtat = e;
  instantEtat = Date.now();

  // Mesures ("t" ou "hr" valent null quand le capteur est en défaut)
  $('val-temp').textContent = typeof e.t === 'number' ? fmt(e.t) : '--';
  $('val-hr').textContent = typeof e.hr === 'number' ? fmt(e.hr) : '--';

  // Mode
  const texteMode = typeof e.mode === 'string' && e.mode ? e.mode : 'INCONNU';
  const classeMode = texteMode.startsWith('DEFAUT') ? 'DEFAUT' : texteMode.split(' ')[0];
  const elMode = $('val-mode');
  elMode.textContent = texteMode === 'INCONNU' ? 'Mode inconnu' : texteMode;
  elMode.className = 'mode ' + classeMode;

  // Puissance de chauffe modulée (clé "puissance" du firmware, en %)
  const elPuiss = $('val-puissance');
  if (typeof e.puissance === 'number' && e.puissance > 0) {
    elPuiss.textContent = 'Puissance de chauffe demandée : ' + e.puissance + ' %';
    elPuiss.hidden = false;
  } else {
    elPuiss.hidden = true;
  }

  // Consigne
  if (typeof e.consigne === 'number') {
    $('val-consigne').textContent = fmt(e.consigne);
    if (consigneEnAttente && Math.abs(e.consigne - consigneEnAttente.valeur) < 0.01) {
      consigneEnAttente = null;
      const el = $('val-consigne-choisie');
      el.textContent = 'Consigne appliquée par l\'ESP32';
      el.classList.remove('en-attente');
    }
    if (!glisse && !consigneEnAttente) $('curseur').value = e.consigne;
  }

  // Interrupteurs : l'état visuel vient du JSON, jamais du clic
  const valeurs = {
    auto: estVrai(e.auto),
    chauffage: estVrai(e.chauffage),
    ventilateur: estVrai(e.ventilateur),
    pompe: estVrai(e.pompe),
  };
  for (const [nom, valeur] of Object.entries(valeurs)) {
    $('inter-' + nom).setAttribute('aria-checked', String(valeur));
    if (enAttente[nom] && enAttente[nom].valeur === valeur) delete enAttente[nom];   // commande confirmée
  }
  // Un changement de mode remet les actionneurs à l'arrêt côté ESP32 : on oublie les attentes
  if (valeurs.auto) ACTIONNEURS.forEach((a) => delete enAttente[a]);

  // Un état reçu prouve que l'ESP32 est vivant, même si le statut retenu manque
  if (espEnLigne === null) espEnLigne = true;

  majEsp();
  majCommandes();
  majFraicheur();
}

// ----------------------------------------------------------------------------- mise à jour de l'interface
function majBroker(etat) {
  const point = $('point-broker');
  const texte = $('texte-broker');
  point.className = 'point';
  if (etat === 'ok') {
    point.classList.add('vert');
    texte.textContent = 'Broker connecté';
    $('bandeau-broker').hidden = true;
  } else if (etat === 'connexion') {
    point.classList.add('orange');
    texte.textContent = 'Connexion…';
  } else {
    point.classList.add('rouge');
    texte.textContent = etat === 'perdu' ? 'Broker perdu' : 'Broker injoignable';
    $('bandeau-broker').hidden = false;
  }
}

function majEsp() {
  const point = $('point-esp');
  const texte = $('texte-esp');
  point.className = 'point';
  if (espEnLigne === true) {
    point.classList.add('vert');
    texte.textContent = 'ESP32 en ligne';
  } else if (espEnLigne === false) {
    point.classList.add('rouge');
    texte.textContent = 'ESP32 hors ligne';
  } else {
    texte.textContent = 'ESP32 ?';
  }
  $('bandeau-esp').hidden = !(connecte && espEnLigne === false);
}

function majCommandes() {
  const pret = connecte && dernierEtat !== null;
  const auto = $('inter-auto').getAttribute('aria-checked') === 'true';

  $('curseur').disabled = !pret;
  $('inter-auto').disabled = !pret;
  $('sous-auto').textContent = auto ? 'Le contrôleur décide seul' : 'Commande manuelle depuis le téléphone';
  for (const a of ACTIONNEURS) $('inter-' + a).disabled = !pret || auto;   // grisés en automatique

  for (const nom of ['auto', ...ACTIONNEURS]) {
    $('inter-' + nom).classList.toggle('en-attente', Boolean(enAttente[nom]));
  }
}

function majFraicheur() {
  const el = $('val-fraicheur');
  const carte = $('carte-mesures');
  if (!instantEtat) {
    el.textContent = 'En attente des données…';
    return;
  }
  const age = Date.now() - instantEtat;
  const perime = age > DELAI_PERIME_MS;
  el.textContent = perime
    ? 'Données anciennes (' + Math.round(age / 1000) + ' s)'
    : 'Mis à jour il y a ' + Math.max(0, Math.round(age / 1000)) + ' s';
  el.classList.toggle('perimee', perime);
  carte.classList.toggle('perime', perime);
}

// Expiration des commandes restées sans réponse
function verifierAttentes() {
  const maintenant = Date.now();
  for (const [nom, attente] of Object.entries(enAttente)) {
    if (maintenant - attente.depuis > DELAI_REPONSE_MS) {
      delete enAttente[nom];
      toast('L\'ESP32 n\'a pas confirmé la commande : ' + NOMS[nom]);
    }
  }
  if (consigneEnAttente && maintenant - consigneEnAttente.depuis > DELAI_REPONSE_MS) {
    consigneEnAttente = null;
    const el = $('val-consigne-choisie');
    el.textContent = 'Consigne non confirmée par l\'ESP32';
    el.classList.remove('en-attente');
    if (dernierEtat && typeof dernierEtat.consigne === 'number') $('curseur').value = dernierEtat.consigne;
  }
  majCommandes();
}

// ----------------------------------------------------------------------------- actions de l'utilisateur
function brancherInterrupteur(nom) {
  $('inter-' + nom).addEventListener('click', () => {
    if (!dernierEtat) return;
    const actuel = $('inter-' + nom).getAttribute('aria-checked') === 'true';
    const cible = !actuel;
    if (publier(SUJET[nom], cible ? '1' : '0')) {
      enAttente[nom] = { valeur: cible, depuis: Date.now() };
      majCommandes();
    }
  });
}

function brancherCurseur() {
  const curseur = $('curseur');
  const debut = () => { glisse = true; };
  const fin = () => { glisse = false; };
  curseur.addEventListener('pointerdown', debut);
  curseur.addEventListener('touchstart', debut, { passive: true });
  curseur.addEventListener('pointerup', fin);
  curseur.addEventListener('touchend', fin);

  // Pendant le glissement : affichage local seulement, rien n'est publié
  curseur.addEventListener('input', () => {
    glisse = true;
    const el = $('val-consigne-choisie');
    el.textContent = 'Nouvelle consigne : ' + fmt(parseFloat(curseur.value)) + ' °C';
    el.classList.remove('en-attente');
  });

  // Au lâcher : une seule publication
  curseur.addEventListener('change', () => {
    glisse = false;
    const valeur = parseFloat(curseur.value);
    if (valeur < 15 || valeur > 35) return;
    if (publier(SUJET.consigne, String(valeur))) {
      consigneEnAttente = { valeur, depuis: Date.now() };
      const el = $('val-consigne-choisie');
      el.textContent = 'Envoi de ' + fmt(valeur) + ' °C…';
      el.classList.add('en-attente');
    }
  });
}

function brancherReglages() {
  const { hote, port } = lireConfig();
  $('champ-hote').value = hote;
  $('champ-port').value = port;
  $('bouton-reconnecter').addEventListener('click', () => {
    const h = $('champ-hote').value.trim();
    const p = parseInt($('champ-port').value, 10);
    if (!h || !(p > 0 && p < 65536)) {
      toast('Adresse ou port invalide');
      return;
    }
    ecrireConfig(h, p);
    tentative = 0;
    connecter();
    toast('Reconnexion à ' + h + ':' + p);
  });
}

// ----------------------------------------------------------------------------- démarrage
document.addEventListener('DOMContentLoaded', () => {
  ['auto', ...ACTIONNEURS].forEach(brancherInterrupteur);
  brancherCurseur();
  brancherReglages();
  majEsp();
  majCommandes();
  connecter();

  setInterval(() => { majFraicheur(); verifierAttentes(); }, 1000);

  // Android coupe les connexions des pages en arrière-plan : on se reconnecte au retour
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible' && !connecte) {
      tentative = 0;
      connecter();
    }
  });

  // Le service worker n'est accepté par les navigateurs qu'en HTTPS ou sur localhost
  if ('serviceWorker' in navigator && window.isSecureContext) {
    navigator.serviceWorker.register('sw.js').catch(() => {});
  }
});
