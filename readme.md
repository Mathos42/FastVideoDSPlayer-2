FastVideoDSPlayer 2
===================
Lecteur pour le format FastVideoDS. Sur votre carte SD avec TWiLight Menu++ d'installé, mettre le fichier FastVideoDS.nds dans "_nds\apps" puis lancez vos vidéos. Utilisez [FastVideoDS Encoder](https://www.hiraven.com/FastVideoDS/FastVideoDSEncoder.zip) pour encoder vos vidéos.

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
A player for the FastVideoDS format. On your SD card with TWiLight Menu++ installed, place the FastVideoDS.nds file in the ‘_nds\apps’ folder, then play your videos. Use [FastVideoDS Encoder](https://www.hiraven.com/FastVideoDS/FastVideoDSEncoder.zip) to encode your videos.

## Features
-    Support for long videos
-    Smooth playback by adjusting the LCD refresh rate to an integer multiple of the frame rate
-    Supports up to 60 frames per second on the DSi (approximately 30 frames per second on the DS)
-    Uses the 3D engine for motion compensation
-    Loads data from the SD card and decodes audio on the ARM7 processor, whilst the ARM9 processor is fully available for video decoding
-    Support for Argv (for use with TWiLight Menu++, for example)
-    Video controls: play/pause, next video, previous video, auto-play next video in the folder and keyframe search
-    Disables the lower screen’s backlight during playback to save power
-    Handling accented letters in directory and file names

## Controls
### Buttons
-    A – Play/pause
-    Left D-pad – Skip to previous keyframe (hold down to continue)
-    Right D-pad – Skip to the next keyframe (hold down to continue)
-    L/Y – Previous video
-    R/X – Next video
-    B – Return to the video list
-    START - Turn track repeat on/off
-    SELECT - Turn shuffle on/off


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
