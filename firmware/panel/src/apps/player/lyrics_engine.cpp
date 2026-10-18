#include "apps/player/lyrics_engine.h"
#include "driver/utf8_utils.h"
#include "esp_log.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cctype>

static const char* TAG = "LYRICS_ENGINE";

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

uint32_t LyricsEngine::parse_time_to_ms(const std::string& time_str) {
    std::string s = trim(time_str);
    if (s.empty()) return 0;

    // Check for millisecond suffix (e.g. "83450ms")
    if (s.length() > 2 && s.substr(s.length() - 2) == "ms") {
        return (uint32_t)std::stoul(s.substr(0, s.length() - 2));
    }
    // Check for second suffix (e.g. "83.45s")
    if (s.back() == 's') {
        float sec = std::stof(s.substr(0, s.length() - 1));
        return (uint32_t)(sec * 1000.0f);
    }

    uint32_t h = 0, m = 0, sec = 0, ms = 0;
    int colons = 0;
    for (char c : s) if (c == ':') colons++;

    if (colons == 2) {
        // HH:MM:SS.mmm or HH:MM:SS:FF
        if (sscanf(s.c_str(), "%lu:%lu:%lu.%lu", (unsigned long*)&h, (unsigned long*)&m, (unsigned long*)&sec, (unsigned long*)&ms) >= 3) {
            if (ms < 100 && s.find('.') != std::string::npos) {
                size_t dotPos = s.find('.');
                std::string sub = s.substr(dotPos + 1);
                if (sub.length() == 1) ms *= 100;
                else if (sub.length() == 2) ms *= 10;
            }
            return h * 3600000 + m * 60000 + sec * 1000 + ms;
        }
    } else if (colons == 1) {
        // MM:SS.mmm
        if (sscanf(s.c_str(), "%lu:%lu.%lu", (unsigned long*)&m, (unsigned long*)&sec, (unsigned long*)&ms) >= 2) {
            if (s.find('.') != std::string::npos) {
                size_t dotPos = s.find('.');
                std::string sub = s.substr(dotPos + 1);
                if (sub.length() == 1) ms *= 100;
                else if (sub.length() == 2) ms *= 10;
            }
            return m * 60000 + sec * 1000 + ms;
        }
    } else {
        // Raw seconds
        float fsec = 0.0f;
        if (sscanf(s.c_str(), "%f", &fsec) == 1) {
            return (uint32_t)(fsec * 1000.0f);
        }
    }

    return 0;
}

LyricsTrackData LyricsEngine::parse_ttml(const std::string& ttml_xml) {
    LyricsTrackData result;
    result.mode = LYRICS_MODE_TTML_KARAOKE;
    result.source_info = "TTML Karaoke Engine";

    size_t pos = 0;
    while ((pos = ttml_xml.find("<p", pos)) != std::string::npos) {
        size_t pEndTag = ttml_xml.find("</p>", pos);
        if (pEndTag == std::string::npos) break;

        std::string pContent = ttml_xml.substr(pos, pEndTag - pos + 4);
        pos = pEndTag + 4;

        // Parse p attributes: begin="..." end="..." or dur="..."
        size_t bPos = pContent.find("begin=\"");
        size_t ePos = pContent.find("end=\"");
        size_t dPos = pContent.find("dur=\"");

        uint32_t lineStart = 0;
        uint32_t lineEnd = 0;

        if (bPos != std::string::npos) {
            size_t bValEnd = pContent.find("\"", bPos + 7);
            if (bValEnd != std::string::npos) {
                lineStart = parse_time_to_ms(pContent.substr(bPos + 7, bValEnd - bPos - 7));
            }
        }
        if (ePos != std::string::npos) {
            size_t eValEnd = pContent.find("\"", ePos + 5);
            if (eValEnd != std::string::npos) {
                lineEnd = parse_time_to_ms(pContent.substr(ePos + 5, eValEnd - ePos - 5));
            }
        } else if (dPos != std::string::npos && lineStart > 0) {
            size_t dValEnd = pContent.find("\"", dPos + 5);
            if (dValEnd != std::string::npos) {
                uint32_t dur = parse_time_to_ms(pContent.substr(dPos + 5, dValEnd - dPos - 5));
                lineEnd = lineStart + dur;
            }
        }

        LyricsLine line;
        line.start_ms = lineStart;
        line.end_ms = lineEnd;

        // Parse <span> words inside <p>
        size_t spanPos = 0;
        std::string fullText;

        while ((spanPos = pContent.find("<span", spanPos)) != std::string::npos) {
            size_t spanCloseAttr = pContent.find(">", spanPos);
            size_t spanEndTag = pContent.find("</span>", spanCloseAttr);
            if (spanCloseAttr == std::string::npos || spanEndTag == std::string::npos) break;

            std::string attrStr = pContent.substr(spanPos, spanCloseAttr - spanPos);
            std::string wordText = pContent.substr(spanCloseAttr + 1, spanEndTag - spanCloseAttr - 1);

            uint32_t wordStart = lineStart;
            uint32_t wordEnd = lineEnd;

            size_t sbPos = attrStr.find("begin=\"");
            size_t sePos = attrStr.find("end=\"");
            size_t sdPos = attrStr.find("dur=\"");

            if (sbPos != std::string::npos) {
                size_t sbValEnd = attrStr.find("\"", sbPos + 7);
                if (sbValEnd != std::string::npos) {
                    wordStart = parse_time_to_ms(attrStr.substr(sbPos + 7, sbValEnd - sbPos - 7));
                }
            }
            if (sePos != std::string::npos) {
                size_t seValEnd = attrStr.find("\"", sePos + 5);
                if (seValEnd != std::string::npos) {
                    wordEnd = parse_time_to_ms(attrStr.substr(sePos + 5, seValEnd - sePos - 5));
                }
            } else if (sdPos != std::string::npos) {
                size_t sdValEnd = attrStr.find("\"", sdPos + 5);
                if (sdValEnd != std::string::npos) {
                    uint32_t wDur = parse_time_to_ms(attrStr.substr(sdPos + 5, sdValEnd - sdPos - 5));
                    wordEnd = wordStart + wDur;
                }
            }

            TtmlWordSpan span;
            span.start_ms = wordStart;
            span.end_ms = wordEnd;
            span.text = sanitize_utf8_string(trim(wordText));

            line.words.push_back(span);
            if (!fullText.empty()) fullText += " ";
            fullText += span.text;

            spanPos = spanEndTag + 7;
        }

        if (line.words.empty()) {
            // No <span> tags, extract plain text inside <p>
            size_t contentStart = pContent.find(">");
            size_t contentEnd = pContent.rfind("</p>");
            if (contentStart != std::string::npos && contentEnd != std::string::npos && contentEnd > contentStart) {
                fullText = sanitize_utf8_string(trim(pContent.substr(contentStart + 1, contentEnd - contentStart - 1)));
            }
        }

        line.full_text = fullText;
        if (line.end_ms == 0 && !line.words.empty()) {
            line.end_ms = line.words.back().end_ms;
        }

        if (!line.full_text.empty()) {
            result.lines.push_back(line);
        }
    }

    std::sort(result.lines.begin(), result.lines.end(), [](const LyricsLine& a, const LyricsLine& b) {
        return a.start_ms < b.start_ms;
    });

    bool hasWordTimestamps = false;
    for (const auto& l : result.lines) {
        if (!l.words.empty()) {
            for (const auto& w : l.words) {
                if (w.start_ms != l.start_ms || w.end_ms != l.end_ms) {
                    hasWordTimestamps = true;
                    break;
                }
            }
        }
        if (hasWordTimestamps) break;
    }

    if (result.lines.empty()) {
        result.mode = LYRICS_MODE_UNSUPPORTED;
    } else if (hasWordTimestamps) {
        result.mode = LYRICS_MODE_TTML_KARAOKE;
        result.source_info = "TTML Word Karaoke Engine";
    } else {
        result.mode = LYRICS_MODE_LINE_STANDARD;
        result.source_info = "TTML Line Standard Engine";
    }

    return result;
}

LyricsTrackData LyricsEngine::parse_lrc(const std::string& lrc_content) {
    LyricsTrackData result;
    result.mode = LYRICS_MODE_LINE_STANDARD;
    result.source_info = "LRC Line Engine";

    std::stringstream ss(lrc_content);
    std::string rawLine;

    while (std::getline(ss, rawLine)) {
        std::string s = trim(rawLine);
        if (s.empty() || s[0] != '[') continue;

        size_t closeB = s.find(']');
        if (closeB == std::string::npos) continue;

        std::string timeTag = s.substr(1, closeB - 1);
        std::string lineText = trim(s.substr(closeB + 1));

        uint32_t lineStart = parse_time_to_ms(timeTag);
        if (lineStart > 0 || timeTag.find("00:") == 0 || timeTag.find("0:") == 0) {
            LyricsLine line;
            line.start_ms = lineStart;
            line.full_text = sanitize_utf8_string(lineText);

            // Parse word-level tags if present: <00:12.34>Word
            size_t wPos = 0;
            while ((wPos = lineText.find('<', wPos)) != std::string::npos) {
                size_t wClose = lineText.find('>', wPos);
                if (wClose == std::string::npos) break;

                std::string wTimeStr = lineText.substr(wPos + 1, wClose - wPos - 1);
                uint32_t wStart = parse_time_to_ms(wTimeStr);

                size_t nextTag = lineText.find('<', wClose);
                std::string wordStr = (nextTag != std::string::npos)
                    ? lineText.substr(wClose + 1, nextTag - wClose - 1)
                    : lineText.substr(wClose + 1);

                TtmlWordSpan span;
                span.start_ms = wStart;
                span.end_ms = wStart + 500; // Default estimate until next word
                span.text = sanitize_utf8_string(trim(wordStr));
                line.words.push_back(span);

                wPos = wClose + 1;
            }

            for (size_t i = 0; i < line.words.size(); i++) {
                if (i + 1 < line.words.size()) {
                    line.words[i].end_ms = line.words[i + 1].start_ms;
                } else {
                    line.words[i].end_ms = line.words[i].start_ms + 1000;
                }
            }

            result.lines.push_back(line);
        }
    }

    std::sort(result.lines.begin(), result.lines.end(), [](const LyricsLine& a, const LyricsLine& b) {
        return a.start_ms < b.start_ms;
    });

    for (size_t i = 0; i < result.lines.size(); i++) {
        if (i + 1 < result.lines.size()) {
            result.lines[i].end_ms = result.lines[i + 1].start_ms;
        } else {
            result.lines[i].end_ms = result.lines[i].start_ms + 5000;
        }
    }

    if (result.lines.empty()) {
        result.mode = LYRICS_MODE_UNSUPPORTED;
    }

    return result;
}

LyricsTrackData LyricsEngine::discover_and_parse(
    const std::string& audio_filepath,
    const std::string& vorbis_synced_lyrics,
    const std::string& vorbis_unsynced_lyrics
) {
    ESP_LOGI(TAG, "Executing Priority Fallback Discovery for %s", audio_filepath.c_str());

    size_t dotPos = audio_filepath.find_last_of('.');
    std::string basePath = (dotPos != std::string::npos) ? audio_filepath.substr(0, dotPos) : audio_filepath;

    // --- Priority 1: External .ttml ---
    std::string ttmlPath = basePath + ".ttml";
    std::ifstream tfile(ttmlPath);
    if (tfile.is_open()) {
        ESP_LOGI(TAG, "[Priority 1 Match] Found external TTML: %s", ttmlPath.c_str());
        std::stringstream buf;
        buf << tfile.rdbuf();
        tfile.close();
        LyricsTrackData res = parse_ttml(buf.str());
        if (res.is_valid()) return res;
    }

    // --- Priority 2: External .lrc ---
    std::string lrcPath = basePath + ".lrc";
    std::ifstream lfile(lrcPath);
    if (lfile.is_open()) {
        ESP_LOGI(TAG, "[Priority 2 Match] Found external LRC: %s", lrcPath.c_str());
        std::stringstream buf;
        buf << lfile.rdbuf();
        lfile.close();
        LyricsTrackData res = parse_lrc(buf.str());
        if (res.is_valid()) return res;
    }

    // --- Priority 3: Embedded Vorbis Comment FLAC Tags ---
    if (!vorbis_synced_lyrics.empty()) {
        ESP_LOGI(TAG, "[Priority 3 Match] Using embedded Vorbis SYNCEDLYRICS");
        LyricsTrackData res = parse_lrc(vorbis_synced_lyrics);
        if (res.is_valid()) return res;
    }
    if (!vorbis_unsynced_lyrics.empty()) {
        ESP_LOGI(TAG, "[Priority 3 Match] Using embedded Vorbis UNSYNCEDLYRICS");
        LyricsTrackData res = parse_lrc(vorbis_unsynced_lyrics);
        if (res.is_valid()) return res;
    }

    // --- Priority 4: Unsupported ---
    ESP_LOGI(TAG, "[Priority 4 Fallback] No lyrics found. Mode = LYRICS_UNSUPPORTED");
    LyricsTrackData unsupported;
    unsupported.mode = LYRICS_MODE_UNSUPPORTED;
    unsupported.source_info = "No Lyrics Available";
    return unsupported;
}

int LyricsEngine::find_active_line_index(const LyricsTrackData& lyrics, uint32_t current_ms) {
    if (!lyrics.is_valid()) return -1;
    int activeIdx = -1;
    for (size_t i = 0; i < lyrics.lines.size(); i++) {
        if (current_ms >= lyrics.lines[i].start_ms) {
            activeIdx = (int)i;
        } else {
            break;
        }
    }
    return activeIdx;
}

float LyricsEngine::get_word_fill_ratio(const TtmlWordSpan& word, uint32_t current_ms) {
    if (current_ms <= word.start_ms) return 0.0f;
    if (current_ms >= word.end_ms) return 1.0f;
    uint32_t dur = word.end_ms - word.start_ms;
    if (dur == 0) return 1.0f;
    return (float)(current_ms - word.start_ms) / (float)dur;
}

float LyricsEngine::get_line_fill_ratio(const LyricsLine& line, uint32_t current_ms) {
    if (current_ms <= line.start_ms) return 0.0f;
    if (current_ms >= line.end_ms) return 1.0f;
    uint32_t dur = line.end_ms - line.start_ms;
    if (dur == 0) return 1.0f;
    return (float)(current_ms - line.start_ms) / (float)dur;
}
