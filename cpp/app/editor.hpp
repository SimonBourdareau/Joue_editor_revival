#pragma once
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "designer.hpp"
#include "joue/instrumentmodule.hpp"
#include "liveview.hpp"
#include "texture.hpp"
#include "updatecheck.hpp"
#include "joue/endpoints.hpp"
#include "joue/library.hpp"
#include "joue/settingsmeta.hpp"

struct EditorOptions {
    std::string dataDir;
    std::string library;
    std::string select;
    bool devicePanel = false;
    bool openFirst = false;
    std::string element;  // element to select at start-up (for screenshots)
    bool live = false;  // kept for old command lines: the live instrument is open at start-up anyway
    bool about = false;  // opens the About window at start-up (for screenshots)
    bool noTouches = false;     // does not listen to the Play port
    bool noUpdateCheck = false;  // does not look for a newer version
    std::string language;       // en or fr: the language of the interface; empty: the one chosen before, else the system's
    bool safeGraphics = false;  // old OpenGL, no vertical sync, and the device drawn without a texture
    int designerElement = -1;  // element of the designer to select at start-up
    std::string designer;  // opens the module designer at start-up: a blob file, or "new"
};

class Editor {
    friend struct EditorTestAccess;

public:
    explicit Editor(EditorOptions options);

    // The OpenGL vendor, renderer and version in use: written to graphics.log in the data folder and shown in the About window.
    void setGraphicsInfo(const std::string& info);

    // Draws the whole window; call once per frame between NewFrame and Render.
    void draw();

    bool dirty() const { return dirty_; }
    std::string title() const;
    bool wantsQuit() const { return quit_; }
    void askQuit() { quitRequested_ = true; }

private:
    struct JobResult {
        std::vector<std::string> lines;
        std::vector<joue::PortPair> pairs;
        int defaultPair = -1;
    };

    // library state
    void loadRoot(std::unique_ptr<joue::Node> root, const std::string& message);
    void openFromPath(const std::string& path);
    bool saveLibrary();
    void exportCopy(const std::string& path);
    std::string snapshot() const;
    void rebuildObserved();
    void setStatus(const std::string& message, bool error = false);

    // undo
    void markChanged();
    void flushPendingUndo();
    void beginStructural();
    void endStructural();
    void undo();
    void redo();

    // drawing
    void drawMenuBar();
    void drawTreeToolbar();
    void drawTree();
    void drawEntries(joue::Node& owner);
    void drawContent();
    void drawModule(joue::Node& module);
    bool anyVisibleSetting(joue::Node& array, const std::vector<joue::SettingMeta>* meta, const std::string& parentValue);
    void drawSettings(joue::Node& array, bool locked, const std::vector<joue::SettingMeta>* meta, const std::string& parentValue);
    void drawValue(joue::Node& setting, bool locked, const joue::SettingMeta* meta);
    void drawDevicePanel();
    void drawLeftOfModule(joue::Node& module, bool locked, float width);
    void drawRightOfModule(joue::Node& module, bool locked);
    bool ruleFlags(const std::string& settingName, bool& hidden, bool& disabled);
    void drawPopups();
    bool editText(const char* id, std::string& value, bool disabled, float width = 0);
    void startJob(std::function<JobResult()> work);
    void pollJob();

    // library actions
    void newFolder();
    void duplicateSelected();
    void moveSelected(int direction);
    void deleteSelected();

    EditorOptions options_;
    std::string libraryPath_;
    std::unique_ptr<joue::Node> root_;
    joue::Node* selected_ = nullptr;
    bool dirty_ = false;
    std::string status_;
    bool statusError_ = false;
    std::map<std::string, std::set<std::string>> observed_;

    // An undo step is the serialized library plus the selection that belonged to that state.
    struct State {
        std::string xml;
        std::vector<int> selection;
    };
    State snapshotState() const;
    void restore(const State& state);

    std::vector<State> undo_, redo_;
    State lastSnapshot_;
    std::optional<State> pending_;
    bool changedThisFrame_ = false;

    bool showDevice_ = false;
    void drawDeviceInfoWindow();
    void drawComparePopup();
    void drawHelp();
    void pollUpdateCheck();
    void openGuide(bool designer);
    bool deviceInfoOpen_ = false, helpOpen_ = false, welcomeSeen_ = false, checkUpdates_ = true, comparePopup_ = false, compareReady_ = false, updateStarted_ = false, updateManual_ = false;
    std::vector<std::string> compareLines_;
    std::string compareTitle_;
    std::vector<joue::Node*> compareChoices_;
    joue::Node* compareFirst_ = nullptr;
    int compareChoice_ = -1;
    UpdateCheck update_;
    std::string updateTag_, updateUrl_;
    long long lastUpdateCheck_ = 0;
    std::future<JobResult> job_;
    std::vector<joue::PortPair> pairs_;
    int pairIndex_ = 0;
    int slot_ = 1;
    std::string output_ = "Nothing read yet.";
    std::string hexInput_;

    bool openPathPopup_ = false, deletePopup_ = false, quitPopup_ = false, exportPopup_ = false, aboutPopup_ = false;
    Texture logo_;  // the logo of the program, made when it is first drawn
    void drawAbout();
    void loadUiSettings();
    void applyTouchSelection();
    bool languageChosen_ = false;  // the language was chosen by the user (kept in ui.txt) and is not the system's
    bool touchSelect_ = true;  // a touch on the instrument selects the element in the editor
    void saveUiSettings() const;
    std::string graphicsInfo_;
    bool graphicsSoftware_ = false;
    bool quitRequested_ = false, quit_ = false;
    std::string pathInput_;
    bool firstElementOpened_ = false;

    // Element selected in the picture or in the list: the library element's name, and the name of the clicked shape.
    std::string selectedElement_, selectedShape_;
    std::string lastExpert_;                    // Custom mapping as it was last frame, to choose again what a selected key edits when it changes
    const joue::Node* lastExpertModule_ = nullptr;
    bool scrollToSelected_ = false;
    std::string pendingElement_;
    Designer designer_;
    LiveView live_;
    float liveHeight_ = 0;  // height of the instrument panel above the columns; 0 until the first frame sets it

    // A library module made from the module in a slot of the instrument, with what is needed to push its edits back.
    struct InstrumentLink {
        int slot = 0;
        joue::Bytes original, current;  // as read; as last written
        std::string backup;             // the file holding the original
        const joue::ModuleBindings* bindings = nullptr;
        joue::SettingValues initial;    // the values when the module was read or last pushed
        std::set<std::pair<std::string, std::string>> bound;
    };
    std::map<std::string, InstrumentLink> links_;  // by the name of the library module, which survives undo and reloads
    const InstrumentLink* activeLink_ = nullptr;   // while a linked module is drawn
    void openModuleFromInstrument(int slot, const joue::Bytes& blob);
    void drawInstrumentPanel(joue::Node& module, InstrumentLink& link);
    // Upload of a library module (a preset) to the module of the same type that is on the instrument.
    struct LastUpload {
        int slot = 0;
        joue::Bytes original;  // what the module held before
        std::string backup;
    };
    std::optional<LastUpload> lastUpload_;
    int uploadSlot_ = 0;
    std::string backupLayout(int slot, const joue::Bytes& blob);  // saves the layout as a file, returns its path or empty
    void drawUploadPanel(joue::Node& module);
    void uploadPreset(joue::Node& module, int slot);
    void pushLinked(joue::Node& module, InstrumentLink& link, bool restore);

    // what drawSettings evaluates the dynamic rules against
    joue::Node* ctxModule_ = nullptr;
    joue::Node* ctxElement_ = nullptr;
    int ctxType_ = 0;
    std::string ctxScope_ = "M";
    int axisFilter_ = 0;  // 0 all, 1 X, 2 Y, 3 Z (settings of Area-like elements are per axis)
    joue::Node* selectionOwner_ = nullptr;  // module the selection belongs to
};
