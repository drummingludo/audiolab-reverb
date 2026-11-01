# 11 · Plugin « Audio Lab Reverb » (interface et moteur)

Plugin VST3 / AU / AAX (non signé) tiré du prototype « Labo Reverb » et de la maquette « Audio Lab, Modes de réverb » de Ludovic (01/11/2026). Construit comme Audio Lab Compresseur, Opto et FET.

| | |
|---|---|
| Dépôt GitHub | drummingludo/audiolab-reverb |
| Code plugin (AU) | `aufx AlRv Trsv` |
| Identifiant | fr.transversalstudio.audiolab.reverb |
| Catégories | VST3 Fx / Reverb ; AAX Reverb |
| Taille de base | 1000 × 460 px (redimensionnable de 60 à 200 %, mémorisée) |
| Formats d'entrée / sortie | mono → mono, mono → stéréo, stéréo → stéréo ; aucune latence |

## 1. Réglages

| Réglage | Plage | Remarque |
|---|---|---|
| Mode | Chamber, Plate, Spring, Room, Hall, Church | Choisir un onglet charge les valeurs du mode (tableau § 2), en un seul geste pour l'hôte. Une automation du seul Mode ne change que le moteur. |
| Pre-delay | 0 à 250 ms (pas de 1 ms) | commun à tous les modes |
| Decay | 0,1 à 10 s, échelle logarithmique | RT60 du médium (vers 500 Hz) |
| Mix | 0 à 100 % | dry/wet linéaire, lissé |
| Size | 0 à 100 % | change la structure : fondu de 12 ms à chaque modification |
| Damp | 0 à 100 % | amortissement des aigus |
| Width | 0 à 100 % | largeur mid/side du signal traité |
| Bypass | | fondu de 20 ms ; une fois le fondu fini, sortie = entrée exacte |

## 2. Valeurs de chaque mode (maquette)

| Mode | Pre-delay | Decay | Mix | Size | Damp | Width |
|---|---|---|---|---|---|---|
| Chamber | 0 ms | 1,5 s | 30 % | 40 % | 50 % | 80 % |
| Plate | 10 ms | 2,2 s | 25 % | 60 % | 30 % | 100 % |
| Spring | 0 ms | 2,0 s | 35 % | 30 % | 40 % | 50 % |
| Room | 0 ms | 0,6 s | 20 % | 25 % | 50 % | 70 % |
| Hall | 25 ms | 2,2 s | 30 % | 80 % | 45 % | 100 % |
| Church | 40 ms | 5,0 s | 40 % | 100 % | 65 % | 100 % |

Valeurs par défaut du plugin (« Factory Default ») : celles de Chamber.

## 3. Moteur (identique au prototype)

Portage ligne à ligne des classes `DSP`, `PlateCore`, `RoomCore`, `SpringCore` et `ReverbEngine` du Labo Reverb (mêmes types : mémoires en float, calculs en double ; compilé sans fusion multiplication-addition).

- **Chamber, Room, Hall, Church** : sources-images d'ordre 3 (Chamber, Room) ou 2 (Hall, Church), 48 réflexions au plus sur une fenêtre liée au volume ; diffuseur 8 voies (4 étapes, 3 pour Church) avec inversions de polarité, permutation et Hadamard ; FDN 8 lignes (Householder), longueurs premières au moins égales au libre parcours moyen 4V/S ; par ligne, gain médium + étagère grave + étagère aiguë (Jot), recalés pour un RT60 exact vers 600 Hz ; coefficient des parois déduit du RT60 (Eyring) ; niveaux des réflexions, du diffuseur et de la queue calés sur une seule enveloppe exponentielle.
  - Chamber : pièce 6 × 4,2 × 3,2 m à Size 50 %, graves x1,0, aigus au-dessus de 3 kHz, retour filtré 180 Hz à 10 kHz.
  - Room : 7 × 5,5 × 3 m, graves x1,1, aigus au-dessus de 5 kHz, sans modulation.
  - Hall : 26 × 17 × 12 m, graves x1,35, aigus au-dessus de 4 kHz, modulation 0,25 ms.
  - Church : 36 × 16 × 15 m, graves x1,45, aigus au-dessus de 3 kHz, modulation 0,15 ms.
  - Size multiplie les dimensions de 0,66 à 1,52 ; Damp règle le rapport RT aigus / médium de 0,95 à 0,2.
- **Plate** : réservoir de Dattorro (valeurs publiées) ; Decay → coefficient de boucle (formule corrigée par mesure), Damp → amortissement de 20 kHz à 3 kHz, Size → longueurs de 0,6 à 1,4 fois.
- **Spring** : deux ressorts ; boucle grave de 100 passe-tout étirés (a = 0,6, k = fs / (2 × 4,4 kHz)) et passe-bas à fC ; boucle aiguë de 40 passe-tout (a = −0,6) à −14 dB ; délai de transit 30 à 70 ms (Size) ; modulation aléatoire lente.
- Niveaux des modes égalisés sur leurs réglages par défaut. Anti-dénormaux : décalage de 1e-24 (comme le prototype) et `ScopedNoDenormals`.
- Temps réel : aucune allocation dans le traitement (tableaux fixes pour les sources-images et le choix des longueurs).

## 4. Interface (maquette)

- Fond #1D1E1C, marges de 24 px ; colonne gauche (x 24 → 424), colonne droite (x 456 → 976).
- **Potentiomètres Pre-delay et Decay** : 170 px (repère de 100 unités × 1,7), centres x 122 et 326, y 132. Rail #3E3F3D (éclairci au survol), arc 3,2 u en dégradé #BB3E03 → #EAA93D avec halo, pastille de départ, capuchon #373837 de 66 u avec ombres et liseré, repère #EAA93D (#BB3E03 à zéro), valeur Poppins 600 15 u, unité 7 u #A5A6A6, nom 13 px 600 majuscules.
- **Curseurs Mix, Size, Damp, Width** : lignes de 400 × 24 à y 268, 308, 348, 388 ; nom 11 px 600 sur 70 px, piste de 250 px (rail 4 px, partie remplie en dégradé #0A9396 → #79AFB0), bouton 18 px #373837 avec ombres, valeur 12 px à droite.
- **Onglets des modes** : 520 × 41 à y 87, bande #2A2B29, segment choisi #005F73.
- **Écran** : 520 × 222 à y 144, fond #2A2B29, bord #3E3F3D, coins 10 ; dessin dans le repère 420 × 170 de la maquette. Courbe = enveloppe réelle du moteur (réponse impulsionnelle calculée à 48 kHz dans un fil de fond, lissée sur 70 ms), remplissage ambre 25 % → 2 %, réflexions précoces en barres #79AFB0, repère RT60 pointillé au Decay réglé. Axe : 1 s (Room), 2 s (Chamber), 3 s (Plate, Spring, Hall), 6 s (Church), adapté au Decay.
- Manipulation : glisser vertical (Maj = fin) pour les potentiomètres, clic ou glisser pour les curseurs, molette, flèches, double-clic = valeur du mode en cours.

## 5. Vérifications faites

- `tests/verify_dsp.py` : plugin ↔ prototype (empreinte SHA-256 de la copie `reverb_ref.js`) : 6 modes × 2 réglages × impulsion et musique × 44,1 / 48 / 96 kHz, écart max 9 × 10⁻¹³ ; blocs de 1 à 4096 ; changements de réglages en cours de lecture ; RT60 mesuré à 500 Hz sur les valeurs de la maquette (−4 % à +3 %) ; vitesse (14 à 87 fois le temps réel à 96 kHz).
- `tests/processor_test.cpp` : sauvegarde / rappel, chargement des valeurs d'un mode, bypass exact, Mix 0 % exact, indépendance de la taille des blocs, mono → stéréo, mono → mono à 96 kHz, latence nulle, queue après le son, extrêmes et réglages qui bougent dans les 6 modes.
- `tests/render_ui.cpp` : captures des 6 modes, 60 % et 200 %.
- pluginval niveau 10 (Linux VST3, avec les tests d'interface) ; sur GitHub : auval + pluginval niveau 10 (Mac VST3 + AU), pluginval (Windows VST3), contrôle de structure AAX.
- Non testé ici : écoute réelle dans un logiciel, Windows sur un vrai PC, AAX dans Pro Tools Developer.
