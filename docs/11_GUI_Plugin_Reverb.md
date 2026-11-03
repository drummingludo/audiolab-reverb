# 11 · Plugin « Audio Lab Reverb » / « Lab Verb » (interface et moteur)

Plugin VST3 / AU / AAX (non signé) tiré du prototype « Labo Reverb » et de la maquette « Audio Lab, Modes de réverb » de Ludovic (01/11/2026). Construit comme Audio Lab Compresseur, Opto et FET. Depuis la 1.1.0 : interface en anglais, nom affiché « Lab Verb » (fiche 14), et mêmes tailles d'éléments que le Compresseur (demande de Ludovic du 03/11/2026 : la Reverb paraissait plus grande que le Compresseur). Version actuelle : **2.0.0** (§ 6), qualité audio « pro » d'après l'audit (fiche 15).

| | |
|---|---|
| Dépôt GitHub | drummingludo/audiolab-reverb |
| Code plugin (AU) | `aufx AlRv Trsv` |
| Identifiant | fr.transversalstudio.audiolab.reverb |
| Catégories | VST3 Fx / Reverb ; AAX Reverb |
| Taille de base | version de test sans écran : face 286 × 386 px validée par Ludovic ; 2.0.0 : 286 × 422 px replié (rangée du bouton « Advanced settings » ajoutée), 286 × 666 px déplié ; redimensionnable de 60 à 200 %, mémorisée (1.1.0 avec écran : 760 × 304 ; 1.0.0 : 1000 × 460) |
| Formats d'entrée / sortie | mono → mono, mono → stéréo, stéréo → stéréo ; aucune latence |

## 1. Réglages de la face principale

| Réglage | Plage | Remarque |
|---|---|---|
| Mode | Chamber, Plate, Spring, Room, Hall, Church | Choisir un onglet charge les valeurs du mode (tableau § 2), en un seul geste pour l'hôte. Une automation du seul Mode ne change que le moteur. Les réglages avancés (§ 6.2) ne changent pas. |
| Pre-delay | 0 à 250 ms (pas de 1 ms) | commun à tous les modes ; 2.0.0 : deux têtes de lecture en fondu de 30 ms (aucun clic) |
| Decay | 0,1 à 10 s, échelle logarithmique | RT60 du médium (exact à 500 Hz depuis la 2.0.0) |
| Mix | 0 à 100 % | dry/wet linéaire, lissé |
| Size | 0 à 100 % | change la structure ; 2.0.0 : fondu entre deux moteurs, la queue en cours continue (1.x : fondu de 12 ms par zéro) |
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

## 3. Moteur de la 1.x (pour mémoire)

Portage ligne à ligne des classes `DSP`, `PlateCore`, `RoomCore`, `SpringCore` et `ReverbEngine` du Labo Reverb (mêmes types : mémoires en float, calculs en double ; compilé sans fusion multiplication-addition). La 2.0.0 garde ces bases et les modifie (§ 6.1).

- **Chamber, Room, Hall, Church** : sources-images d'ordre 3 (Chamber, Room) ou 2 (Hall, Church), 48 réflexions au plus sur une fenêtre liée au volume ; diffuseur 8 voies (4 étapes, 3 pour Church) avec inversions de polarité, permutation et Hadamard ; FDN 8 lignes (Householder), longueurs premières au moins égales au libre parcours moyen 4V/S ; par ligne, gain médium + étagère grave + étagère aiguë (Jot), recalés pour un RT60 exact vers 600 Hz ; coefficient des parois déduit du RT60 (Eyring) ; niveaux des réflexions, du diffuseur et de la queue calés sur une seule enveloppe exponentielle.
  - Chamber : pièce 6 × 4,2 × 3,2 m à Size 50 %, graves x1,0, aigus au-dessus de 3 kHz, retour filtré 180 Hz à 10 kHz.
  - Room : 7 × 5,5 × 3 m, graves x1,1, aigus au-dessus de 5 kHz, sans modulation (2.0.0 : modulation très faible).
  - Hall : 26 × 17 × 12 m, graves x1,35, aigus au-dessus de 4 kHz, modulation 0,25 ms.
  - Church : 36 × 16 × 15 m, graves x1,45, aigus au-dessus de 3 kHz, modulation 0,15 ms.
  - Size multiplie les dimensions de 0,66 à 1,52 ; Damp règle le rapport RT aigus / médium de 0,95 à 0,2.
- **Plate** : réservoir de Dattorro (valeurs publiées) ; Decay → coefficient de boucle (formule corrigée par mesure), Damp → amortissement de 20 kHz à 3 kHz, Size → longueurs de 0,6 à 1,4 fois.
- **Spring** : deux ressorts ; boucle grave de 100 passe-tout étirés (a = 0,6, k = fs / (2 × 4,4 kHz)) et passe-bas à fC ; boucle aiguë de 40 passe-tout (a = −0,6) à −14 dB ; délai de transit 30 à 70 ms (Size) ; modulation aléatoire lente.
- 1.x : entrée sommée en mono (L + R) / 2 pour tous les modes ; Size et le mode passaient par un fondu à zéro de 12 ms ; le Pre-delay sautait d'un retard entier à l'autre (clic).
- Niveaux des modes égalisés sur leurs réglages par défaut. Anti-dénormaux : décalage de 1e-24 (comme le prototype) et `ScopedNoDenormals`.

## 4. Interface

- Version de test sans écran (03/11/2026, validée) : une colonne de x 24 à 262 ; onglets des modes en 2 rangées de 3 (238 × 60 à y 24) ; potentiomètres Pre-delay et Decay du Compresseur (boîtes 143 × 130 à x 8 et 135, y 103) ; curseurs Mix, Size, Damp, Width (238 × 24 à y 254, 282, 310, 338). L'écran de décroissance (code `DecayGraph.h`, conservé) est retiré pour ce test.
- **Potentiomètres** : potentiomètre principal du Compresseur (charte § 7) : arc Ø 98, épaisseur 3,2, dégradé #BB3E03 → #EAA93D, halo fin et discret, pastille de départ ; capuchon 74 avec ombres et liseré ; repère 1,5 px #EAA93D (#BB3E03 à zéro) ; valeur 14 px 600, unité 11 px gris doux, nom 11 px 600 majuscules 58 px sous le centre.
- **Curseurs** : nom 11 px 600 sur 54 px, piste à partir de x 58 (rail 4 px, partie remplie en dégradé #0A9396 → #79AFB0), bouton 18 px #373837 avec ombres, valeur 12 px à droite.
- **Onglets des modes** : bande #2A2B29, segment choisi #005F73, texte 12 px.
- 2.0.0 : bouton et panneau « Advanced settings » (§ 6.3).
- Manipulation : glisser vertical (Maj = fin) pour les potentiomètres, clic ou glisser pour les curseurs, molette, flèches, double-clic = valeur du mode en cours (réglages avancés : valeur par défaut, neutre).
- 1.1.0 avec écran (pour mémoire) : fenêtre 760 × 304, écran 450 × 216 ; 1.0.0 : fenêtre 1000 × 460.

## 5. Vérifications de la 1.x

- `tests/verify_dsp.py` (1.x) : plugin ↔ prototype identiques (écart max 9 × 10⁻¹³) ; RT60 à 500 Hz −4 % à +3 % ; vitesse 14 à 87 fois le temps réel à 96 kHz.
- `tests/processor_test.cpp`, `tests/render_ui.cpp`, pluginval niveau 10 (Linux VST3) ; sur GitHub : auval + pluginval (Mac), pluginval (Windows), contrôle de structure AAX.

## 6. Version 2.0.0 (03/11/2026) : qualité audio « pro »

Décision de Ludovic (audit validé, « go pour tout ») : les plugins sont la version « pro » ; le plugin s'écarte donc volontairement du prototype web, qui ne change pas pour l'instant. La face principale validée ne change pas ; les nouveaux réglages sont dans un panneau « Advanced settings » replié.

### 6.1 Ce qui change

1. **Vraie entrée stéréo.**
   - **Chamber, Room, Hall, Church** : deux sources, gauche et droite, placées comme deux enceintes devant l'auditeur (écartées de 0,2 × la largeur de la pièce de chaque côté), chacune avec ses propres réflexions précoces (sources-images) ; deux diffuseurs (un par voie, tirages différents) ; FDN de 16 lignes en deux moitiés (lignes 0 à 7 alimentées par L et lues par la sortie gauche, 8 à 15 pour R), mélangées par une rotation de 22,5° (environ 15 % de l'énergie passe d'une moitié à l'autre à chaque tour). Résultat : une source à gauche donne des réflexions précoces nettement à gauche, puis une queue qui s'étale et se rééquilibre, comme dans une vraie salle. Chaque source porte la moitié de l'énergie : une source au centre garde le niveau de la 1.x ; une source jouée d'un seul côté est 3 dB plus faible qu'au centre (somme des puissances ; 1.x : 6 dB).
   - **Plate** : L entre dans la moitié du réservoir qui nourrit surtout la sortie gauche, R dans l'autre ; chaque voie a ses 4 diffuseurs d'entrée (longueurs de Dattorro). Une source au centre (L = R) fait exactement travailler le réservoir comme avant.
   - **Spring : reste mono**, comme un vrai ressort : un seul transducteur d'entrée reçoit (L + R) / 2 ; la stéréo vient des deux ressorts de longueurs différentes. Une source à gauche ou à droite donne exactement la même réverb (vérifié au bit près).
2. **FDN 16 lignes et modulation légère partout.** 16 lignes au lieu de 8 (deux fois plus d'échos par seconde), mélange de Hadamard dans chaque moitié (au lieu de Householder : chaque tour mélange toutes les lignes) ; toutes les lignes sont modulées dans tous les modes, à des vitesses toutes différentes : Room 0,02 ms à 0,45 Hz (très faible, nouveau), Chamber 0,06 ms à 0,7 Hz, Hall 0,25 ms à 0,6 Hz, Church 0,15 ms à 0,4 Hz (vitesses de 0,62 à 1,59 fois ces valeurs selon la ligne). Lecture modulée par interpolation passe-tout du 1er ordre : aucune perte d'aigus (l'interpolation linéaire de la 1.x en perdait, surtout à 44,1 / 48 kHz). RT60 toujours exact : gains de Jot par ligne, recalés à 500 Hz (1.x : 600 Hz).
3. **Size, mode et Pre-delay sans coupure ni clic.**
   - Deux moteurs complets (« emplacements ») sont préparés dès le début de la lecture. Changer Size ou le mode configure l'emplacement libre, puis l'entrée passe de l'un à l'autre en 20 ms (fondu en cloche) : le moteur sortant ne reçoit plus de son mais **garde sa queue**, qui s'éteint naturellement (il s'arrête quand elle reste sous −120 dBFS pendant 100 ms). Si un nouveau changement arrive pendant que l'ancien moteur sonne encore, celui-ci s'efface en 30 ms (automation continue de Size : un changement toutes les 75 ms environ, la réverb ne s'interrompt jamais). Le changement attend que le réglage soit stable 4 ms (30 ms au plus), pour qu'un changement de mode et ses valeurs arrivent ensemble. La mémoire d'un emplacement libéré est vidée petit à petit (65 536 échantillons au plus par tranche de 128), jamais d'un coup dans le fil audio.
   - Pre-delay : deux têtes de lecture ; un nouveau réglage démarre un fondu de 30 ms de l'ancienne vers la nouvelle (le suivant attend la fin du fondu).
   - Tous les réglages sont mis à jour sur une grille fixe de 128 échantillons, avec des rampes échantillon par échantillon (gains de boucle, filtres d'absorption, gains des réflexions, filtres de sortie) : aucun effet d'escalier, et la sortie ne dépend pas de la taille des blocs envoyés par le logiciel (vérifié au bit près).
4. **Même son de 44,1 à 192 kHz.** Filtres audibles conçus « matched » (pôle par invariance impulsionnelle, module exact à 0 Hz et à Nyquist, d'après Vicanek) au lieu de la formule exponentielle seule ou de la transformée bilinéaire : passe-bas des réflexions précoces, passe-bas 10 kHz de Chamber, amortissement de la Plate, filtre de boucle du ressort, étagères aiguës de Jot. Conséquence à 44,1 / 48 kHz : les aigus de la queue durent exactement ce que demande Damp (ex. Hall à 16 kHz : 1,33 s pour 1,35 s voulus ; 1.x : 0,97 s) ; la queue est un peu plus brillante qu'en 1.x à ces fréquences, et identique à 96 / 192 kHz.
5. **Niveaux des modes** : recalés pour garder ceux de la 1.x (énergie de 90 Hz à 11 kHz, source au centre, à ±0,01 dB) : gains Chamber 0,710, Plate 0,692, Spring 1,218, Room 0,522, Hall 0,866, Church 0,878 (1.x : 0,75 / 0,72 / 1,23 / 0,55 / 0,95 / 0,96).
6. **Damp à 0 % sur la Plate** : l'amortissement disparaît progressivement de 5 % à 0 % (1.x : saut entre 0 et 1 %).

### 6.2 Nouveaux réglages (panneau « Advanced settings »)

Identifiants nouveaux (version 2) ; les anciens ne changent ni de nom ni de sens. Une session de la 1.x, qui ne contient pas ces réglages, se rouvre avec leurs valeurs par défaut, qui redonnent le son de la face principale (vérifié). Double-clic : valeur par défaut.

| Réglage | Identifiant | Plage | Défaut | Ce qu'il fait |
|---|---|---|---|---|
| Low cut | `lowcut` | 20 Hz à 1 kHz (échelle log.) | 20 Hz = Off | Passe-haut 12 dB/oct. (Butterworth) sur la réverb seule (le son direct n'est pas filtré). À 20 Hz il est hors circuit : son exact. |
| High cut | `highcut` | 1 à 20 kHz (échelle log.) | 20 kHz = Off | Passe-bas 12 dB/oct. « matched » sur la réverb seule. À 20 kHz hors circuit. Écart au filtre analogique ±0,42 dB jusqu'à 20 kHz pour une coupure de 2 à 15 kHz, à toutes les fréquences d'échantillonnage. |
| Bass | `bass` | ×0.5 à ×2 (échelle log.) | ×1.00 = son actuel | Multiplie le RT60 des graves (sous 200 Hz) : étagère du 2e ordre dans chaque ligne, en plus du caractère grave du mode (Hall ×1,35…) ; le médium (500 Hz, 1 kHz) ne bouge pas (±3 %). Sans effet en Spring (grisé). |
| Early / Late | `erlate` | −100 à +100 % | 0 % = équilibre du mode | Balance réflexions précoces / queue : −100 % = réflexions seules, +100 % = queue seule (diffuseurs + FDN). Sans effet en Plate et Spring (grisé : pas de réflexions séparées). |
| Ducking | `ducking` | 0 à 100 % | 0 % | La réverb baisse pendant que le son direct joue (jusqu'à −30 dB à 100 %, −15 dB à 50 %) : baisse complète au-dessus de −20 dBFS de crête, aucune sous −50 dBFS ; détecteur de crête en dB, attaque 5 ms, retour 100 dB/s (la réverb revient en 0,3 s environ). |
| Freeze | `freeze` | Off / On | Off | Gèle la queue : l'entrée de la réverb se ferme en 20 ms, les boucles deviennent sans perte en 100 ms ; la queue tient tant que Freeze est allumé. Off : elle repart avec le Decay réglé. Pendant Freeze, les changements de mode et de Size attendent la sortie de Freeze. Le logiciel est prévenu d'une queue de 3600 s. |

### 6.3 Interface

- Face principale inchangée. Ajout de la flèche « Advanced settings » du Compressor (`Disclosure`, x 16, y 370, hauteur 28 : chevron aligné sur la marge de 24 px) : fenêtre 286 × 422 repliée, 286 × 666 dépliée ; état mémorisé avec la session, la fenêtre garde son échelle (60 à 200 %).
- Panneau : surface #2A2B29 de 238 × 228 à x 24, y 414 (16 px sous la flèche), coins 12, marge intérieure 16 ; 6 lignes de 26 px (écart 8) à partir de y 430 : Low cut, High cut, Bass, Early / Late, Ducking (curseurs du Compressor : libellé 12 px texte secondaire sur 68 px, valeur 12 px sur 48 px à droite), Freeze (sélecteur Off / On posé sur la surface).
- Couleurs (charte § 4) : filtres Low cut et High cut en teal secondaire ; Bass (temps) et Ducking (action) en orange ; Early / Late (niveau) en teal. Bass et Early / Late ont leur neutre au milieu : la piste se remplit depuis le milieu (repère #757675) ; réglage sans effet dans le mode choisi : grisé (gris doux), toujours réglable.
- Bulles d'aide en anglais simple pour chaque nouveau réglage et pour la flèche.

### 6.4 Latence

Aucune, dans tous les modes et avec tous les réglages (le son direct n'est jamais retardé ; aucun suréchantillonnage nécessaire : pas de non-linéarité dans une réverb).

### 6.5 Mesures avant / après (48 kHz sauf mention ; `tests/quality_test.cpp`)

| Mesure | 1.x | 2.0.0 |
|---|---|---|
| Source à gauche, écart gauche − droite sur toute la réponse (100 premières ms) | 0 dB (entrée mono) | Chamber +7,7 (+9,6) ; Room +7,9 (+8,1) ; Hall +9,0 (+11,3) ; Church +9,7 (+11,9) ; Plate +2,3 (+6,3) dB ; queue après 0,5 s : +0,4 à +2,8 dB ; Spring : identique (mono, voulu) |
| Source au centre, écart gauche − droite | jusqu'à −1,4 dB (Church ; source unique décentrée) | ±0,3 dB |
| Lignes du FDN / lignes modulées | 8 / 4 (Room : 0) | 16 / 16 (tous les modes) |
| RT60 à 500 Hz, réglages des modes, 44,1 à 192 kHz | −4 % à +3 % (48 kHz) | −1,4 % à +4,3 % |
| RT60 à 8 kHz, 192 kHz comparé à 48 kHz | +11 à +16 % (Chamber, Hall, Church) | ±3 % au plus (Room, Chamber, Hall, Church, Plate) |
| Niveau par octave (125 Hz à 16 kHz), 44,1 / 96 / 192 kHz comparés à 48 kHz | jusqu'à 1,4 dB (Plate à 16 kHz) | 0,48 dB au plus (Spring : 0,60) |
| Changement de Size, son continu : creux du son traité (tranches de 20 ms) | −3,0 à −3,2 dB (passage par zéro pendant 12 ms) | −0,1 à −1,8 dB (aucun passage par zéro) |
| Changement de Size 0,5 s après la fin du son : la queue | passe par zéro puis repart dans un moteur aux nouvelles longueurs | identique à sans changement (jusqu'à −120 dBFS) |
| Changement de mode : la queue de l'ancien mode | effacée (remise à zéro) | finit naturellement |
| Indice de clic (sinus 440 Hz ; 1,0 = sinus pur) au changement de Pre-delay 0 → 120 ms | 5,9 (Plate) à 22,9 (Hall) | 1,01 à 1,03 |
| Indice de clic, Decay balayé vers 6 s | 1,5 à 2,0 | 1,00 à 1,13 |
| Indice de clic, 9 réglages balayés (Decay, Damp, Mix, Width, Low cut, High cut, Bass, Early / Late, Ducking) | | 1,13 au plus |
| Bass ×2 / ×0,5 (RT60 à 63 Hz) | (n'existait pas) | ×1,69 à ×1,92 / ×0,50 à ×0,53 ; 500 Hz et 1 kHz à ±3 % |
| Ducking 100 % / 50 % pendant le son ; 0,5 s après la fin | | −30,0 / −15,0 dB ; +0,00 dB |
| Freeze : la queue tient sur 3 s ; part de l'entrée qui entre encore | | ±0,13 dB ; rien (0) |
| Vitesse d'un moteur (deux calculent pendant un changement) | 14 à 87 fois le temps réel à 96 kHz | 11 (Spring) à 67 (Plate) fois à 96 kHz ; 21 fois au moins à 48 kHz |
| Mémoire réservée | | 5,5 Mo à 48 kHz, 21 Mo à 192 kHz (deux moteurs complets) |

### 6.6 Vérifications 2.0.0

- `tests/quality_test.cpp` (nouveau, sans JUCE, seuils chiffrés, renvoie 1 en cas d'échec ; lancé sur GitHub avant la compilation Mac) : 1) stéréo (seuils : ≥ +3 dB sur toute la réponse et ≥ +6 dB sur les 100 premières ms, Plate ≥ +1,5 / +4 dB ; centre ±1 dB ; Spring identique au bit près) ; 2) RT60 à 500 Hz ±5 % (réglages des modes à 44,1 / 48 / 96 / 192 kHz, et trois Decay réalistes par mode) ; 3) niveau par octave ±0,75 dB (Spring ±1 dB) et RT60 par octave ±5 % de 44,1 à 192 kHz ; 4) niveaux des modes à ±0,5 dB de la 1.x ; 5) Size et mode : queue intacte, creux ≥ −3 dB, indice de clic ≤ 1,3, automation continue ; 6) Pre-delay ≤ 1,3 ; 7) High cut ±0,5 dB, Low cut ±0,3 dB, Bass, Early / Late (somme exacte), Ducking, Freeze, 9 réglages balayés ≤ 1,3 ; 8) aucune allocation dans le traitement, sortie identique au bit près avec des blocs de 1 à 4096, extrêmes, vitesse. Tout est conforme (55 s).
- `tests/processor_test.cpp` : en plus de la 1.x, défauts neutres des réglages avancés, textes (Off, ×1.00, −40 %), sauvegarde / rappel des réglages avancés et du panneau ouvert, session de la 1.x rouverte avec les réglages avancés au neutre, choix d'un mode qui ne les touche pas, longueur de queue (Bass, Freeze).
- `tests/verify_dsp.py` : comparaisons au bit près avec le prototype retirées (devenues fausses, voulu) ; gardés : empreinte du prototype, niveau de chaque mode égal à celui du prototype (±0,5 dB), RT60 à 500 Hz (±5 %), sortie indépendante des blocs, vitesse.
- `tests/render_ui.cpp` : captures repliées (6 modes, 60 %, 200 %) et dépliées (Chamber, Plate et Spring avec réglages grisés, valeurs non neutres, 60 %, 200 %).
- pluginval niveau 10 (Linux VST3, avec les tests d'interface).
- Non testé ici : écoute réelle dans un logiciel, Mac (auval et pluginval tournent sur GitHub), Windows sur un vrai PC, AAX dans Pro Tools Developer.

### 6.7 Limites connues

- **Spring** : le ressort « étiré » utilise un facteur entier k = arrondi (fs / 8 800) ; sa limite haute vaut fs / (2k) : 4,41 kHz à 44,1 kHz, 4,8 kHz à 48 kHz, 4,36 kHz à 96 / 192 kHz. Le RT60 de l'octave 4 kHz varie donc jusqu'à 14 % selon la fréquence d'échantillonnage (niveaux par octave à ±0,6 dB). Le corriger demanderait de faire tourner le ressort à une fréquence interne fixe.
- **High cut au-dessus de 15 kHz** à 44,1 / 48 kHz : jusqu'à +0,74 dB d'écart au filtre analogique près de 20 kHz (coupure 19 kHz).
- **Decay très court dans un grand espace** (Hall, Church sous 1 s ; Plate sous 1 s à grande Size) : moins de deux passages dans la boucle, la décroissance n'est plus exponentielle et le RT60 mesuré s'écarte (comme en 1.x).
- **Bass ×2** : le RT60 mesuré sur la réponse entière monte de ×1,7 à ×1,9 à 63 Hz (les réflexions précoces, qui suivent le médium, tirent la mesure vers le bas) ; la boucle elle-même applique bien ×2.
- **Pendant un changement de Size ou de mode**, deux moteurs calculent jusqu'à la fin de la queue sortante : charge de calcul doublée pendant ce temps.
