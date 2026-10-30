#include "content/custom_song_files.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>

namespace gh2::custom_songs
{
    namespace
    {
        namespace fs = std::filesystem;

        // These files are metadata, never commands to execute at startup.
        bool literal(const dtb::Node &node)
        {
            if (node.type == dtb::kArray)
                return std::all_of(node.nodes.begin(), node.nodes.end(), literal);
            return node.type == dtb::kInt || node.type == dtb::kFloat || node.type == dtb::kString ||
                   (node.type == dtb::kSymbol && node.text.find('#') == std::string::npos);
        }

        template <class Node>
        Node *field(Node &node, const std::string &key)
        {
            for (auto &child : node.nodes)
                if (child.type == dtb::kArray && !child.nodes.empty() && child.nodes[0].text == key)
                    return &child;
            return nullptr;
        }

        bool value(const dtb::Node *node)
        {
            return node && node->nodes.size() == 2u &&
                   (node->nodes[1].type == dtb::kString || node->nodes[1].type == dtb::kSymbol) &&
                   !node->nodes[1].text.empty();
        }

        bool identifier(const std::string &id)
        {
            return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_' || c == '-';
            });
        }

        // Onyx writes disc paths into songs.dta while putting their files
        // directly beside it. Keep either that layout or relative paths.
        fs::path resolve(const fs::path &dir, const std::string &text)
        {
            const fs::path path(std::u8string(text.begin(), text.end()));
            if (path.is_absolute() || path.has_root_name())
                return {};
            for (const auto &part : path)
                if (part == "..")
                    return {};
            std::error_code error;
            if (fs::is_regular_file(dir / path, error))
                return dir / path;
            if (fs::is_regular_file(dir / path.filename(), error))
                return dir / path.filename();
            return {};
        }

        bool assets(Song &song, dtb::Node &audio, const fs::path &dir, const std::string &prefix)
        {
            dtb::Node *name = field(audio, "name");
            if (!value(name))
                return false;
            const std::string original = name->nodes[1].text;
            const fs::path vgs = resolve(dir, original + ".vgs");
            if (vgs.empty())
                return false;
            const std::string mapped = prefix + std::to_string(song.files.size());
            song.files.emplace_back(mapped + ".vgs", vgs);
            if (const fs::path voc = resolve(dir, original + ".voc"); !voc.empty())
            {
                song.files.emplace_back(mapped + ".voc", voc);
                // The singer builds this path from the song's symbol,
                // independently of the audio name in its config entry.
                if (audio.nodes[0].text == "song")
                    song.files.emplace_back("songs/" + song.id + "/" + song.id + ".voc", voc);
            }
            name->nodes[1] = {dtb::kString, 0, 0.0f, mapped, {}};

            dtb::Node *midi = field(audio, "midi_file");
            const fs::path chart = resolve(dir, value(midi) ? midi->nodes[1].text : original + ".mid");
            if (chart.empty())
                return false;
            song.files.emplace_back(mapped + ".mid", chart);
            if (!midi)
                audio.nodes.push_back(dtb::array({dtb::symbol("midi_file"),
                                                {dtb::kString, 0, 0.0f, mapped + ".mid", {}}}));
            else
                midi->nodes = {dtb::symbol("midi_file"), {dtb::kString, 0, 0.0f, mapped + ".mid", {}}};
            return true;
        }

        bool prepare(Song &song, const fs::path &dir)
        {
            dtb::Node &entry = song.entry;
            if (entry.type != dtb::kArray || entry.nodes.empty() || entry.nodes[0].type != dtb::kSymbol ||
                !identifier(entry.nodes[0].text) || !literal(entry) || !value(field(entry, "name")) ||
                !value(field(entry, "artist")) || !field(entry, "song"))
                return false;
            song.id = "ghc_custom_" + entry.nodes[0].text;
            entry.nodes[0] = dtb::symbol(song.id);
            const std::string prefix = "ghc/custom/" + song.id + "/";
            for (dtb::Node &part : entry.nodes)
            {
                if (part.type != dtb::kArray || part.nodes.empty())
                    continue;
                const std::string &key = part.nodes[0].text;
                if (key == "song" || key == "song_coop" || key.compare(0, 14, "song_practice_") == 0)
                    if (!assets(song, part, dir, prefix))
                        return false;
            }
            return true;
        }
    }

    std::vector<Song> scan(const std::vector<fs::path> &roots, std::ostream &log)
    {
        std::vector<Song> songs;
        std::set<std::string> ids;
        std::set<fs::path> seen;
        for (const fs::path &root : roots)
        {
            std::error_code error;
            if (!fs::is_directory(root, error))
                continue;
            const fs::path canonical = fs::weakly_canonical(root, error);
            if (!error && !seen.insert(canonical).second)
                continue;
            std::vector<fs::path> manifests;
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
            for (; !error && it != end; it.increment(error))
                if (it->path().filename() == "songs.dta" && it->is_regular_file(error))
                    manifests.push_back(it->path());
            if (error)
                log << "[custom] " << root << ": " << error.message() << '\n';
            std::sort(manifests.begin(), manifests.end());
            for (const fs::path &manifest : manifests)
            {
                std::ifstream in(manifest, std::ios::binary);
                std::string text((std::istreambuf_iterator<char>(in)), {});
                if (text.compare(0, 3, "\xef\xbb\xbf") == 0)
                    text.erase(0, 3);
                auto parsed = dtb::parse(text);
                if (!in || !parsed || parsed->nodes.empty())
                {
                    log << "[custom] " << manifest << ": unreadable or invalid songs.dta\n";
                    continue;
                }
                for (dtb::Node &entry : parsed->nodes)
                {
                    Song song{{}, std::move(entry), {}};
                    if (!prepare(song, manifest.parent_path()))
                        log << "[custom] " << manifest << ": skipped invalid GH2 entry or missing .mid/.vgs files\n";
                    else if (!ids.insert(song.id).second)
                        log << "[custom] " << manifest << ": duplicate " << song.id << ", keeping first copy\n";
                    else
                        songs.push_back(std::move(song));
                }
            }
        }
        std::sort(songs.begin(), songs.end(), [](const Song &a, const Song &b) {
            const auto &left = field(a.entry, "name")->nodes[1].text;
            const auto &right = field(b.entry, "name")->nodes[1].text;
            return left != right ? left < right : a.id < b.id;
        });
        return songs;
    }
}
