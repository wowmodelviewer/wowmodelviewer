#pragma once

// ---- Application modal dialogs --------------------------------------------
// Extracted from main.cpp — standalone modals that operate directly on
// AppState (URL Import, Config Selection, About, Language / Locale).

struct AppState;

namespace AppDialogs
{

void drawImportDialog(AppState& app);
void drawConfigPopup(AppState& app);
void drawAboutDialog(AppState& app);
void drawLanguageDialog(AppState& app);

} // namespace AppDialogs
