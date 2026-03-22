#pragma once

// ---- Application modal dialogs --------------------------------------------
// Extracted from main.cpp — standalone modals that operate directly on
// AppState (URL Import, Config Selection, About, Language / Locale).

struct AppState;

/// @brief Application modal dialogs (URL Import, Config Selection, About, Language).
namespace AppDialogs
{

/// @brief Draw the URL import dialog (Armory / Wowhead).
void drawImportDialog(AppState& app);

/// @brief Draw the game config selection popup (locale / product picker).
void drawConfigPopup(AppState& app);

/// @brief Draw the About dialog with version and credits.
void drawAboutDialog(AppState& app);

/// @brief Draw the Language / Locale selection dialog.
void drawLanguageDialog(AppState& app);

} // namespace AppDialogs
