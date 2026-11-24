<p align="center"><img src="resources/zebu/zebu_logo.png" width="260" alt="ZEBU Slicer"></p>

# ZEBU Slicer — Beta 0.1.0

Slicer FDM, fork d'[OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) **v2.5.2**, distribué sous licence **AGPL-3.0**
(voir `LICENSE.txt` ; le README d'origine d'OrcaSlicer est dans `docs/README_OrcaSlicer.md`).

## Ce que ZEBU change par rapport à OrcaSlicer

| Point | Ce qui est fait en 0.1.0 |
|---|---|
| **Presets par machine** | Un filament ou un process utilisateur n'est proposé que pour les imprimantes de sa liste `compatible_printers`. « Enregistrer sous » le rattache à l'imprimante courante. Option *Préférences → ZEBU Slicer*. |
| **Pas de profils imposés** | Seuls `Custom` (modèles pour « Créer une imprimante ») et `OrcaFilamentLibrary` (parents des filaments) sont livrés (94 Mo → 6,6 Mo). Les presets système sont masqués des listes. L'assistant constructeurs est remplacé par « Créer une imprimante / un filament ». |
| **Import depuis OrcaSlicer** | *Fichier → Importer → Importer depuis OrcaSlicer…* (et au premier lancement). L'héritage des presets Orca est résolu à partir du dossier `system` d'Orca, puis les presets sont écrits complets, sans parent. Chaque filament ou process est rattaché à la machine choisie, avec une copie « Nom @Machine » par machine si besoin. |
| **ZEBU Live** | Panneau sous la sélection d'imprimante (onglet Préparer) : caméra (snapshot Crowsnest), températures buse, plateau et chambre, état, progression, temps restant, couche. Il utilise l'hôte Moonraker de l'imprimante (bouton *Connexion* : `print_host`, clé API ou utilisateur et mot de passe). |
| **Mises à jour** | Vérification au démarrage sur les GitHub Releases du dépôt `ZEBU_UPDATE_REPO`. Windows : téléchargement, contrôle SHA256, installeur silencieux dans le même dossier, redémarrage. Linux : remplacement de l'AppImage sur place. |
| **Installeur Windows** | NSIS : choix de la langue au début, `Program Files\ZEBU Slicer` par défaut et modifiable (autre disque), désinstalleur. La langue reste modifiable dans les Préférences. |
| **Linux** | AppImage `ZEBU_Slicer_Linux_V<version>.AppImage`. Langue choisie au premier lancement, puis modifiable dans les Préférences. |
| **Cohabitation avec Orca** | Exécutable `zebu-slicer`, dossier de configuration séparé (`%APPDATA%\ZEBUSlicer`, `~/.config/ZEBUSlicer`). Les projets 3MF d'Orca s'ouvrent normalement : la version interne reste celle d'Orca 2.5.2. |
| **Réseau** | Mode furtif par défaut : pas de synchronisation Orca Cloud ou Bambu. Seule la vérification des mises à jour ZEBU sort sur Internet. |

Prévu en **0.2.0** : export G-code parallélisé (aujourd'hui toutes les étapes de `GCode::process_layers` sont `serial_in_order`), profilage sur 32 threads.

## Compiler

### Windows (Visual Studio 2022, CMake, NSIS 3, Git, Strawberry Perl)
```bat
build_win.bat -d                     :: dépendances (une seule fois, long)
build_win.bat -s --build-dir build   :: ZEBU Slicer
powershell -ExecutionPolicy Bypass -File packaging\windows\build_installer.ps1 -BuildDir build
```
→ `build\ZEBU_Slicer_Windows_Installer_V0.1.0-beta_x64.exe` (+ `.sha256`)

### Linux (AppImage)
```bash
./build_linux.sh -u      # paquets système (sudo)
./build_linux.sh -dsi    # dépendances + ZEBU + AppImage
```
→ `build/ZEBU_Slicer_Linux_V0.1.0-beta.AppImage`

Sur Fedora, le plus simple est le conteneur Ubuntu 24.04 fourni : `./build_linux.sh -g -dsi`.

### Tests des modules ZEBU (sans compilation complète)
```bash
tests/zebu/run_tests.sh /chemin/vers/OrcaSlicer/resources/profiles
```

## Publier une version (avec mise à jour automatique)
1. Créez le dépôt GitHub, puis mettez son nom dans `version.inc` (`ZEBU_UPDATE_REPO "compte/ZEBU-Slicer"`), ou passez `-DZEBU_UPDATE_REPO=compte/ZEBU-Slicer` à CMake.
2. Changez `ZEBU_VERSION` dans `version.inc`.
3. Poussez un tag `v0.1.0-beta` : le workflow *ZEBU build* compile l'installeur Windows et l'AppImage, puis crée la release (pré-release si le tag contient un « - »). Les installations existantes la proposent au démarrage suivant.

## Réglages (fichier `ZEBUSlicer.conf`, section `app`)
`zebu_strict_presets`, `zebu_hide_system_presets`, `zebu_live_enabled`, `zebu_live_camera_interval_ms` (1000 par défaut), `zebu_check_updates`, `zebu_update_prerelease`.

## Crédits
ZEBU Slicer repose sur le travail d'OrcaSlicer (SoftFever et ses contributeurs), de Bambu Studio, de PrusaSlicer, de SuperSlicer et de Slic3r.
