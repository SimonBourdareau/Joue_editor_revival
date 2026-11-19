# Factory presets

`factoryLibrary.xml` is the library of factory presets of the manufacturer's application: the fifteen modules of the Pro and Play series, as the original editor shows them.

Joué Revival loads it by itself the first time it starts, and **File > Open the factory presets** loads it again. It is looked for next to the program (in this `presets` folder, or inside the app on macOS).

These presets come from the manufacturer's application. They are included so that the program works at once; they are not covered by the GNU GPL of the program.

# Mackie Control presets

`mackie-presets.xml` and `scaler-mixer.xml` hold the Mackie Control presets, one for each kind of module, all named *Mackie Control: <module>* (see `docs/mixer-bridge.md`). The program reads them at every start and shows them, locked, in the library folder **Mackie presets**: there is nothing to import. They come from the factory presets of the manufacturer's application, so the same remark applies: the settings of the module are the manufacturer's; the notes and controller numbers are the author's.
