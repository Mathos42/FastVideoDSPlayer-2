FastVideoDSPlayer 2
===================
> **Licence** : ce dépôt est un fork de [Gericom/FastVideoDSPlayer](https://github.com/Gericom/FastVideoDSPlayer), publié sans licence explicite (code d'origine : tous droits réservés). La licence [zlib](LICENSE) de ce dépôt ne couvre que mes propres modifications et ajouts (anti-rebond, boucle/aléatoire, mode aléatoire sur toute la carte SD, corrections diverses), pas le code original de Gericom.

Lecteur pour le format FastVideoDS.  
Sur votre carte SD avec TWiLight Menu++ d'installé, mettre le fichier FastVideoDS.nds dans "_nds\apps" puis lancez vos vidéos.  

Utilisez [FastVideoDS Encoder](https://www.hiraven.com/FastVideoDS/FastVideoDSEncoder.zip) pour encoder vos vidéos. (https://github.com/Mathos42/FastVideoDSEncoder-2/)  
- Plusieurs grosses corrections et mises à jour effectuées par rapport à la version originale  
  (plus d'info sur mon github FastVideoDSEncoder-2)
  
  ---
## Caractéristiques
- Prise en charge des vidéos longues
- Lecture fluide grâce à l'ajustement de la fréquence de rafraîchissement de l'écran LCD à un multiple entier de la fréquence d'images
- Prise en charge jusqu'à 60 images par seconde sur DSi (environ 30 images par seconde sur DS)
- Utilise le moteur 3D pour la compensation de mouvement
- Charge les données depuis la carte SD et décode l'audio sur le processeur ARM7 tandis que le processeur ARM9 est entièrement disponible pour le décodage vidéo
- Prise en charge d'Argv (à utiliser avec TWiLight Menu++ par exemple)
- Commandes vidéo : lecture/pause et recherche par image clé
- Désactive le rétroéclairage de l'écran inférieur pendant la lecture pour économiser de l'énergie
  
> **Ajouts de ma version :**
- Prise en compte des lettres avec accents dans les noms de répertoires et de fichiers
- Commandes vidéo : vidéo suivante, précédente, enchainement des vidéos dans le répertoire  
- Fonction boucle sur une vidéo
- Lecture aléatoire du répertoire en cours
- Lecture aléatoire dans tous les dossiers de la carte SD
- Mise à jour de la librairie FatFS en R0.16
- Fichier nds rendu autonome, mais continu de fonctionner avec TWiLight Menu++ également
- Affichage des dossiers, choix de la vidéo à regarder (en mode autonome)
- Y X L R SELECT et START fonctionnent également en mode autonome
- L'écran du bas reste noir entre les vidéos et lors de l'appui sur Y X L et R. Appuyer sur l'écran, mettre en pause, sortir de la vidéo rétablissent l'écran du bas
- Inversion des écrans pour la lecture vidéo (si par exemple l'écran du haut ne fonctionne plus).
  
En mode TWiLight Menu++ le nombre de fichiers dans un répertoire est limité à ce que supporte la DS (39 fichiers dans un même répertoire).  
En mode autonome mon code limite à 512 fichiers dans un seul et même répertoire.  

## Contrôles
### Boutons
- A - Lecture/pause
- Dpad gauche - Passer à l'image clé précédente (maintenir enfoncé pour continuer)
- Dpad droit - Passer à l'image clé suivante (maintenir enfoncé pour continuer)
- 
> **Ajouts de ma version :**
- L/Y - Vidéo précédente
- R/X - Vidéo suivante
- B - Retour à la liste des vidéos
- START - Activer/Désactiver la lecture en boucle de la piste
- SELECT - Activer/Désactiver la lecture aléatoire dans le répertoire en cours ou sur toute la carte SD
- Maintenir croix haut et appuyer sur START : les 2 écrans s'inversent.  

### Toucher
L'écran tactile permet de lancer ou de mettre en pause la vidéo, ainsi que de se déplacer dans la vidéo en appuyant ou en faisant glisser la barre de défilement.

## Librairies Utilisées
- [FatFS](http://elm-chan.org/fsw/ff/00index_e.html)

--------------------------------------------------------------------------------------
FastVideoDSPlayer 2
===================
> **License**: this repository is a fork of [Gericom/FastVideoDSPlayer](https://github.com/Gericom/FastVideoDSPlayer), published without an explicit license (original code: all rights reserved). This repository's [zlib license](LICENSE) covers only my own modifications and additions (debounce fix, loop/shuffle, whole-SD-card shuffle mode, various fixes), not Gericom's original code.

A player for the FastVideoDS format.  
On your SD card with TWiLight Menu++ installed, place the FastVideoDS.nds file in the ‘_nds\apps’ folder, then play your videos.  

Use [FastVideoDS Encoder](https://www.hiraven.com/FastVideoDS/FastVideoDSEncoder.zip) to encode your videos. (https://github.com/Mathos42/FastVideoDSEncoder-2/)  
- Several major fixes and updates have been made compared to the original version  
  (more information on my GitHub repository, FastVideoDSEncoder-2)
  
---
## Features
- Supports long videos
- Smooth playback by adjusting the lcd refresh rate to an integer multiple of the frame rate
- Supports up to 60 fps on dsi (~30 fps on ds)
- Uses the 3d engine for motion compensation
- Loads data from the sd card and decodes audio on the arm7 while the arm9 is fully available for decoding video
- Argv support (for use with TWiLight Menu++ for example)
- Video controls: play/pause and keyframe seeking
- Disables the backlight of the bottom screen while playing to save energy 

> **Additions to my version :**
- Support for accented letters in folder and file names
- Video controls: next video, previous video, play videos in sequence from the folder
- Video loop function
- Shuffle playback within the current folder
- Random playback from all folders on the SD card
- FatFS library updated to R0.16
- The nds file is now standalone, but continues to work with TWiLight Menu++ as well
- Folder display and selection of the video to watch (in standalone mode)
- Y, X, L, R, SELECT and START also work in standalone mode
- The bottom screen remains black between videos and when Y, X, L or R are pressed. Tapping the screen, pausing or exiting the video restores the bottom screen
- Swapping the screens for video playback (if, for example, the top screen is no longer working).

PLEASE NOTE ! That in TWiLight Menu++ mode, the number of files in a directory is limited to the maximum supported by the DS (39 files in a single directory).  
In standalone mode, my code limits the number of files in a single directory to 512.  

## Controls
### Buttons
- A – Play/pause
- Left D-pad – Skip to previous keyframe (hold down to continue)
- Right D-pad – Skip to the next keyframe (hold down to continue)

> **Additions to my version :**
- L/Y – Previous video
- R/X – Next video
- B – Return to the video list
- START - Turn track repeat on/off
- SELECT - Turn shuffle on/off/all
- Hold down the cross button and press START: the two screens will swap places.  

### Touch
The touchscreen allows you to play or pause the video, as well as navigate through it by tapping or dragging the scroll bar.

## Libraries Used
- [FatFS](http://elm-chan.org/fsw/ff/00index_e.html)

--------------------------------------------------------------------------------------
