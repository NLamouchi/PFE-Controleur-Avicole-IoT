"""
rejouer_scenario.py -- Rejoue un scénario du module d'alerte sanitaire sur le broker MQTT.

Sert à la démonstration : Node-RED reçoit un jour du lot par message, calcule le
voyant et le publie sur avicole/alerte, où l'application web l'affiche.

LES DONNÉES SONT SYNTHÉTIQUES : ce sont les scénarios construits S1, S2 et S3 de
scripts/alerte_sanitaire.py (fichier scenarios_alerte.json). La maquette ne porte
ni pesée ni mesure de la consommation d'eau.

Utilisation (Mosquitto et Node-RED déjà lancés, flux flux_avicole.json importé) :
    python node-red/rejouer_scenario.py S2
    python node-red/rejouer_scenario.py S3 --hote 192.168.1.10 --pause 2
Seule la bibliothèque standard est utilisée ; la publication passe par le client
mosquitto_pub livré avec Mosquitto (chemin réglable par --pub).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ICI = Path(__file__).resolve().parent
LIBELLES = ["vert", "orange", "rouge", "orange (cause climatique)"]


def main():
    p = argparse.ArgumentParser(description="Rejoue un scénario construit du module d'alerte sanitaire.")
    p.add_argument("scenario", choices=["S1", "S2", "S3"],
                   help="S1 : lot normal ; S2 : dérive sanitaire ; S3 : stress thermique")
    p.add_argument("--hote", default="localhost", help="adresse du broker (défaut : localhost)")
    p.add_argument("--port", type=int, default=1883, help="port MQTT du broker (défaut : 1883)")
    p.add_argument("--pause", type=float, default=1.0, help="secondes entre deux jours (défaut : 1)")
    p.add_argument("--jours", type=int, default=None, help="nombre de jours publiés (défaut : tout le lot)")
    p.add_argument("--pub", default=shutil.which("mosquitto_pub") or r"C:\Program Files\mosquitto\mosquitto_pub.exe",
                   help="chemin du client mosquitto_pub")
    a = p.parse_args()

    if not (Path(a.pub).is_file() or shutil.which(a.pub)):
        sys.exit(f"mosquitto_pub introuvable : {a.pub} (préciser son chemin avec --pub)")
    scenarios = json.loads((ICI / "scenarios_alerte.json").read_text(encoding="utf-8"))
    s = next(x for x in scenarios if x["nom"] == a.scenario)

    def publier(sujet, texte):
        # Le message passe par un fichier : l'invite de commandes Windows retire
        # les guillemets d'un JSON passé en argument.
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
            f.write(texte)
            nom = f.name
        try:
            subprocess.run([a.pub, "-h", a.hote, "-p", str(a.port), "-q", "1", "-t", sujet, "-f", nom], check=True)
        finally:
            os.remove(nom)

    publier("avicole/lot/nouveau", f"scénario {a.scenario}")
    n = len(s["eau"]) if a.jours is None else max(0, min(a.jours, len(s["eau"])))
    for i in range(n):
        jour = {"jour": i + 1, "eau": s["eau"][i], "poids": s["poids"][i], "stress": bool(s["stress"][i])}
        publier("avicole/lot/jour", json.dumps(jour))
        print(f"jour {i + 1:2d} publié ; voyant attendu : {LIBELLES[s['voyant'][i]]}")
        time.sleep(a.pause)


if __name__ == "__main__":
    main()
