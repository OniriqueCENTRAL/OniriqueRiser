# ONIRIQUE RISER

Montées de reverb inversée ("reverse reverb") pour la voix, en un geste : posé sur la piste de voix, le plugin capture la
voix tout seul quand on lance la lecture du DAW ; à l'arrêt, la montée est prête. On attrape la forme d'onde et on la
glisse dans le DAW, sur la mesure indiquée : elle finit exactement là où la voix commence.

Petite interface carrée (560 x 560, redimensionnable) : panneau de la montée (LISTEN, LOOP, vue FX / RAW, INPUT OPEN /
CLOSED, EDIT, choix de la phrase), grand bouton REVERSE (1 temps, 2 temps, 1 mesure, 2 mesures, 4 mesures, au tempo du DAW),
effets en un clic : PHONE, CHORUS, TREMOLO, SWEEP, WIDE. Clic sur le nom d'un effet : son menu (MIX + DRIVE, DEPTH, TIMING,
START ou WIDTH). EDIT : SPACE (ROOM, PLATE, DARK, HALL, SHIMMER), SOURCE, SIZE, CURVE, GAIN, S-CLEAN, IN SYNC.

- **FX** : la montée finie, à glisser dans le DAW. La courbe de pré-fondu passe dessus : tirer son point vers le bas fait
  partir la montée plus discrète et arriver plus tard (gain t^k, k = 3 CURVE : c'est le réglage CURVE d'EDIT).
- **RAW** : toute la prise ; deux poignées choisissent la partie (la syllabe) qui crée la reverse reverb, la montée finit
  là où elle commence. LISTEN joue cette partie. Une nouvelle prise, une autre phrase ou un autre SOURCE reviennent au
  choix automatique (début de la phrase, durée SOURCE).
- **INPUT OPEN / CLOSED** : fermé, plus rien n'est capturé et la prise reste telle quelle (pour ne pas ré-enregistrer sa
  lead sans le vouloir, ou quand la montée est posée sur la même piste). Réglage de session, hors presets.

## Fonctionnement
- **Capture** (thread audio, `PluginProcessor.cpp`) : pendant la lecture, une pré-écoute de 2 s tourne ; dès que le son
  dépasse -45 dBFS, la prise commence (pré-écoute comprise) avec sa place sur la ligne de temps (secondes, noires, tempo,
  mesure), jusqu'à 32 s. L'arrêt de la lecture ou un saut (boucle, clic sur la règle) termine la prise ; une lecture sans
  voix ne remplace pas la prise d'avant.
- **Moteur** (`DSP/RiserEngine.h`, hors temps réel, thread de rendu) : phrases détectées (retour du son après 250 ms de
  silence) ; début de la phrase choisie (SOURCE) ; S-CLEAN : de-esser de la VOCALBOX à fond + envoi qui plonge de -24 dB sur
  chaque sifflante ; retourné, reverb de la VOCALBOX à 100 % (FDN 16 lignes, queue réglée sur la montée), retourné ;
  couleurs ; normalisation -1 dBFS + GAIN ; silence ajouté devant pour que le fichier commence sur un temps du DAW.
- **IN SYNC** : la montée est jouée par le plugin au bon moment pendant la lecture (sans glisser de fichier).
- **LISTEN / LOOP** : écoute de la montée (vue FX) ou de la partie choisie de la prise (vue RAW), DAW à l'arrêt.
- Le fichier glissé : WAV 24 bits stéréo dans le dossier temporaire « ONIRIQUE RISER », nommé avec la mesure et le temps.

Gratuit : aucun système de licence. Presets d'usine + utilisateur (%APPDATA%/Onirique/Riser/Presets, *.orsrpreset).

## Générer le .vst3 (Windows)
    build_windows.bat

Mac : `./build_mac.sh`. Chaque envoi sur main lance `.github/workflows/build.yml` (Mac universel VST3 + AU, Windows), puis
publie dans ONIRIQUE CENTRAL ; le message du commit devient la note de version.

Captures locales : compiler avec `/DONIRIQUE_UI_TEST=1` (jamais dans les versions publiées) ; `ONIRIQUE_UI_TEST=demo` (une prise de synthèse), `edit` (panneau EDIT ouvert), `rawview` (vue RAW), `closed` (INPUT CLOSED).
