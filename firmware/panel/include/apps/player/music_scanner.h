#ifndef MUSIC_SCANNER_H
#define MUSIC_SCANNER_H

#include <string>
#include <vector>
#include <cstdint>

struct KaraokeWord {
    uint32_t start_ms;
    std::string word;
};

struct LrcLine {
    uint32_t time_ms;
    std::string text;
    std::vector<KaraokeWord> words;
};

struct TrackMetadata {
    std::string full_path;
    std::string filename;
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string album_artist;
    uint32_t track_num = 0;
    uint32_t sample_rate = 44100;
    uint8_t bit_depth = 16;
    uint8_t channels = 2;
    uint32_t duration_sec = 180;
    uint32_t bitrate_kbps = 0;
    std::string cover_path;
    std::string lrc_path;
    std::string embedded_lyrics_synced;
    std::string embedded_lyrics_unsynced;
    std::vector<LrcLine> embedded_lyrics;
};

typedef void (*ScanProgressCallback)(size_t processed, size_t total, const std::string& current_file);

class MusicScanner {
public:
    static std::vector<TrackMetadata> load_library();
    static bool save_library(const std::vector<TrackMetadata>& library);

    static std::vector<TrackMetadata> full_scan(ScanProgressCallback callback = nullptr);
    static std::vector<TrackMetadata> partial_scan(ScanProgressCallback callback = nullptr);

    static TrackMetadata extract_metadata(const std::string& filepath);
    static std::vector<LrcLine> parse_lrc_string(const std::string& lrc_content);

private:
    static std::vector<std::string> scan_directory_files(const std::string& root_dir);
    static void parse_flac_vorbis_comment(const uint8_t* comment_data, size_t length, TrackMetadata& meta);
};

#endif // MUSIC_SCANNER_H
