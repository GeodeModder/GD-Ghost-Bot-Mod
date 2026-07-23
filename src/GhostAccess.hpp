#pragma once
#include "GhostTypes.hpp"

// Defined in GhostPlayLayer.cpp. Returns nullptr if not currently in a
// real play session (e.g. in the level editor, or PlayLayer::get() is null).
// Other files use this instead of casting to GhostPlayLayer* directly, so
// the GhostPlayLayer $modify class stays defined in exactly one .cpp file
// (defining a $modify class in a header and including it in two .cpp files
// would register the same hook twice / cause duplicate symbols).
GhostManager* ghostGetCurrentManager();
