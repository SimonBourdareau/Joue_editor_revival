# Mackie Control

Joué Revival peut faire de l'instrument une surface de contrôle pour un DAW qui parle **Mackie Control** (Bitwig Studio, Ableton Live, Reaper, Logic...) : faders, transport, solo, mute, armement, panoramique, banques et noms de pistes. C'est un **aperçu** : les préréglages du Scaler et du Grand Clavier ont été essayés avec Bitwig sous Windows ; les autres n'ont pas été essayés sur un vrai module.

## Démarrage rapide

1. Dans la bibliothèque, ouvrez le dossier **Mackie presets** (sous les préréglages d'usine Play), choisissez *Mackie Control: <votre module>*, posez le module sur l'instrument et appuyez sur **Envoyer vers le Joué...**. Les préréglages sont verrouillés ; **Dupliquer** en fait une copie modifiable.
2. Le programme active alors Mackie Control, ouvre sa fenêtre et ouvre le port MIDI **Joue Mackie**. À la main : **Affichage > Mackie Control...**, puis **Connecter**.
3. Dans le DAW, ajoutez un contrôleur Mackie Control et réglez son entrée et sa sortie MIDI sur **Joue Mackie**. Dans Bitwig : **Settings > Controllers > Add controller > Mackie > Mackie Control**. Faites-le après l'étape 2 : le DAW ne liste le port qu'une fois qu'il existe.
4. La fenêtre montre ce que le module envoie et ce que répond le DAW ; les lignes en orange viennent du DAW.

**Ports.**

- **Windows :** installez **loopMIDI** une fois (gratuit pour un usage personnel : [tobias-erichsen.de/software/loopmidi.html](https://www.tobias-erichsen.de/software/loopmidi.html)), puis relancez Joué Revival. Le programme crée son port avec le pilote qu'installe loopMIDI : ne créez aucun port dans loopMIDI. Le pilote n'est pas fourni ; s'il manque, la fenêtre le dit.
- **macOS, Linux :** rien à installer (les ports virtuels du système ; non essayés).
- Pour utiliser des ports qui existent déjà (loopMIDI, IAC), choisissez **Utiliser des ports qui existent déjà (loopMIDI, IAC...)** et indiquez **Envoyer vers** et **Écouter**.

## Les préréglages

| Préréglage | Commandes |
|---|---|
| Scaler, Play Keys | une surface de mixage : master et 8 faders, solo, mute, armement, transport, panoramique, banques (voir plus bas) |
| Matrix | les deux rangées de pads les plus proches de vous : mute 1 à 8 ; les deux rangées plus loin : solo 1 à 8 |
| Strips, Rounds | faders 1 à 4 et 5 à 8 (pitch bend, canaux 1 à 8) |
| Bubbles | quatre boutons : lecture, stop, enregistrement, cycle |
| Grand Clavier, Play Piano | touches : armement, solo et mute de 8 pistes ; curseur : molette de jog ; deux boutons : banque à gauche et à droite. Armement 1, armement 5 et la dernière touche (sélection 1) sont désactivés : ils n'envoient rien tant que Mackie Control tourne |
| Synth | touches à partir de la note 89 : transport et navigation ; quatre boutons : lecture, stop, enregistrement, cycle |
| Play Drum | pads 1 à 6 : mute 1 à 6 ; pads 7 à 12 : solo 1 à 6 ; boutons : lecture et enregistrement |
| Play Audio | lecture, enregistrement, les trois boutons de préréglage comme sélection 1 à 3, effet comme cycle |

Il n'y a pas de préréglage pour les Fretboards et la Guitare (les cordes n'envoient que des notes) ni pour la Pro Area. Un module Play ne peut être écrit que par une carte Pro.

Un module qui en utilise un porte une étiquette rouge **Préréglage Mackie Control** à côté de **Modifier**. **Pour revenir en arrière, envoyez de nouveau le préréglage d'origine du module** (*Pro Scaler*, *Pro Matrix*...). Faites de même avant de poser un autre préréglage Mackie dessus : écrire un préréglage Mackie par-dessus un autre ne donne pas le même module que par-dessus l'original.

## Le Scaler comme table de mixage

| Commande | Fait |
|---|---|
| Touche 1 (la plus à gauche) | fader master |
| Touches 2 à 9 | faders 1 à 8 : la position du doigt le long de la touche est le fader |
| Touche 10, **FINE** | maintenue, un fader bouge quatre fois moins |
| Touche 11, **SHIFT** | maintenue, les touches 12 à 17 donnent leur seconde fonction |
| Touches 12 à 17 | stop, retour rapide, avance rapide, solo, mute, armement de la piste sélectionnée. Avec maj : boucle, punch, annuler, rétablir, mode des faders, clic |
| Boutons 1 et 2 (à droite) | lecture et enregistrement |
| Bulle | gauche et droite : panoramique de la piste sélectionnée ; haut et bas : banque de pistes précédente et suivante |
| Bande | molette de jog |

La **piste sélectionnée** est le dernier fader touché, sinon la piste que le DAW a sélectionnée. Une touche n'a pas de moteur : un fader déplacé par le DAW peut être ailleurs que le doigt ; le **mode des faders** (**Mode des faders :** dans la fenêtre, ou maj et touche 16) dit quoi faire : *absolu* va là où est le doigt, *relatif* part de la valeur du DAW et suit le mouvement, *hybride* attend que le doigt atteigne la valeur du DAW. Les directions de la bulle, la distance à pousser et la vitesse de répétition se règlent dans la fenêtre. Les boutons **-** et **+** du Scaler décalent les touches d'une octave : le programme s'en accommode, mais mieux vaut ne pas les presser.

## Les autres modules

Pour eux, la fenêtre dessine **le module lui-même** : chaque commande montre ce qu'elle fait (MUTE 3, SOLO 5, JOG...), s'allume quand on la presse, et les voyants et les faders du DAW sont montrés. Le programme transmet les messages du module sans les changer (**Transmettre les messages du module sans les changer**, activé tout seul) et répond à la poignée de main du DAW.

## Modules faits dans le concepteur

**Générer > Mackie Control...** dans le [concepteur de modules](module-designer.fr.md) fait des pads avec les notes Mackie : armement, solo, mute et sélection de 8 pistes, transport, banques, curseurs. Un module dont tous les pads envoient des notes Mackie est reconnu comme un préréglage : Mackie Control s'active et la fenêtre le dessine. Il n'y a pas d'étiquette rouge, puisqu'il n'y a pas de préréglage d'origine auquel revenir. Les faders ne s'y font pas (une bande du concepteur envoie un contrôleur, un fader Mackie est du pitch bend) : utilisez le préréglage Strips ou Rounds.

## Options

**Faders :** 8 (une unité Mackie, par défaut) ou 16 (deux unités : le DAW demande une seconde unité, dans Bitwig *Mackie Control + 1 Extender*, et le programme crée un second port, **Joue Mackie 2**).

## Si ça ne marche pas

| Symptôme | À essayer |
|---|---|
| Rien du DAW dans le journal | connectez d'abord dans Joué Revival, puis ajoutez le contrôleur dans le DAW avec **Joue Mackie** en entrée et en sortie |
| La fenêtre dit que le pilote manque (Windows) | installez loopMIDI ; la fenêtre a un bouton vers sa page de téléchargement |
| Le module ne fait rien | **Affichage > Écouter le port Play (contacts)** doit être coché ; pour le Scaler, **Traduire** aussi |
| Une touche d'un clavier ne fait rien | armement 1, armement 5 et sélection 1 sont désactivés |
| L'armement ne fait rien dans le DAW | la piste ne peut pas être armée (un groupe, par exemple) |

Pour signaler un problème : **Copier le journal** dans la fenêtre, puis **Aide > Copier un rapport de bogue** et **Aide > Signaler un bogue...** (voir le README).

Pas encore fait : une table pour changer ce que fait chaque commande, et des essais avec d'autres DAW que Bitwig.
