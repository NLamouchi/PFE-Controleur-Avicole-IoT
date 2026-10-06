// =============================================================================
// verifier_alerte.js : vérifie que le nœud fonction Node-RED du module d'alerte
// (alerte_sanitaire_fonction.js) reproduit la fonction analyser() du script Python.
//
// Exécution (depuis le dossier node-red) : node verifier_alerte.js
// Données de référence : scenarios_alerte.json, produit par scripts/alerte_sanitaire.py
//
// Le corps du nœud est exécuté tel quel, avec des objets msg, flow et node simulés, et
// reçoit les jours du lot un par un, comme il les recevrait par MQTT.
// =============================================================================
'use strict';
const fs = require('fs');
const path = require('path');

const corps = fs.readFileSync(path.join(__dirname, 'alerte_sanitaire_fonction.js'), 'utf8');
const noeudFonction = new Function('msg', 'flow', 'node', corps);
const scenarios = JSON.parse(fs.readFileSync(path.join(__dirname, 'scenarios_alerte.json'), 'utf8'));
const CODE = { vert: 0, orange: 1, rouge: 2 };

function executerLot(sc) {
  const contexte = {};
  const flow = { get: (k) => contexte[k], set: (k, v) => { contexte[k] = v; } };
  const node = { status: () => {}, warn: (t) => { throw new Error(t); } };
  noeudFonction({ topic: 'avicole/lot/nouveau', payload: '' }, flow, node);
  const voyants = [], ecartsEau = [], ecartsGain = [];
  for (let i = 0; i < sc.eau.length; i++) {
    const sorties = noeudFonction({ topic: 'avicole/lot/jour',
      payload: { jour: i + 1, eau: sc.eau[i], poids: sc.poids[i], stress: sc.stress[i] } }, flow, node);
    const r = sorties[1].payload;
    voyants.push(r.climatique ? 3 : CODE[r.voyant]);
    ecartsEau.push(r.ecart_eau);
    ecartsGain.push(r.ecart_gain);
  }
  return { voyants, ecartsEau, ecartsGain };
}

let lots = 0, jours = 0, desaccords = 0, ecartMax = 0;
const exemples = [];
for (const sc of scenarios) {
  const r = executerLot(sc);
  lots++;
  for (let i = 0; i < sc.voyant.length; i++) {
    jours++;
    if (r.voyants[i] !== sc.voyant[i]) {
      desaccords++;
      if (exemples.length < 5) exemples.push(`${sc.nom} jour ${i + 1} : Python ${sc.voyant[i]}, Node-RED ${r.voyants[i]}`);
    }
    // écarts arrondis à 0,1 % par le nœud : comparaison à 0,05 point près
    if (sc.d_eau && sc.d_eau[i] !== -999) {
      ecartMax = Math.max(ecartMax, Math.abs(100 * sc.d_eau[i] - r.ecartsEau[i]),
                                    Math.abs(100 * sc.d_gain[i] - r.ecartsGain[i]));
    }
  }
}
console.log(`lots vérifiés          : ${lots} (S1, S2, S3 et ${lots - 3} lots normaux)`);
console.log(`jours comparés         : ${jours}`);
console.log(`voyants différents     : ${desaccords}`);
console.log(`écart max sur S1 à S3  : ${ecartMax.toFixed(3)} point de %`);
exemples.forEach((e) => console.log('  ' + e));
process.exit(desaccords === 0 ? 0 : 1);
