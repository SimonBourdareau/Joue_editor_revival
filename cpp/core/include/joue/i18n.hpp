#pragma once
#include <string>

namespace joue::i18n {

enum class Language { English, French };

// The language of the interface. English is the language of the source: a text with no entry in the French catalogue is shown as it is.
void setLanguage(Language language);
Language language();
const char* languageCode(Language language);                 // "en" or "fr"
Language languageFromCode(const std::string& code);          // anything that starts with "fr" is French, the rest English
Language detectSystemLanguage();                             // French when the user's language is French (Windows, macOS, or LC_ALL / LANG / LANGUAGE)

// A label or a format string of the interface. "Text##id" translates Text and keeps the id, so that ids stay the same wherever they are used.
// The pointer stays valid for the life of the program.
const char* tr(const char* english);

// A message made at run time, with names and numbers in it ("Not erased: this tag holds a manufacturer layout (Scaler)."): an exact entry of
// the catalogue, or an entry in which {} stands for each part that varies, as in "Slot {}: layout read, {}, {} bytes".
std::string message(const std::string& english);

// A text that comes from data (the title of a setting, a choice in a list): exact entries only.
std::string text(const std::string& english);

}  // namespace joue::i18n
