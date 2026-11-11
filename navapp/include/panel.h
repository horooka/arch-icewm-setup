#pragma once

#include "nav.h"

#include <string>
#include <vector>

// Renders the configured navapp panel.
// Interactive when a terminal is available; otherwise prints a static frame.
// Returns 0 on success.
int eval_panel(const std::string &layout, const std::string &navdict_path,
               const std::vector<Dest> &dests, const Settings &settings,
               bool interactive);
