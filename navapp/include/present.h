#pragma once

#include "nav.h"

#include <string>
#include <vector>

// Renders the configured navapp present.
// Interactive when a terminal is available; otherwise prints a static frame.
// Returns 0 on success.
int eval_present(const std::string &layout, const std::string &navdict_path,
                 const std::vector<Dest> &dests, const Settings &settings,
                 bool interactive);
