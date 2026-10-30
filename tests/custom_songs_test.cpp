#include "content/custom_song_files.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
    namespace fs = std::filesystem;
    int failed = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "failed: " #condition << " line " << __LINE__ << '\n'; ++failed; } } while (0)

    void write(const fs::path &path, const std::string &text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }

    std::string entry(const std::string &id, const std::string &title)
    {
        return "('" + id + "' ('name' \"" + title + "\") (artist \"Artist\") "
               "(song (name \"songs/" + id + "/chart\") (tracks ((guitar (2)))) "
               "(midi_file \"songs/" + id + "/chart.mid\")) "
               "(song_practice_1 (name \"songs/" + id + "/chart_empty\") "
               "(midi_file \"songs/" + id + "/chart.mid\")))";
    }

    void package(const fs::path &dir, const std::string &id, const std::string &title)
    {
        write(dir / "songs.dta", entry(id, title));
        for (const char *name : {"chart.mid", "chart.vgs", "chart.voc", "chart_empty.vgs"})
            write(dir / name, "asset");
    }
}

int main(int argc, char **argv)
{
    const fs::path root = fs::temp_directory_path() /
        ("ghc-custom-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path portable = root / "portable", user = root / "user";
    std::ostringstream log;
    CHECK(gh2::custom_songs::scan({portable, user}, log).empty());
    package(portable / "Title with spaces (Onyx)_gh2", "first", "Z Song");
    package(user / "duplicate", "first", "Duplicate");
    package(user / "other", "second", "A Song");
    package(user / "broken", "missing", "Broken");
    fs::remove(user / "broken" / "chart.vgs");
    write(user / "invalid" / "songs.dta", "(unclosed");
    write(user / "commands" / "songs.dta", "{quit}");
    auto songs = gh2::custom_songs::scan({portable, user, portable}, log);
    CHECK(songs.size() == 2u);
    if (songs.size() == 2u)
    {
        CHECK(songs[0].id == "ghc_custom_second");
        CHECK(songs[1].id == "ghc_custom_first");
        CHECK(songs[1].files.front().second.parent_path() == portable / "Title with spaces (Onyx)_gh2");
        CHECK(songs[1].files.size() == 6u); // main audio, vocals at both paths, MIDI; practice audio and MIDI
        CHECK(songs[1].files[2].first == "songs/ghc_custom_first/ghc_custom_first.voc");
        CHECK(songs[1].entry.nodes[0].text == songs[1].id);
        const auto &audio = songs[1].entry.nodes[3];
        CHECK(audio.nodes[1].nodes[1].text == "ghc/custom/ghc_custom_first/0");
    }
    CHECK(log.str().find("duplicate") != std::string::npos);
    CHECK(log.str().find("missing .mid/.vgs") != std::string::npos);
    CHECK(log.str().find("invalid songs.dta") != std::string::npos);

    // Multiple entries, a UTF-8 BOM, comments, and paths retaining the
    // archive layout instead of the flattened Onyx layout.
    const fs::path nested = root / "nested";
    write(nested / "songs.dta", "\xef\xbb\xbf; exports\n" + entry("third", "Third") + entry("fourth", "Fourth"));
    for (const char *id : {"third", "fourth"})
        for (const char *asset : {"chart.mid", "chart.vgs", "chart_empty.vgs"})
            write(nested / "songs" / id / asset, "asset");
    CHECK(gh2::custom_songs::scan({nested}, log).size() == 2u);

    // No explicit midi_file: use the GH2 name + .mid convention.
    package(root / "implicit", "implicit", "Implicit");
    write(root / "implicit" / "songs.dta", "(implicit (name \"Implicit\") (artist \"Artist\") (song (name \"chart\")))");
    CHECK(gh2::custom_songs::scan({root / "implicit"}, log).size() == 1u);

    // Optional real-export check, without copying or modifying the inputs.
    if (argc > 1)
    {
        auto exports = gh2::custom_songs::scan({fs::path(argv[1])}, std::cerr);
        CHECK(!exports.empty());
        std::cout << "Reference exports: " << exports.size() << " songs\n";
        for (const auto &song : exports)
            std::cout << song.id << ": " << song.files.size() << " asset mappings\n";
    }
    fs::remove_all(root);
    std::cout << (failed ? "FAIL" : "PASS") << " custom songs\n";
    return failed ? 1 : 0;
}
