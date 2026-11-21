#pragma once
#include <map>
#include <string>
#include <vector>

struct OptionChoice { const char* label; const char* value; };

struct OptionDef {
    const char* id;        // clave en el .ini
    const char* label;     // texto en el menú
    const char* category;  // pestaña del menú
    const char* coreKey;   // opción del núcleo Beetle PSX HW (nullptr = opción del frontend)
    std::vector<OptionChoice> choices;
    int defIndex;
    const char* help;
    bool needsRestart;     // requiere reiniciar el juego
    unsigned flags = 0;    // OPT_*: con qué renderer funciona
};

enum : unsigned { OPT_VULKAN_ONLY = 1, OPT_HW_ONLY = 2 };

enum class Scope { Global, Game };

class Config {
public:
    static Config& get();

    const std::vector<OptionDef>& defs() const { return defs_; }
    std::vector<std::string> categories() const;

    void loadGlobal();
    void saveGlobal();
    void loadGame(const std::string& gameStem);   // aplica overrides por juego
    void saveGame();
    void clearGame();
    // ¿La opción tiene efecto con el renderer actual? (las que no, se ocultan en el menú)
    bool appliesToRenderer(const OptionDef& d) const;
    // Renderizador con el que arrancó PSX-K2 ("hardware_vk" o "software"). Cambiarlo requiere reiniciar la app:
    // hasta entonces el núcleo sigue recibiendo el activo (si no, dibujaba en software sobre la salida Vulkan).
    void setActiveRenderer(const char* r) { activeRenderer_ = r; }
    const char* activeRenderer() const { return activeRenderer_.empty() ? value("renderer") : activeRenderer_.c_str(); }
    bool rendererPending() const { return !activeRenderer_.empty() && activeRenderer_ != value("renderer"); }
    // Restaura a valores por defecto (en el ámbito que se está editando). nullptr = todas las categorías
    void resetCategory(const char* category);                              // borra overrides del juego actual
    void unloadGame();
    bool hasGame() const { return !gameStem_.empty(); }

    Scope editScope = Scope::Global;

    int  index(const char* id) const;              // valor efectivo
    const char* value(const char* id) const;
    bool isOn(const char* id) const { return std::string(value(id)) == "enabled"; }
    void setIndex(const char* id, int idx);        // escribe en editScope
    bool isOverridden(const char* id) const;

    // Para el núcleo: devuelve el valor gestionado o nullptr
    const char* coreValue(const char* coreKey) const;
    bool consumeCoreDirty() { bool d = coreDirty_; coreDirty_ = false; return d; }
    bool consumeFrontendDirty() { bool d = feDirty_; feDirty_ = false; return d; }

private:
    Config();
    const OptionDef* find(const char* id) const;
    void parseIni(const std::string& path, std::map<std::string, int>& out);
    void writeIni(const std::string& path, const std::map<std::string, int>& m, bool full);

    std::vector<OptionDef> defs_;
    std::map<std::string, int> global_, game_;
    std::string gameStem_;
    std::string activeRenderer_;
    bool coreDirty_ = true, feDirty_ = true;
    bool applyingPreset_ = false;
    void applyPreset(int idx);
};
