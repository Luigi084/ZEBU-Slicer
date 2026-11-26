# Journal des versions de ZEBU Slicer

ZEBU Slicer, par Luigi Laplace : fork d'OrcaSlicer 2.5.2, sous licence AGPL-3.0.

## [Non publié]

### Dessus courbes (non planaire), expérimental
- Les dessus en pente douce peuvent être imprimés en suivant leur forme, sans marches d'escalier :
  option « Dessus courbes (ZEBU) » de l'onglet Qualité du traitement, décochée par défaut. Décochée, le
  G-code est celui de la 0.2.0-beta.
- La tête de l'imprimante est décrite par le dessous de son modèle CAO (G3D Pro 360, Raise K2Plus,
  Zortrax M200k fournis), choisi dans les paramètres de l'imprimante. Une surface n'est imprimée courbe
  que si la tête, avec sa marge, ne peut toucher ni la pièce ni les autres objets du plateau ; sinon elle
  reste plane.
- Les couches planes s'arrêtent sous la surface ; les dernières couches (4 par défaut) suivent sa forme,
  la plus basse épousant les marches des couches planes (son débit suit son épaisseur).
- Menu ZEBU → « Analyse des dessus courbes » (et clic droit sur un objet) : montre en vert les surfaces
  imprimables en courbe, en orange les autres.
- Pas encore disponible en mode vase, sur les imprimantes à tapis, avec le contournage Z, ni pour un
  objet imprimé avec plusieurs filaments.

## [0.2.0-beta] - 2026-11-25

### Vitesse
- Export du G-code accéléré : les contours de l'option « Éviter de traverser les parois » et l'index des
  contours de chaque couche sont construits à l'avance, en parallèle, au lieu de l'être dans le thread
  unique de la génération des couches.
- Mesures sur un Threadripper 2950X (32 threads), trois projets réels, médiane de 3 passes :

  | Projet | OrcaSlicer 2.5.2 | ZEBU 0.2.0-beta | Gain |
  |---|---|---|---|
  | Grosse pièce (1 144 couches, supports) | 38,3 s | 28,1 s | +36 % |
  | Plateau rempli (26 pièces) | 31,7 s | 23,8 s | +33 % |
  | Petite pièce | 2,9 s | 2,4 s | +19 % |

  Le G-code produit est identique à celui d'OrcaSlicer (même empreinte, commentaires exclus).
- Windows compilé avec clang-cl, comme OrcaSlicer : avec MSVC, le tranchage était environ 15 % plus lent.

### Mesure
- Chronométrage de chaque étape du tranchage et de l'export : lignes « ZEBU timing » dans le journal, et
  fichier choisi par la variable `ZEBU_TIMINGS`.
- `ZEBU_MAX_THREADS=<n>` limite le nombre de threads de l'intérieur, pour mesurer la montée en charge.
- Banc de comparaison Orca / ZEBU : `scripts/zebu/bench/zebu_bench.ps1`.

### Presets
- Une imprimante enregistrée sous un nouveau nom propose de **renommer** (ses filaments, process et
  connexion passent sur le nouveau nom, l'ancienne est supprimée), de **partager** ces presets entre les
  deux, ou de ne rien faire.

### Divers
- La fenêtre de dépannage affiche la version de ZEBU.
- Développement : cache de compilation dans le CI (un build passe d'environ 1 h à 10 min).

## [0.1.0-beta] - 2026-11-24

Première version.
- Presets rattachés à leur machine : un filament ou un process n'apparaît que pour ses imprimantes.
- Pas de profils imposés : presets système masqués, imprimantes et filaments créés par l'utilisateur.
- Import des imprimantes, filaments et process depuis OrcaSlicer.
- Panneau ZEBU Live : caméra, températures, état et progression de l'imprimante (Moonraker), dans
  l'onglet Préparer.
- Installeur Windows (langue au choix, dossier modifiable, mise à jour intégrée) et AppImage Linux.
- Thème orange, logo et écran de démarrage ZEBU.
- Pas de code Orca Cloud ni Bambu actif (`ZEBU_NO_CLOUD`) : seule la vérification des mises à jour ZEBU
  sort sur Internet.
