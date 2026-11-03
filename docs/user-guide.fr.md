# Joué Revival : guide de l'utilisateur

*[English](user-guide.md) · Français*

Ce guide couvre la fenêtre principale. Pour dessiner des dispositions, les imprimer et écrire des puces, voir le [Guide du concepteur de modules](module-designer.fr.md).

## La fenêtre

- **En haut : Instrument en direct.** Le Joué tel qu'il est maintenant : ses trois emplacements, les modules qui s'y trouvent, et les contacts. Il indique *Joué déconnecté* quand l'instrument n'est pas là. **Masquer** le ferme ; **Affichage > Instrument en direct** le ramène.
- **À gauche : la bibliothèque.** Dossiers et préréglages. Les préréglages d'usine ont un cadenas.
- **Au centre : le module.** Son image (cliquez sur un élément pour le modifier) et, pour un préréglage qui convient à un module de l'instrument, le panneau d'envoi.
- **À droite : Réglages et Affectation MIDI.** Les réglages du module, et ceux de l'élément sur lequel vous avez cliqué.

Menus : **Fichier** (Nouvelle bibliothèque, Ouvrir..., Ouvrir depuis un chemin..., Concepteur de modules..., Enregistrer, Exporter une copie..., Quitter), **Modifier** (Annuler, Rétablir), **Affichage** (Connexion à l'appareil, Informations sur l'appareil, Concepteur de modules, Instrument en direct, Language / Langue, Toucher pour sélectionner, Écouter le port Play), **Aide** (Démarrage rapide, Guide de l'utilisateur, Guide du concepteur de modules, mises à jour).

## La bibliothèque

Ouvrez la bibliothèque d'usine de l'éditeur d'origine avec **Fichier > Ouvrir...** (elle ne fait pas partie de ce programme). La première fois que vous appuyez sur **Enregistrer**, votre propre `library.xml` est créé dans le dossier de données ; chaque enregistrement garde le fichier précédent dans un dossier `backups`, donc aucune version n'est perdue.

- **Les préréglages d'usine sont verrouillés** (cadenas). Ils ne peuvent pas être supprimés, et leurs réglages sont grisés. **Déverrouiller pour modifier** en déverrouille un pour cette session ; **Dupliquer** en fait une copie qui est à vous. Play Audio est verrouillé comme les autres, bien que la bibliothèque d'origine le laisse déverrouillé.
- **Nouveau dossier, Dupliquer, Monter, Descendre, Supprimer** agissent sur l'élément sélectionné. Un dossier qui contient un préréglage verrouillé ne peut pas non plus être supprimé.
- **Annuler et Rétablir** (Ctrl+Z, Maj+Ctrl+Z) couvrent chaque modification.

## Modifier les réglages d'un module

Cliquez sur un élément de l'image pour voir son **Affectation MIDI** à droite.

- **Les boutons et les pads** ont une **Action** (Note, Choix du canal, Sustain, Vibrato, Aftertouch, Contrôle Y, Glissando, Changement de contrôle, Changement de programme, Pitch bend) ; les options qui suivent dépendent de l'Action choisie.
- **Les plages** ont une **Direction** (Naturel ou Inversé ; vers le haut ou vers le bas pour un pitch bend) qui garde la plage quand vous la changez.
- **Touches et pads, Affectation personnalisée désactivée :** un clic modifie le *groupe* (ALL_KEYS, ALL_PADS) : son canal, sa vélocité et son contrôle Y s'appliquent à toutes les touches à la fois, et sur le Scaler et Play Keys il a la **Première note** et la **Gamme**.
- **Affectation personnalisée activée :** un clic modifie cette touche ou ce pad seul. Cocher ou décocher Affectation personnalisée fait suivre la sélection.
- **Gammes :** les 23 de l'éditeur d'origine, puis 47 autres sous des intertitres (mineure mélodique et ses modes, mineure harmonique, majeure harmonique, pentatoniques et hexatoniques, japonaises et chinoises, orientales et populaires, bebop et symétriques). Une gamme et une première note réécrivent les notes des 17 touches ; des notes qui ne suivent aucune gamme forment une affectation personnalisée.
- **Plage de pression** (Basse ou Haute) règle d'un coup la profondeur de chaque pad, touche, corde et bouton.

## L'instrument en direct

1. **Quittez d'abord l'éditeur Joué d'origine** : il garde les ports MIDI de l'instrument.
2. Branchez le Joué. Les modules apparaissent dans leurs emplacements avec l'image de leur disposition. Un module de trois emplacements répond depuis l'emplacement 1 seulement, un de deux emplacements depuis l'emplacement 1 ou 2.
3. **Les contacts** sont dessinés d'après les notes, la pression et les contrôleurs que joue l'instrument ; ce ne sont pas des positions mesurées. Les boutons **octave** sur l'image corrigent l'octave que suppose le programme.
4. Si quelque chose semble faux, appuyez sur **Réanalyser les modules**, et ouvrez **Détails** puis son **Journal de détection** : ils disent ce que la carte a annoncé, ce qui a été lu pour chaque emplacement et pourquoi une lecture a été refusée.
5. **Toucher pour sélectionner** (menu Affichage, activé par défaut) : posez un doigt sur l'instrument et l'éditeur sélectionne cet élément, comme un clic dessus, dans le module ouvert quand il est du même type que le module touché (un préréglage Scaler, ou un Scaler ouvert avec Modifier). Toucher la disposition temporaire du concepteur sélectionne l'élément dans le concepteur. Cela demande le port Play : voir le point suivant.
6. **Affichage > Écouter le port Play (contacts)** désactivé : le programme ne garde plus le port Play (utilisez-le si un moniteur MIDI ou un synthétiseur ne reçoit plus les notes), et aucun contact n'est dessiné.

## Modifier le module qui est dans l'instrument

Appuyez sur **Modifier** sur un module de l'image en direct. Il s'ouvre comme une copie dans le dossier *Depuis l'instrument*, remplie de ce que contient le module. Seuls les réglages qui peuvent être écrits dans ce module sont montrés.

- **Envoyer vers le Joué...** liste les réglages que vous avez changés et écrit seulement ceux-là. La disposition, le type et la couleur du module restent.
- **Restaurer l'original...** réécrit la disposition telle qu'elle était à la lecture. **Annuler mes modifications** abandonne vos changements sans toucher le module.
- L'original est sauvegardé comme fichier dans le dossier de données (`module-backups`) avant que quoi que ce soit soit écrit.

## Envoyer un préréglage à un module

Un préréglage a un panneau **Envoyer vers le Joué** quand un module du même type, de la même version et de la même révision est dans l'instrument. La confirmation dit si le préréglage convient, et liste les réglages qui vont changer. **Restaurer la disposition précédente...** annule le dernier envoi.

## Informations sur l'appareil

**Affichage > Informations sur l'appareil...** montre ce que l'instrument dit de lui-même, sans rien écrire : l'identifiant matériel et la famille de micrologiciel qu'il prend, la version et le build du micrologiciel, le processeur tactile, le numéro de série de la carte, l'identifiant de la puce de chaque emplacement, les modules annoncés, et les ports MIDI utilisés. **Relire** lit de nouveau ; **Copier** met le tout dans le presse-papiers, toujours en anglais, pour l'envoyer avec une question ou un rapport de bogue. Une carte qui ne répond pas est montrée comme *sans réponse*, jamais comme des zéros.

## Comparer

- **Comparer...** (à côté du nom d'un préréglage) liste les réglages qui diffèrent entre ce préréglage et un autre choisi dans la bibliothèque.
- **Comparer avec l'emplacement N...** (dans le panneau d'envoi, quand un module qui convient est dans l'instrument) liste ce qu'un envoi changerait dans ce module. C'est la liste de la confirmation d'envoi, et **rien n'est écrit**.

## Aide et mises à jour

- **Aide > Démarrage rapide** (aussi **F1**) montre les six premiers pas. La première fois que le programme démarre, il s'ouvre tout seul sous le nom *Bienvenue* ; décochez **Afficher cette fenêtre au démarrage** pour l'arrêter.
- **Aide > Guide de l'utilisateur** et **Guide du concepteur de modules** ouvrent le guide dans votre langue : le fichier qui est à côté du programme, ou celui en ligne quand il n'y est pas.
- **Mises à jour.** Une fois par jour, au démarrage, le programme demande à GitHub la dernière version du projet et, quand elle est plus récente que celle qui tourne, affiche **La version ... est disponible** à droite de la barre de menus ; un clic ouvre la page de la version. Il n'envoie rien d'autre que cette demande (GitHub voit votre adresse, comme pour toute page web). **Aide > Rechercher les mises à jour au démarrage** l'arrête, **Vérifier maintenant** demande aussitôt, et `--no-update-check` la saute pour un démarrage. Il faut `curl` sous macOS et Linux (installé sur la plupart des systèmes) ; sous Windows il utilise la bibliothèque du système. Il ne télécharge ni n'installe jamais rien.

## Langue

L'interface est en anglais ou en français. Le programme démarre dans la langue de votre système ; **Affichage > Language / Langue** la change aussitôt, et le choix est gardé (`language=` dans `ui.txt`). `--lang en` ou `--lang fr` sur la ligne de commande la règle pour ce seul démarrage. Les noms qui viennent d'une bibliothèque (les noms des préréglages et des dossiers) ne sont pas traduits.

## Fichiers

Le dossier de données est `%APPDATA%\JoueEditorNG` sous Windows, `~/Library/Application Support/JoueEditorNG` sous macOS et `$XDG_DATA_HOME/JoueEditorNG` (ou `~/.local/share/JoueEditorNG`) sous Linux.

| Fichier ou dossier | Ce que c'est |
|---|---|
| `library.xml` | Votre bibliothèque. |
| `backups/` | La bibliothèque précédente à chaque enregistrement. |
| `module-backups/` | La disposition d'un module avant chaque envoi. |
| `graphics.log` | Ce que le pilote graphique a indiqué au dernier démarrage. |
| `ui.txt` | Les quelques choix gardés d'un démarrage à l'autre (Écouter le port Play, Toucher pour sélectionner, la langue, la fenêtre de bienvenue, la recherche de mises à jour et la date de la dernière). |

## Ligne de commande

`joue_editor [--library FICHIER] [--data DOSSIER] [--select MODULE] [--designer FICHIER] [--scale S] [--safe-graphics] [--no-touches] [--lang en|fr] [--no-update-check]`

`--scale 1.5` agrandit tout, `--safe-graphics` dessine l'instrument sans textures (pour les anciens pilotes et les machines virtuelles), `--no-touches` démarre sans écouter le port Play, `--designer FICHIER` ouvre le concepteur de modules sur une disposition.

## Dépannage

| Problème | Que faire |
|---|---|
| Zones noires dans la fenêtre | Démarrer avec `--safe-graphics` (Windows : `JoueRevival-safe-graphics.bat`). |
| Pas de *Connecté* | Quitter l'éditeur d'origine ; débrancher et rebrancher le câble USB ; regarder la ligne des ports sous **Détails**. |
| Un module manque ou est dans le mauvais emplacement | **Réanalyser les modules** ; lire le **Journal de détection**. |
| Un autre programme ne reçoit pas les notes | Désactiver **Affichage > Écouter le port Play**. |
| macOS dit que l'application est endommagée ou d'un développeur inconnu | `xattr -cr "Joue Revival.app"`, ou clic droit, **Ouvrir**. |
| Windows SmartScreen avertit | **Informations complémentaires**, **Exécuter quand même** : le fichier n'est pas signé. |
