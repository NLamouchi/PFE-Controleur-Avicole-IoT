// =============================================================================
// Module d'alerte sanitaire précoce : corps du nœud fonction Node-RED
// (portage de la fonction analyser() de scripts/alerte_sanitaire.py, vérifié par
//  node-red/verifier_alerte.js sur les mêmes scénarios)
//
// Entrée (sujet avicole/lot/jour, JSON), un message par jour du lot, sans jour manquant :
//   { "jour": 24, "eau": 118.5, "poids": 1.62, "stress": false }
//   eau : consommation du jour ; poids : poids moyen du jour ; stress : jour de stress thermique
// Entrée (sujet avicole/lot/nouveau) : remet l'historique à zéro au début d'un lot.
//
// Sorties : 1) ligne CSV pour le fichier des alertes ; 2) voyant publié sur avicole/alerte (retenu).
// Seuils : HYPOTHÈSES à valider avec un vétérinaire, identiques au script Python.
// =============================================================================
const SEUIL_EAU_ORANGE = -0.10, SEUIL_EAU_ROUGE = -0.20;    // écart relatif de la consommation d'eau
const SEUIL_GAIN_ORANGE = -0.30, SEUIL_GAIN_ROUGE = -0.50;  // écart relatif du gain de poids journalier
const SEUIL_CLIMAT = 0.10;                                   // écart d'eau signalé pendant un stress thermique
const NB_JOURS_TENDANCE = 5;
const VERT = 0, ORANGE = 1, ROUGE = 2, CLIMAT = 3;
const LIBELLE = ["vert", "orange", "rouge", "orange"];
const MESSAGE = ["Lot normal", "Suspicion sanitaire : surveiller le lot",
                 "Alerte sanitaire : prévenir le vétérinaire",
                 "Cause climatique probable : vérifier la ventilation et le pad-cooling"];

// Extrapolation linéaire (moindres carrés) au point x à partir des points (xs, ys)
function tendance(xs, ys, x) {
    const n = xs.length;
    const mx = xs.reduce((s, v) => s + v, 0) / n;
    const my = ys.reduce((s, v) => s + v, 0) / n;
    let sxy = 0, sxx = 0;
    for (let k = 0; k < n; k++) {
        sxy += (xs[k] - mx) * (ys[k] - my);
        sxx += (xs[k] - mx) * (xs[k] - mx);
    }
    const a = sxy / sxx;
    return a * x + (my - a * mx);
}

// Voyant et écarts relatifs jour par jour (indices = rang du jour dans le lot)
function analyser(eau, poids, stress) {
    const n = eau.length;
    const gain = new Array(n).fill(NaN);
    for (let i = 3; i < n; i++) gain[i] = (poids[i] - poids[i - 3]) / 3;   // gain moyen sur 3 jours
    const voyant = new Array(n).fill(VERT);
    const dEau = new Array(n).fill(NaN), dGain = new Array(n).fill(NaN);
    const C = new Array(n).fill(false), F = new Array(n).fill(false);
    for (let i = 0; i < n; i++) {
        const candidats = [];
        for (let k = 3; k < i; k++) if (!stress[k] && !C[k]) candidats.push(k);
        const ref = candidats.slice(-NB_JOURS_TENDANCE);                   // 5 derniers jours normaux
        if (ref.length < NB_JOURS_TENDANCE) continue;
        dEau[i] = eau[i] / tendance(ref, ref.map(k => eau[k]), i) - 1;
        dGain[i] = gain[i] / tendance(ref, ref.map(k => gain[k]), i) - 1;
        if (stress[i]) {
            if (Math.abs(dEau[i]) >= SEUIL_CLIMAT || dGain[i] <= SEUIL_GAIN_ORANGE) voyant[i] = CLIMAT;
            continue;
        }
        C[i] = dEau[i] <= SEUIL_EAU_ORANGE || dGain[i] <= SEUIL_GAIN_ORANGE;
        F[i] = dEau[i] <= SEUIL_EAU_ROUGE || dGain[i] <= SEUIL_GAIN_ROUGE;
        const c1 = i >= 1 && C[i - 1];
        const c2 = i >= 2 && C[i - 2];
        if ((F[i] && c1) || (C[i] && c1 && c2)) voyant[i] = ROUGE;
        else if (F[i] || (C[i] && c1)) voyant[i] = ORANGE;
    }
    return { voyant, dEau, dGain };
}

// ----------------------------------------------------------------------------- traitement du message
if (msg.topic === "avicole/lot/nouveau") {
    flow.set("lot", { jour: [], eau: [], poids: [], stress: [] });
    node.status({ fill: "grey", shape: "ring", text: "nouveau lot" });
    return null;
}

const j = msg.payload;
if (!j || typeof j.jour !== "number" || typeof j.eau !== "number" || typeof j.poids !== "number") {
    node.warn("Message journalier incomplet : " + JSON.stringify(j));
    return null;
}
const lot = flow.get("lot") || { jour: [], eau: [], poids: [], stress: [] };
const rang = lot.jour.indexOf(j.jour);
if (rang >= 0) {                                   // jour déjà reçu : la nouvelle valeur remplace l'ancienne
    lot.eau[rang] = j.eau; lot.poids[rang] = j.poids; lot.stress[rang] = !!j.stress;
} else {
    lot.jour.push(j.jour); lot.eau.push(j.eau); lot.poids.push(j.poids); lot.stress.push(!!j.stress);
}
flow.set("lot", lot);

const r = analyser(lot.eau, lot.poids, lot.stress);
const i = lot.jour.indexOf(j.jour);
const v = r.voyant[i];
const pct = x => (Number.isNaN(x) ? null : Math.round(1000 * x) / 10);   // écart en %, une décimale
const resultat = {
    jour: j.jour, voyant: LIBELLE[v], climatique: v === CLIMAT, message: MESSAGE[v],
    ecart_eau: pct(r.dEau[i]), ecart_gain: pct(r.dGain[i])
};
node.status({ fill: ["green", "yellow", "red", "yellow"][v], shape: "dot", text: "jour " + j.jour + " : " + resultat.voyant });

const ligne = [new Date().toISOString(), j.jour, j.eau, j.poids, j.stress ? 1 : 0,
               resultat.ecart_eau ?? "", resultat.ecart_gain ?? "", resultat.voyant, resultat.message].join(";");
return [{ payload: ligne }, { topic: "avicole/alerte", payload: resultat, retain: true }];
