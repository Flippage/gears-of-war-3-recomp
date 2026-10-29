#pragma once
// The Play page (and, from Task 11, the Game files pages).
struct App;

// Re-reads what the Play page shows: the build's content (<game>\DLC) and the renderer (gow3.toml).
// Call at start and whenever the game folder, build or content changes.
void RefreshPlayPage(const App& app);
void DrawPlayPage(App& app);
