# Audio Lab Reverb

La réverbération d'**Audio Lab** (« Chimiste du son », Transversal Studio) en plugin **VST3 / AU** pour Logic Pro, Ableton Live, FL Studio, Cubase, Reaper, Studio One, Bitwig…, et **AAX** pour Pro Tools (version de test non signée, voir plus bas).

- **Six modes**, chacun bâti sur un algorithme publié :
  - **Chamber, Room, Hall, Church** : réflexions précoces calculées par sources-images (Allen et Berkley, 1979) pour deux sources (gauche, droite) dans une pièce de la taille du mode, deux diffuseurs (Signalsmith, 2021), queue par réseau de retards réinjectés à 16 lignes (FDN, deux moitiés Hadamard couplées, toutes les lignes légèrement modulées) avec filtres d'absorption de Jot : Decay règle exactement le RT60 du médium, les graves et les aigus suivent le caractère du mode.
  - **Plate** : réservoir de Jon Dattorro (1997), longueurs et coefficients publiés ; L et R entrent chacun dans une moitié du réservoir.
  - **Spring** : deux ressorts faits de chaînes de passe-tout étirés dans une boucle (Välimäki, Parker et Abel, 2010), d'où les échos en « boing » ; entrée mono, comme un vrai ressort.
- **Vraie entrée stéréo** (2.0.0) : une source à gauche donne une réverb qui penche à gauche.
- **Réglages de la maquette** : Pre-delay, Decay (RT60 en secondes), Mix, Size, Damp, Width ; choisir un mode charge ses réglages de départ. Changer Size, le mode ou le Pre-delay ne coupe pas la réverb et ne fait aucun clic (2.0.0).
- **Advanced settings** (2.0.0, panneau replié) : Low cut, High cut, Bass (temps de réverbération des graves), Early / Late, Ducking, Freeze.
- **Même son de 44,1 à 192 kHz** (filtres « matched ») ; aucune latence. Depuis la 2.0.0 le plugin est la version « pro » : il s'écarte volontairement du prototype web « Labo Reverb » (JavaScript), dont il garde les niveaux et les RT60.
- **Interface** : potentiomètres orange, curseurs teal, onglets des modes, thème sombre de la charte Transversal, police Poppins, fenêtre redimensionnable de 60 à 200 %, bulles d'aide en anglais.

Plan détaillé de l'interface et du moteur : `docs/11_GUI_Plugin_Reverb.md`.

## Télécharger

Page **Releases** du dépôt : `AudioLabReverb-Mac.zip` (VST3 + AU, Apple Silicon et Intel, installateur en un double-clic) et `AudioLabReverb-Windows.zip` (VST3 64 bits). Chaque fabrication dépose aussi ces archives dans l'onglet **Actions** (*Artifacts*).
Mode d'emploi : fichier `LISEZ-MOI.txt` dans chaque archive.

### Pro Tools (AAX)

Chaque archive contient aussi `Audio Lab Reverb.aaxplugin` (Mac universel, Windows 64 bits), fabriqué avec le SDK AAX fourni par JUCE (GPL v3). Il n'est **pas encore signé** par PACE : il ne se charge que dans **Pro Tools Developer**. Aucune latence.

## Fabrication

Chaque envoi sur `main` déclenche la fabrication sur des machines Mac et Windows de GitHub, puis la validation automatique (`auval` pour l'AU, `pluginval` niveau 10 pour VST3 et AU ; pour l'AAX, contrôle de structure, d'architectures et du point d'entrée). Un tag `v…` publie la version.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Tests (Linux ou Mac) :

```
clang++ -O2 -std=c++17 -I Source tests/quality_test.cpp -o qt && ./qt   # banc qualité du moteur, sans JUCE (seuils chiffrés)
cmake -B build -DALR_TESTS=ON
cmake --build build --target ALRProcessorTest ALRRenderUI
g++ -O2 -std=c++17 -ffp-contract=off -ISource tests/core_run.cpp -o tests/core_run
python3 tests/verify_dsp.py     # moteur C++ ↔ prototype Labo Reverb : niveaux, RT60 (Node.js et NumPy requis)
```

## Sources

- J. Dattorro, *Effect Design, Part 1: Reverberator and Other Filters*, JAES, 1997.
- J.-M. Jot et A. Chaigne, *Digital delay networks for designing artificial reverberators*, AES, 1991 ; J. O. Smith, *Physical Audio Signal Processing* (FDN).
- G. Luff (Signalsmith Audio), *Let's Write A Reverb*, ADC 2021.
- J. B. Allen et D. A. Berkley, *Image method for efficiently simulating small-room acoustics*, JASA, 1979.
- V. Välimäki, J. Parker et J. S. Abel, *Parametric spring reverberation effect*, JAES, 2010 ; J. Parker, EURASIP, 2011.
- S. Costello (Valhalla DSP), présentation AES 2015 ; L. Beranek (salles de concert) ; mesures de la cathédrale St Paul de Londres.

## Licence

Code sous **GNU AGPLv3** (voir `LICENSE`), comme la version libre du framework [JUCE](https://juce.com) qu'il utilise. Police Poppins sous licence SIL OFL 1.1 (`Resources/Poppins-OFL-LICENSE.txt`).

© Transversal Studio, Ludovic Diaz
