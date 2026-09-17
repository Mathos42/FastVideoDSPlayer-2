FastVideoDSPlayer 2
===================
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
- Commandes vidéo : lecture/pause, vidéo suivante, précédente, lecture automatique de la vidéo suivante dans le répertoire et recherche par image clé
- Désactive le rétroéclairage de l'écran inférieur pendant la lecture pour économiser de l'énergie
- Prise en compte des lettres avec accents dans les noms de répertoires et de fichiers
- Fonction boucle sur une vidéo et lecture aléatoire du répertoire
- Mise à jour de la librairie FatFS en R0.16
- Fichier nds rendu autonome, mais continu de fonctionner avec TWiLight Menu++ également.
- Affichage des dossiers, choix de la vidéo à regarder (en mode autonome).
- Y X L R SELECT et START fonctionnent également en mode autonome.
- L'écran du bas reste noir entre les vidéos et lors de l'appui sur Y X L et R. Appuyer sur l'écran, mettre en pause, sortir de la vidéo rétablissent l'écran du bas (dans les 2 modes, TWiLight Menu++ et autonome).

ATTENTION ! En mode TWiLight Menu++ le nombre de fichiers dans un répertoire est limité à ce que supporte la DS (40 fichiers dans un même répertoire, plus exactement 39 fichiers + le répertoire RETOUR).  
En mode autonome mon code limite à 512 fichiers dans un seul et même répertoire.  

## Contrôles
### Boutons
- A - Lecture/pause
- Dpad gauche - Passer à l'image clé précédente (maintenir enfoncé pour continuer)
- Dpad droit - Passer à l'image clé suivante (maintenir enfoncé pour continuer)
- L/Y - Vidéo précédente
- R/X - Vidéo suivante
- B - Retour à la liste des vidéos
- START - Activer/Désactiver la lecture en boucle de la piste
- SELECT - Activer/Désactiver la lecture aléatoire

### Toucher
L'écran tactile permet de lancer ou de mettre en pause la vidéo, ainsi que de se déplacer dans la vidéo en appuyant ou en faisant glisser la barre de défilement.

## Librairies Utilisées
- [FatFS](http://elm-chan.org/fsw/ff/00index_e.html)

--------------------------------------------------------------------------------------

Pour encoder les vidéos utilisez FastVideoDSEncoder : https://www.hiraven.com/FastVideoDS/FastVideoDSEncoder.zip  
Fichiers .bat pour :  
Encoder une ou plusieurs vidéos : https://www.hiraven.com/FastVideoDS/FastVideoDS.bat  
Encoder tout un répertoire de vidéos : https://www.hiraven.com/FastVideoDS/Encodage_repertoire.bat  

--------------------------------------------------------------------------------------
FastVideoDSPlayer 2
===================
A player for the FastVideoDS format.  
On your SD card with TWiLight Menu++ installed, place the FastVideoDS.nds file in the ‘_nds\apps’ folder, then play your videos.  

Use [FastVideoDS Encoder](https://www.hiraven.com/FastVideoDS/FastVideoDSEncoder.zip) to encode your videos. (https://github.com/Mathos42/FastVideoDSEncoder-2/)  
- Several major fixes and updates have been made compared to the original version  
  (more information on my GitHub repository, FastVideoDSEncoder-2)
  
---
## Features
- Support for long videos
- Smooth playback by adjusting the LCD refresh rate to an integer multiple of the frame rate
- Supports up to 60 frames per second on the DSi (approximately 30 frames per second on the DS)
- Uses the 3D engine for motion compensation
- Loads data from the SD card and decodes audio on the ARM7 processor, whilst the ARM9 processor is fully available for video decoding
- Support for Argv (for use with TWiLight Menu++, for example)
- Video controls: play/pause, next video, previous video, auto-play next video in the folder and keyframe search
- Disables the lower screen’s backlight during playback to save power
- Handling accented letters in directory and file names
- Loop function for a video and random playback of the folder
- FatFS library updated to R0.16
- The NDS file has been made standalone, but it still works with TWiLight Menu++ as well.
- Displays folders and allows you to select which video to watch (in standalone mode).
- Y, X, L, R, SELECT and START also work in standalone mode.
- The bottom screen remains black between videos and when Y, X, L and R are pressed. Tapping the screen, pausing the video or exiting the video restores the bottom screen (in both modes: TWiLight Menu++ and standalone).

PLEASE NOTE ! That in TWiLight Menu++ mode, the number of files in a directory is limited to the maximum supported by the DS (40 files in a single directory – or, to be precise, 39 files plus the ‘RETURN’ directory).  
In standalone mode, my code limits the number of files in a single directory to 512.  

## Controls
### Buttons
- A – Play/pause
- Left D-pad – Skip to previous keyframe (hold down to continue)
- Right D-pad – Skip to the next keyframe (hold down to continue)
- L/Y – Previous video
- R/X – Next video
- B – Return to the video list
- START - Turn track repeat on/off
- SELECT - Turn shuffle on/off


### Touch
The touchscreen allows you to play or pause the video, as well as navigate through it by tapping or dragging the scroll bar.

## Libraries Used
- [FatFS](http://elm-chan.org/fsw/ff/00index_e.html)

--------------------------------------------------------------------------------------

To encode videos, use FastVideoDSEncoder: https://www.hiraven.com/FastVideoDS/FastVideoDSEncoder.zip  
.bat files for :  
Encoding one or multiple videos : https://www.hiraven.com/FastVideoDS/FastVideoDS.bat  
Encoding an entire folder of videos : https://www.hiraven.com/FastVideoDS/Encodage_repertoire.bat  

--------------------------------------------------------------------------------------
