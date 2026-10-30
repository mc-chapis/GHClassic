#pragma once

#include "formats/dtb.h"

#include <filesystem>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace gh2::custom_songs
{
    struct Song
    {
        std::string id; // stable, separate from the discs' song names
        dtb::Node entry;
        // Archive paths and the loose files served there, read on demand.
        std::vector<std::pair<std::string, std::filesystem::path>> files;
    };

    // Roots in priority order; duplicate song IDs keep the first valid copy.
    // Each export has songs.dta beside its .mid/.vgs/.voc files. Paths
    // relative to songs.dta also work for exports that retain subfolders.
    std::vector<Song> scan(const std::vector<std::filesystem::path> &roots, std::ostream &log);
}
