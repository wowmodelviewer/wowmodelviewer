#pragma once

// ---- URL import (Armory / Wowhead) ----------------------------------------
// Extracted from main.cpp.

struct AppState;

namespace URLImportHandler
{

/// Parse the URL in app.importUrlBuf, find a matching importer, and apply
/// the imported character / NPC / item to the scene.
void doImport(AppState& app);

} // namespace URLImportHandler
