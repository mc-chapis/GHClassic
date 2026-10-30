#include "content/custom_songs.h"

#include "content/custom_song_files.h"
#include "content/locale.h"
#include "content/setlists.h"
#include "content/songs.h"
#include "disc/ark.h"
#include "settings/settings.h"

#include <iostream>

namespace gh2::custom_songs
{
    void install()
    {
        const auto catalog = scan(settings::contentPaths("songs"), std::cerr);
        if (catalog.empty())
            return;
        setlists::Tier tier{"battle", "custom", {}, false};
        for (const Song &song : catalog)
        {
            const std::string vocals = "songs/" + song.id + "/" + song.id + ".voc";
            // Instrumental exports may omit lip sync, just as imported
            // GH1 songs do. GH2's blink track keeps the singer idle.
            ark::rename(vocals, 0u, "songs/_blinktrack/_blinktrack.voc");
            for (const auto &[path, file] : song.files)
                ark::addLooseFile(path, file);
            songs::add(dtb::text(song.entry));
            tier.songs.push_back(song.id);
        }
        locale::add("setlist_custom", "CUSTOM");
        locale::add("song_header_custom", "CUSTOM SONGS");
        // This path always uses GH2's paper, even while another campaign
        // has its menus in front. Custom songs are open from the start.
        setlists::add("custom", {std::move(tier)}, "ghc/gh2/ui/sel_song_quickplay.milo",
                      {"GHC", "GHC", "GHC", "GHC", "GHC"});
        std::cerr << "[custom] loaded " << catalog.size() << " songs" << std::endl;
    }
}
