#ifndef LYRICS_ENGINE_H
#define LYRICS_ENGINE_H

#include <string>
#include <vector>
#include <cstdint>

enum LyricsMode {
    LYRICS_MODE_UNSUPPORTED = 0,
    LYRICS_MODE_TTML_KARAOKE = 1,
    LYRICS_MODE_LINE_STANDARD = 2
};

struct TtmlWordSpan {
    uint32_t start_ms;
    uint32_t end_ms;
    std::string text;
};

struct LyricsLine {
    uint32_t start_ms;
    uint32_t end_ms;
    std::string full_text;
    std::vector<TtmlWordSpan> words;
};

struct LyricsTrackData {
    LyricsMode mode = LYRICS_MODE_UNSUPPORTED;
    std::string source_info;
    std::vector<LyricsLine> lines;

    bool is_valid() const {
        return mode != LYRICS_MODE_UNSUPPORTED && !lines.empty();
    }
};

class LyricsEngine {
public:
    /**
     * @brief Priority Fallback Lyrics Discovery:
     * 1) External .ttml file
     * 2) External .lrc file
     * 3) Embedded FLAC Vorbis Comments (SYNCEDLYRICS / LYRICS / UNSYNCEDLYRICS)
     * 4) LYRICS_UNSUPPORTED fallback
     */
    static LyricsTrackData discover_and_parse(
        const std::string& audio_filepath,
        const std::string& vorbis_synced_lyrics = "",
        const std::string& vorbis_unsynced_lyrics = ""
    );

    static LyricsTrackData parse_ttml(const std::string& ttml_xml_content);
    static LyricsTrackData parse_lrc(const std::string& lrc_content);

    static uint32_t parse_time_to_ms(const std::string& time_str);

    static int find_active_line_index(const LyricsTrackData& lyrics, uint32_t current_ms);
    static float get_word_fill_ratio(const TtmlWordSpan& word, uint32_t current_ms);
    static float get_line_fill_ratio(const LyricsLine& line, uint32_t current_ms);
};

#endif // LYRICS_ENGINE_H
