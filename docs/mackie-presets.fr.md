# Préréglages Mackie Control pour les modules

`presets/mackie-presets.xml` et `presets/scaler-mixer.xml` contiennent un préréglage Mackie Control pour chaque sorte de module, tous nommés **Mackie Control: <module>**. Ils sont intégrés à la bibliothèque : le dossier **Mackie presets** est juste sous les préréglages Play et est ouvert à chaque démarrage. Ils sont verrouillés : **Dupliquer** fait une copie modifiable. Sélectionnez-en un, mettez le module sur l'instrument et envoyez-le.

## Ce que fait chacun

| Préréglage | Ce que sont ses commandes |
|---|---|
| Mackie Control: Scaler | la surface de mixage du Scaler : master et 8 faders, fin, maj, stop, retour rapide, avance rapide, solo, mute, armement, boucle, punch, annuler, rétablir, lecture, enregistrement, panoramique et banque (voir mixer-bridge.fr.md) |
| Mackie Control: Play Keys | la même, pour le Play Keys |
| Mackie Control: Matrix | les deux rangées de pads les plus proches de vous (les plus basses sur l'image) sont mute 1 à 8, les deux plus éloignées solo 1 à 8 |
| Mackie Control: Strips | faders 1 à 4 (pitch bend, canaux 1 à 4) |
| Mackie Control: Rounds | faders 5 à 8 (pitch bend, canaux 5 à 8) : avec les Strips, huit faders |
| Mackie Control: Bubbles | les quatre boutons sont lecture, stop, enregistrement et cycle |
| Mackie Control: Grand Clavier, Mackie Control: Play Piano | les touches sont armement, solo et mute de 8 pistes (armement 1, armement 5 et la dernière touche, sélection 1, sont désactivés, voir plus bas) ; le curseur est la molette de jog, comme sur le Scaler ; les deux boutons sont banque à gauche et à droite |
| Mackie Control: Synth | les touches sont transport et navigation à partir de la note 89 (métronome, retour rapide, avance rapide, stop, lecture, enregistrement, les curseurs...) ; les quatre boutons sont lecture, stop, enregistrement et cycle |
| Mackie Control: Play Drum | les pads 1 à 6 sont mute 1 à 6, les pads 7 à 12 solo 1 à 6 ; les deux boutons sont lecture et enregistrement |
| Mackie Control: Play Audio | lecture, enregistrement, les trois boutons de préréglage comme sélection 1 à 3, le bouton d'effet comme cycle |

Les notes sont sur le canal 1 avec une vélocité de 127 et les boutons s'appuient et se relâchent comme des boutons Mackie. Les numéros sont ceux du protocole Mackie Control : armement 0 à 7, solo 8 à 15, mute 16 à 23, sélection 24 à 31, banque à gauche et à droite 46 et 47, cycle 86, retour rapide 91, avance rapide 92, stop 93, lecture 94, enregistrement 95.

Ensemble, Strips, Rounds et Matrix sur les trois emplacements font une surface de huit faders avec mute et solo.

## Les claviers : touches désactivées et curseur

Sur le Grand Clavier et le Play Piano, Mackie Control désactive trois touches : armement 1 et armement 5, car l'armement ne convient pas à tous les types de pistes, et sélection 1. Tant que Mackie Control tourne, une touche désactivée n'envoie rien et est dessinée en sombre dans sa fenêtre ; sans Mackie Control les touches envoient toujours leurs notes. Le curseur envoie son contrôleur 83 et Mackie Control en fait la molette de jog (contrôleur 60), exactement comme pour la bande du Scaler ; il est marqué JOG dans la fenêtre.

## La fenêtre et le port

Quand un module qui utilise un préréglage Mackie Control est posé sur l'instrument, le programme active Mackie Control, **ouvre la fenêtre Mackie Control** et **ouvre le port MIDI** `Joue Mackie` que voit le DAW. Pour le Scaler et le Play Keys la fenêtre est la surface de mixage décrite dans mixer-bridge.fr.md. Pour les autres modules c'est **le module lui-même** : chaque commande porte ce qu'elle fait (MUTE 3, SOLO 5, FADER 2...), est allumée tant qu'elle est pressée, et montre les voyants et les faders que signale le DAW (une piste en mute, en solo ou armée, l'état de lecture). Ce que le module envoie est transmis au DAW sans changement, avec les réponses à la poignée de main du DAW. On peut le désactiver dans la fenêtre.

## Quel module en utilise un, et revenir en arrière

Dans l'instrument en direct, un module qui utilise un préréglage Mackie Control porte une marque rouge **Mackie Control preset** à côté de **Modifier** ; elle ouvre une fenêtre rouge, une fois quand le module est vu pour la première fois puis quand vous cliquez sur la marque, qui dit quel préréglage il utilise. **Pour revenir au préréglage d'origine, rechargez-le** : sélectionnez le préréglage d'origine de ce module (*Pro Scaler*, *Pro Matrix*...) dans la bibliothèque et envoyez-le. Faites-le aussi avant de mettre un autre préréglage Mackie sur le module : écrire un préréglage Mackie sur un module qui en utilise déjà un ne donne pas toujours le module que ce préréglage décrit (les notes des touches ou des pads peuvent revenir à leurs valeurs habituelles).

## Ce qui a été vérifié

Les préréglages du **Scaler**, du **Pro Matrix** et du **Pro Grand Clavier** ont été écrits dans de vraies données de module et relus à l'identique. Les autres n'ont été vérifiés que par rapport aux tables du programme (chaque réglage est connu pour ce module et a une valeur permise) : ils n'ont **pas** été essayés sur un vrai module, et la fenêtre le dit. Merci de dire ce que vous constatez.

## Non inclus

Les Fretboards et la Guitar : une corde n'envoie que des notes, donc rien d'utile pour un mixage. Le Pro Area n'a pas d'équivalent Mackie direct. Les modules Play ne peuvent être écrits que par une carte Pro.
