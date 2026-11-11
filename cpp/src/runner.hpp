//! Orchestration: glue InnerTube, format selection, downloading and muxing
//! together into the commands the user actually types.

#pragma once

#include "cli.hpp"

namespace ytgrab::runner {

void run(const cli::Cli& cli);

}  // namespace ytgrab::runner
