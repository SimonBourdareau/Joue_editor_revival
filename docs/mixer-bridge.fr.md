# Mackie Control (aperçu)

Le Scaler a dix-sept touches, deux boutons, une bulle et une bande : de quoi faire une petite surface de mixage. Le programme transforme ce que le Scaler envoie en messages **Mackie Control**, le protocole que Bitwig, Ableton Live, Logic, Reaper et d'autres comprennent pour les faders, le transport et les noms de pistes.

**État : aperçu.** Le préréglage Scaler de mixage, la traduction et la connexion à un DAW par deux ports MIDI existent, et sont testés ici avec des messages synthétiques et des ports simulés. **Aucun vrai DAW et aucun Scaler avec le préréglage n'a encore été essayé avec.** La fenêtre Mackie Control montre ce qui est envoyé et ce que le DAW répond.

## Le préréglage Scaler de mixage

`presets/scaler-mixer.xml` est un fichier de bibliothèque avec un préréglage, *Mackie Control: Scaler*. Importez-le avec **Fichier > Importer des préréglages...**, sélectionnez-le dans le dossier qu'il crée, et **Envoyer vers le Joué...** avec le Scaler sur l'emplacement 1. L'envoi écrit dans la puce du Scaler lui-même : il transforme les deux boutons en boutons de notes (les boutons d'octave sont remplacés), donne aux touches une affectation personnalisée des notes 36 à 52, et donne à chaque touche, à la bulle et à la bande leurs propres numéros de contrôleur. Les numéros n'ont pas changé d'une version du pont à l'autre, donc un Scaler qui porte déjà le préréglage n'a pas besoin d'être renvoyé quand Mackie Control change ce qu'il en fait. **Restaurer la disposition précédente...** remet le Scaler comme il était. Une carte Joué Play ne peut pas écrire dans les modules.

| Scaler | Envoie | Mackie Control envoie au DAW |
|---|---|---|
| Touche 1 (la plus à gauche) | note 36, contrôleur 14 (position le long de la touche) | fader master : note de contact 112, pitch bend sur le canal 9 |
| Touches 2 à 9 | notes 37 à 44, contrôleurs 15 à 22 | faders 1 à 8 : notes de contact 104 à 111, pitch bend sur les canaux 1 à 8 |
| Touche 10, **FIN** (FINE) | note 45 | tenue pendant qu'un fader est touché, le fader ne bouge que d'un quart du mouvement du doigt (la fraction se règle) |
| Touche 11, **MAJ** (SHIFT) | note 46 | tenue, les touches 12 à 17 donnent leur deuxième fonction |
| Touches 12 à 17 | notes 47 à 52 | stop (93), retour rapide (91), avance rapide (92), puis solo, mute et armement de la piste sélectionnée (notes 8, 16 et 0 plus la piste). Avec maj : cycle ou boucle (86), drop ou punch (87), annuler (81), rétablir (annuler avec maj tenue), le mode des faders (rien n'est envoyé) et métronome (89) |
| Bouton 1 et bouton 2 (les deux de droite) | notes 94, 95 | lecture et enregistrement : notes 94 et 95, vélocité 127 appuyé et 0 relâché |
| Bande | contrôleur 83 (absolu) | molette de jog : contrôleur 60, valeurs 1 à 63 vers la droite, 65 à 127 vers la gauche |
| Bulle | contrôleurs 80 (X), 81 (Y), 82 (pression) | gauche et droite : le panoramique de la piste sélectionnée (le V-pot, contrôleurs 16 à 23, deux crans par pas, répété vite) ; haut et bas : banque à gauche et à droite, donc haut va vers les pistes précédentes et bas vers les suivantes. Chacun peut être réglé sur autre chose, ou sur rien |

Quelques détails. Le contrôle Y des touches a 128 pas (le module n'en stocke pas un sur 14 bits), répartis sur les 16384 d'un fader Mackie ; le DAW applique sa propre courbe pour transformer une position de fader en dB. Une touche est touchée de sa note on à sa note off. La bulle est lâchée quand sa pression tombe à 0, ou après 0,7 seconde sans message. La bande repart de là où le doigt se pose après 0,3 seconde sans message. Tout autre message des touches est ignoré tant que Mackie Control traduit.

## Activation toute seule

Le programme reconnaît un Scaler qui porte le préréglage de mixage, parce que ses notes et ses numéros de contrôleur sont ceux du préréglage et qu'aucun autre Scaler ne les a. Dès qu'un tel module est sur l'instrument, **Traduire** est activé et la fenêtre Mackie Control s'ouvre ; quand le module est retiré, la traduction est désactivée de nouveau, et tout ce qu'elle tient est relâché. Deux options de la fenêtre le désactivent : **S'activer tout seul quand un module de mixage est posé sur l'instrument** et **et ouvrir cette fenêtre**. Un Scaler avec sa propre disposition est laissé tranquille.

## La fenêtre Mackie Control

**Affichage > Mackie Control...** l'ouvre. **Traduire** active la traduction ; il faut que **Affichage > Écouter le port Play (contacts)** soit activé. La fenêtre dessine le master à gauche puis autant de faders que le DAW a de pistes (le DAW les nomme : huit au plus, puisqu'une surface Mackie a huit tranches), avec leurs contacts et leurs touches de sélection, la bulle, la bande, la lecture et l'enregistrement, et un journal de ce qui arrive du Scaler et de ce qui irait au DAW. Quand un DAW répond (plus tard), elle montre aussi les noms des pistes, la position des faders telle que le DAW l'a (un trait orange), les vumètres, les voyants d'armement, de solo et de mute (R, S, M), la piste sélectionnée, et le texte que le DAW écrit sous chaque fader, qui est en général sa valeur en dB.

**Les boutons - et + du Scaler lui-même** (les deux de gauche) sont ses boutons d'octave : ils décalent de 12 les notes des touches et on ne peut pas leur faire envoyer quoi que ce soit. Le programme reconnaît le décalage à la position de la touche touchée et continue de marcher, mais il vaut mieux ne pas les presser.

**Modes des faders.** Les faders du Scaler sont des touches, et une touche n'a pas de moteur, donc un fader qu'a déplacé le DAW (un changement de banque, une automation) peut être ailleurs que le doigt. *Absolu* envoie la position du doigt : le fader saute. *Relatif* part à chaque contact de la valeur du DAW et suit le mouvement du doigt à partir de là. *Hybride* attend que le doigt atteigne la valeur du DAW, puis la suit. Le mode se choisit dans la fenêtre ou avec maj et la touche de mode (elle fait le tour). Relatif et hybride demandent que le DAW signale la position de ses faders, ce que fait un DAW Mackie pour ses pistes ; tant qu'il ne l'a pas fait, la position est utilisée telle quelle. Tenir la touche fin pendant un contact fait bouger lentement n'importe quel mode, et la relâcher ne fait pas sauter le fader.

**La piste sélectionnée.** Solo, mute, armement et panoramique agissent sur le dernier fader touché ; si aucun n'a été touché, sur la piste que le DAW a sélectionnée ; déplacer les faders le long des pistes oublie le contact. La fenêtre la montre.

**La bulle** n'agit que tant qu'elle est pressée (sa pression est au-dessus de 0), et dans une seule direction à la fois : une poussée est gardée jusqu'à ce que le doigt revienne au centre, donc un doigt qui repose près d'un bord ne peut pas sauter entre deux directions. Elle répète une fois après 0,7 seconde puis toutes les demi-secondes tant qu'elle est poussée ; **la distance à pousser la bulle** et la vitesse de répétition se règlent dans la fenêtre, et la bulle peut être réglée sur rien. Quand le doigt part, la bulle ramène ses valeurs à 0 en glissant ; c'est ignoré. Cela a été fait à partir d'un enregistrement d'une vraie bulle.

**Banques.** Une surface Mackie a huit tranches. Quand le DAW a plus de pistes, les deux touches de droite (et la bulle) poussent les huit faders le long des pistes, d'une piste à la fois par défaut, et le DAW envoie alors les noms et les positions des pistes qui sont entrées. Avec moins de huit pistes il n'y a rien à déplacer, et le DAW ignore les commandes de banque. **Gauche et droite** de la bulle sélectionnent la piste précédente ou suivante par défaut ; la fenêtre peut à la place les faire avancer d'une piste ou d'une banque. **Essayer une commande sur le DAW** envoie chaque commande Mackie à la main (banque, canal, curseur, sélection, lecture, arrêt, enregistrement), ce qui montre à quoi répond votre DAW sans toucher au Scaler. **Première piste** envoie assez de déplacements de banque et de canal vers la gauche pour ramener les faders sur la première piste, ce qui sert après avoir essayé des commandes à la main. **Copier le journal** met le journal dans le presse-papiers, pour le coller dans un message quand quelque chose ne marche pas.

## Seize faders

Une unité Mackie a huit tranches, donc une unité donne huit faders. Pour seize, le DAW a besoin d'une deuxième unité Mackie (une extension), chaque unité ayant sa propre paire de ports MIDI. La fenêtre a alors **Faders : 16 faders (deux unités Mackie)**.

- **Les touches.** La touche 1 est toujours le master. Les touches 2 à 9 sont les faders 1 à 8, sur la première unité, et les touches 10 à 17 sont les faders 9 à 16, sur la deuxième unité. Il ne reste aucune touche pour sélectionner les pistes ni pour les déplacer, donc la bulle, à gauche et à droite, déplace les faders le long des pistes (d'une piste à la fois, ou d'une banque), et les boutons sont toujours lecture et enregistrement.
- **La fenêtre** montre le master à gauche puis autant de tranches que le DAW en nomme sur les deux unités ensemble.
- **Quelle moitié est la première.** Laquelle des deux unités tient les huit premières pistes dépend de la façon dont le DAW les place : si la moitié gauche du Scaler montre les pistes 9 à 16, cochez **La deuxième unité tient les huit premières pistes**.
- **Dans Bitwig**, ajoutez **Mackie > Mackie Control + 1 Extender** : il lui faut deux paires de ports MIDI, une pour chaque unité. Faites quatre ports loopMIDI, par exemple `Joue to DAW 1`, `DAW 1 to Joue`, `Joue to DAW 2`, `DAW 2 to Joue`, donnez la première paire à la première unité et la deuxième paire à l'extension, et dans la fenêtre choisissez les quatre mêmes ports (**Envoyer vers** et **Écouter**, puis les deux mêmes pour la deuxième unité). Non essayé avec un vrai Bitwig : les noms des choix dans Bitwig peuvent différer.

**Ports croisés.** Si les deux unités du DAW sont sur les paires de ports opposées à celles que vous avez choisies, les pistes de la première unité sont signalées par la deuxième paire et les faders agissent sur le mauvais bloc. Quand le DAW dit quelle unité est laquelle (appareil 0x14 pour l'unité principale, 0x15 pour l'extension) le programme échange tout seul les deux paires et le dit dans le journal ; sinon cochez **L'unité principale du DAW est sur la deuxième paire de ports**. La fenêtre montre quelle unité le DAW dit être sur chaque paire.

**Passage direct.** **Laisser passer les messages du module sans changement** fait transmettre par Mackie Control ce que le module envoie, pour les préréglages Mackie directs des autres modules (voir mackie-presets.fr.md) ; il répond toujours à la poignée de main et montre les pistes du DAW.

**Ce que la fenêtre vous dit.** Dans ce mode elle dit quand les deux ports de la deuxième unité ne sont pas choisis, quand rien n'est venu d'une unité quatre secondes après la connexion (le DAW n'utilise pas cette paire de ports), et quand les deux unités montrent les mêmes pistes (le DAW les reflète au lieu de suivre).

**Si un fader agit sur deux banques.** Chaque touche va à une seule unité, et le journal dit laquelle : **vers le DAW** est la première unité, **vers la deuxième unité** la seconde. Si vous bougez une touche et que le DAW bouge des pistes dans les deux banques, les deux unités arrivent au même endroit dans le DAW. Le programme refuse de se connecter quand la deuxième unité reçoit les mêmes ports que la première, ou quand un port sert à la fois à envoyer et à écouter. Sinon regardez dans le DAW : son extension doit utiliser la deuxième paire de ports, pas la première, et être placée après l'unité principale (et ne pas la refléter). Pour le voir sans toucher au Scaler, appuyez sur **Sélection 1 (deuxième unité)** : le DAW devrait sélectionner sa neuvième piste, et **Select 1** sa première. Si les deux sélectionnent la première piste, le DAW lit les deux paires de ports comme la même unité.

## Connexion à un DAW

Par défaut le programme crée un port MIDI à lui, nommé `Joue Mackie` (et `Joue Mackie 2` pour une deuxième unité), que le DAW voit alors comme une entrée et une sortie. **Non essayé avec un vrai DAW** : ce qui suit est la configuration qui devrait marcher.

- **Windows.** Le programme utilise le pilote qu'installe **loopMIDI** (le pilote MIDI virtuel gratuit de Tobias Erichsen) : installez loopMIDI une fois, et n'y créez aucun port. Le programme charge ce pilote quand il en a besoin et ne l'inclut pas ; vérifiez la licence de loopMIDI pour votre usage. Si le pilote manque, la fenêtre le dit et propose un bouton vers sa page de téléchargement.
- **macOS** utilise des points de terminaison virtuels CoreMIDI, et **Linux** un port du séquenceur ALSA (reliez-le avec `aconnect` ou dans la liste de ports du DAW). Ni l'un ni l'autre n'a été essayé.
- **Ports existants.** **Utiliser des ports qui existent déjà** garde l'ancienne façon : deux ports de bouclage (loopMIDI sous Windows, le pilote IAC sous macOS), un pour chaque sens, choisis comme **Envoyer vers** et **Écouter**.

**Avec Bitwig**

1. Dans Joué Revival : **Affichage > Mackie Control...**, appuyez sur **Connecter** (le programme se connecte ensuite tout seul quand un module de mixage est posé sur l'instrument). Le port `Joue Mackie` existe maintenant.
2. Dans Bitwig : **Settings > Controllers > Add controller > Mackie > Mackie Control**. Mettez son entrée MIDI et sa sortie MIDI sur `Joue Mackie`. Faites-le après l'étape 1 : Bitwig ne liste le port que lorsqu'il existe.
3. Posez le Scaler avec le préréglage de mixage sur l'instrument, ou cochez **Traduire** à la main.
4. Regardez le journal : les lignes en ambre viennent **du DAW**. Un DAW qui demande « device query » puis envoie les noms de ses pistes nous parle.

Si quelque chose ne marche pas, le journal est ce qu'il me faut : les premières lignes après avoir appuyé sur Connecter, et ce que le DAW envoie (**Copier le journal**).

## Modules faits dans le concepteur de modules

Un module fait dans le concepteur de modules dont tous les pads envoient des notes Mackie Control (utilisez **Générer > Mackie Control...**) est aussi un module Mackie Control : quand il est sur l'instrument, Mackie Control s'active, ouvre sa fenêtre, qui dessine le module avec ce que fait chaque pad, et ouvre le port. Il n'y a pas de préréglage d'origine auquel revenir, donc la marque rouge n'est pas montrée pour lui. Voir le [guide du concepteur de modules](module-designer.fr.md) et les [préréglages Mackie Control](mackie-presets.fr.md).

## Signaler un problème

Appuyez sur **Copier le journal** dans la fenêtre après avoir essayé, et utilisez **Aide > Copier un rapport de bogue** et **Aide > Signaler un bogue...** : le rapport contient la version, les informations sur l'appareil, les modules, le journal de détection et l'état de Mackie Control. Voir le README pour les étapes.

## La suite

1. La poignée de main Mackie et les réponses aux questions du DAW sont écrites (demande d'appareil, réponse de connexion, version) mais pas essayées ; les programmes diffèrent dans ce qu'ils exigent.
2. Des essais avec le script Mackie Control de Bitwig et d'Ableton Live, et avec les ports que le programme crée lui-même sur chaque système.
3. Une table pour changer ce que fait chaque touche, avec un bouton Apprendre.
