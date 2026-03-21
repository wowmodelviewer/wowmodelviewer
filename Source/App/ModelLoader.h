#pragma once

// ---- Model loading and management helpers ---------------------------------
// Extracted from main.cpp — handles model loading, animation/character/model
// control init, equipment, item sets, start outfits, NPC/item browsers,
// mounts, and all related filter-rebuilding functions.

#include <string>
#include <vector>

struct AppState;
class GameFile;
class ItemDatabase;
struct NPCRecord;
class OrbitCamera;
class WoWModel;

namespace ModelLoader
{

/// Convert a wide string to narrow ASCII (lossy, for display only).
std::string wstringToString(const std::wstring& ws);

/// Return the currently loaded WoWModel (first child of root), or nullptr.
WoWModel* getLoadedModel(AppState& app);

/// Reset the orbit camera to frame the given model.
void resetCameraToModel(OrbitCamera& camera, const WoWModel* model, float fov);

/// Apply a creature / item skin variant to the model.
void applySkin(WoWModel* model, int skinIndex, AppState& app);

/// Populate app.animEntries / app.skinEntries from the model.
void initAnimationControl(WoWModel* model, AppState& app);

/// Populate app.customizationOptions from the model's ChrCustomization DB.
void initCharacterControl(WoWModel* model, AppState& app);

/// Populate app.geosetGroups and app.pcrState from the model.
void initModelControl(WoWModel* model, AppState& app);

/// Load a .m2 GameFile, create a WoWModel, attach to root, init controls.
void loadModel(GameFile* file, AppState& app, float fov);

/// Tear down the current model and reset related state.
void clearModel(AppState& app);

// ---- Equipment helpers ----------------------------------------------------

void rebuildEquipFilteredItems(AppState& app, const ItemDatabase& db);
void tryToEquipItem(WoWModel* model, int id, AppState& app, const ItemDatabase& db);

// ---- Item Sets ------------------------------------------------------------

void buildItemSets(AppState& app);
void rebuildItemSetFilter(AppState& app);
void applyItemSet(WoWModel* model, int setId, AppState& app, const ItemDatabase& db);

// ---- Start Outfits --------------------------------------------------------

void buildStartOutfits(WoWModel* model, AppState& app);
void rebuildStartOutfitFilter(AppState& app);
void applyStartOutfit(WoWModel* model, int outfitId, AppState& app, const ItemDatabase& db);

// ---- NPC Browser ----------------------------------------------------------

void rebuildNpcFilter(AppState& app, const std::vector<NPCRecord>& npcs);
void loadNPC(unsigned int creatureID, AppState& app, float fov);

// ---- Item Browser ---------------------------------------------------------

void rebuildItemBrowseFilter(AppState& app, const ItemDatabase& db);
void loadItemModel(unsigned int itemId, AppState& app, float fov);

// ---- Mounts ---------------------------------------------------------------

void buildMountList(AppState& app);
void rebuildMountFilter(AppState& app);
void mountCharacter(int displayID, GameFile* creatureFile, AppState& app, float fov);
void dismountCharacter(AppState& app, float fov);

} // namespace ModelLoader
