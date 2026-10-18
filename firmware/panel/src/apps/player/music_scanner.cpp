#include "music_scanner.h"
#include "driver/utf8_utils.h"
#include "cJSON.h"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cctype>

static const char* DB_DIR_PATH = "/sdcard/Jacktor/data/music";
static const char* DB_FILE_PATH = "/sdcard/Jacktor/data/music/library.json";
static const char* MUSIC_ROOT_PATH = "/sdcard/Music";

static std::string trim_str(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static void ensure_directory_exists(const std::string& path) {
    std::string current;
    std::stringstream ss(path);
    std::string token;
    while (std::getline(ss, token, '/')) {
        if (token.empty()) continue;
        current += "/" + token;
        struct stat st;
        if (stat(current.c_str(), &st) != 0) {
            mkdir(current.c_str(), 0755);
        }
    }
}

std::vector<LrcLine> MusicScanner::parse_lrc_string(const std::string& lrc_content) {
    std::vector<LrcLine> lines;
    std::stringstream ss(lrc_content);
    std::string rawLine;

    while (std::getline(ss, rawLine)) {
        rawLine = trim_str(rawLine);
        if (rawLine.empty() || rawLine[0] != '[') continue;

        size_t closeBracket = rawLine.find(']');
        if (closeBracket == std::string::npos) continue;

        std::string timeStr = rawLine.substr(1, closeBracket - 1);
        std::string textStr = trim_str(rawLine.substr(closeBracket + 1));

        unsigned int min = 0, sec = 0, ms = 0;
        if (sscanf(timeStr.c_str(), "%u:%u.%u", &min, &sec, &ms) >= 2) {
            uint32_t totalMs = min * 60000 + sec * 1000 + (ms < 100 ? ms * 10 : ms);
            LrcLine line;
            line.time_ms = totalMs;
            line.text = textStr;

            size_t pos = 0;
            while ((pos = textStr.find('<', pos)) != std::string::npos) {
                size_t endTag = textStr.find('>', pos);
                if (endTag == std::string::npos) break;
                std::string tag = textStr.substr(pos + 1, endTag - pos - 1);
                unsigned int wMin = 0, wSec = 0, wMs = 0;
                unsigned int wTotal = 0;
                if (sscanf(tag.c_str(), "%u:%u.%u", &wMin, &wSec, &wMs) >= 2) {
                    wTotal = wMin * 60000 + wSec * 1000 + (wMs < 100 ? wMs * 10 : wMs);
                } else if (sscanf(tag.c_str(), "%u", &wTotal) == 1) {
                }

                size_t nextTag = textStr.find('<', endTag);
                std::string wordStr = (nextTag != std::string::npos) ? textStr.substr(endTag + 1, nextTag - endTag - 1) : textStr.substr(endTag + 1);
                KaraokeWord kw;
                kw.start_ms = wTotal;
                kw.word = trim_str(wordStr);
                line.words.push_back(kw);

                pos = endTag + 1;
            }

            lines.push_back(line);
        }
    }

    std::sort(lines.begin(), lines.end(), [](const LrcLine& a, const LrcLine& b) {
        return a.time_ms < b.time_ms;
    });

    return lines;
}

std::vector<TrackMetadata> MusicScanner::load_library() {
    std::vector<TrackMetadata> library;
    std::ifstream file(DB_FILE_PATH);
    if (!file.is_open()) return library;

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    cJSON* root = cJSON_Parse(buffer.str().c_str());
    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        return library;
    }

    int count = cJSON_GetArraySize(root);
    for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsObject(item)) continue;

        TrackMetadata t;
        cJSON* path = cJSON_GetObjectItem(item, "full_path");
        if (cJSON_IsString(path) && path->valuestring) t.full_path = sanitize_utf8_string(path->valuestring);
        else continue;

        cJSON* fn = cJSON_GetObjectItem(item, "filename");
        if (cJSON_IsString(fn) && fn->valuestring) t.filename = sanitize_utf8_string(fn->valuestring);

        cJSON* title = cJSON_GetObjectItem(item, "title");
        if (cJSON_IsString(title) && title->valuestring) t.title = sanitize_utf8_string(title->valuestring);

        cJSON* artist = cJSON_GetObjectItem(item, "artist");
        if (cJSON_IsString(artist) && artist->valuestring) t.artist = sanitize_utf8_string(artist->valuestring);

        cJSON* album = cJSON_GetObjectItem(item, "album");
        if (cJSON_IsString(album) && album->valuestring) t.album = sanitize_utf8_string(album->valuestring);

        cJSON* genre = cJSON_GetObjectItem(item, "genre");
        if (cJSON_IsString(genre) && genre->valuestring) t.genre = sanitize_utf8_string(genre->valuestring);

        cJSON* albArtist = cJSON_GetObjectItem(item, "album_artist");
        if (cJSON_IsString(albArtist) && albArtist->valuestring) t.album_artist = sanitize_utf8_string(albArtist->valuestring);

        cJSON* tn = cJSON_GetObjectItem(item, "track_num");
        if (cJSON_IsNumber(tn)) t.track_num = (uint32_t)tn->valuedouble;

        cJSON* sr = cJSON_GetObjectItem(item, "sample_rate");
        if (cJSON_IsNumber(sr)) t.sample_rate = (uint32_t)sr->valuedouble;

        cJSON* bd = cJSON_GetObjectItem(item, "bit_depth");
        if (cJSON_IsNumber(bd)) t.bit_depth = (uint8_t)bd->valuedouble;

        cJSON* ch = cJSON_GetObjectItem(item, "channels");
        if (cJSON_IsNumber(ch)) t.channels = (uint8_t)ch->valuedouble;

        cJSON* dur = cJSON_GetObjectItem(item, "duration_sec");
        if (cJSON_IsNumber(dur)) t.duration_sec = (uint32_t)dur->valuedouble;

        cJSON* br = cJSON_GetObjectItem(item, "bitrate_kbps");
        if (cJSON_IsNumber(br)) t.bitrate_kbps = (uint32_t)br->valuedouble;

        cJSON* cov = cJSON_GetObjectItem(item, "cover_path");
        if (cJSON_IsString(cov) && cov->valuestring) t.cover_path = cov->valuestring;

        cJSON* lrcP = cJSON_GetObjectItem(item, "lrc_path");
        if (cJSON_IsString(lrcP) && lrcP->valuestring) t.lrc_path = lrcP->valuestring;

        cJSON* lrcS = cJSON_GetObjectItem(item, "embedded_lyrics_synced");
        if (cJSON_IsString(lrcS) && lrcS->valuestring) t.embedded_lyrics_synced = lrcS->valuestring;

        cJSON* lrcU = cJSON_GetObjectItem(item, "embedded_lyrics_unsynced");
        if (cJSON_IsString(lrcU) && lrcU->valuestring) t.embedded_lyrics_unsynced = lrcU->valuestring;

        library.push_back(t);
    }

    cJSON_Delete(root);
    return library;
}

bool MusicScanner::save_library(const std::vector<TrackMetadata>& library) {
    ensure_directory_exists(DB_DIR_PATH);

    cJSON* root = cJSON_CreateArray();
    if (!root) return false;

    for (const auto& track : library) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "full_path", track.full_path.c_str());
        cJSON_AddStringToObject(item, "filename", track.filename.c_str());
        cJSON_AddStringToObject(item, "title", track.title.c_str());
        cJSON_AddStringToObject(item, "artist", track.artist.c_str());
        cJSON_AddStringToObject(item, "album", track.album.c_str());
        cJSON_AddStringToObject(item, "genre", track.genre.c_str());
        cJSON_AddStringToObject(item, "album_artist", track.album_artist.c_str());
        cJSON_AddNumberToObject(item, "track_num", track.track_num);
        cJSON_AddNumberToObject(item, "sample_rate", track.sample_rate);
        cJSON_AddNumberToObject(item, "bit_depth", track.bit_depth);
        cJSON_AddNumberToObject(item, "channels", track.channels);
        cJSON_AddNumberToObject(item, "duration_sec", track.duration_sec);
        cJSON_AddNumberToObject(item, "bitrate_kbps", track.bitrate_kbps);
        cJSON_AddStringToObject(item, "cover_path", track.cover_path.c_str());
        cJSON_AddStringToObject(item, "lrc_path", track.lrc_path.c_str());
        cJSON_AddStringToObject(item, "embedded_lyrics_synced", track.embedded_lyrics_synced.c_str());
        cJSON_AddStringToObject(item, "embedded_lyrics_unsynced", track.embedded_lyrics_unsynced.c_str());

        cJSON_AddItemToArray(root, item);
    }

    char* rendered = cJSON_Print(root);
    cJSON_Delete(root);

    if (!rendered) return false;

    std::ofstream file(DB_FILE_PATH, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        free(rendered);
        return false;
    }

    file << rendered;
    file.close();
    free(rendered);
    return true;
}

std::vector<std::string> MusicScanner::scan_directory_files(const std::string& root_dir) {
    std::vector<std::string> file_paths;
    DIR* dir = opendir(root_dir.c_str());
    if (!dir) return file_paths;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        std::string full_path = root_dir + "/" + entry->d_name;

        struct stat st;
        if (stat(full_path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                std::vector<std::string> sub_files = scan_directory_files(full_path);
                file_paths.insert(file_paths.end(), sub_files.begin(), sub_files.end());
            } else if (S_ISREG(st.st_mode)) {
                size_t len = full_path.length();
                if (len >= 5) {
                    std::string ext5 = full_path.substr(len - 5);
                    std::string ext4 = full_path.substr(len - 4);
                    std::transform(ext5.begin(), ext5.end(), ext5.begin(), ::tolower);
                    std::transform(ext4.begin(), ext4.end(), ext4.begin(), ::tolower);

                    if (ext5 == ".flac" || ext4 == ".ogg") {
                        file_paths.push_back(full_path);
                    }
                }
            }
        }
    }
    closedir(dir);
    return file_paths;
}

void MusicScanner::parse_flac_vorbis_comment(const uint8_t* comment_data, size_t length, TrackMetadata& meta) {
    if (length < 8) return;
    uint32_t vendor_len = comment_data[0] | (comment_data[1] << 8) | (comment_data[2] << 16) | (comment_data[3] << 24);
    if (vendor_len > length - 4) return;
    size_t pos = 4 + vendor_len;
    if (pos + 4 > length) return;

    uint32_t comment_count = comment_data[pos] | (comment_data[pos + 1] << 8) | (comment_data[pos + 2] << 16) | (comment_data[pos + 3] << 24);
    pos += 4;

    for (uint32_t i = 0; i < comment_count; i++) {
        if (pos + 4 > length) break;
        uint32_t str_len = comment_data[pos] | (comment_data[pos + 1] << 8) | (comment_data[pos + 2] << 16) | (comment_data[pos + 3] << 24);
        pos += 4;
        if (pos + str_len > length) break;

        std::string comment((const char*)&comment_data[pos], str_len);
        pos += str_len;

        size_t eq = comment.find('=');
        if (eq != std::string::npos) {
            std::string key = comment.substr(0, eq);
            std::string val = comment.substr(eq + 1);
            std::transform(key.begin(), key.end(), key.begin(), ::toupper);

            if (key == "TITLE") meta.title = sanitize_utf8_string(val);
            else if (key == "ARTIST") meta.artist = sanitize_utf8_string(val);
            else if (key == "ALBUM") meta.album = sanitize_utf8_string(val);
            else if (key == "GENRE") meta.genre = sanitize_utf8_string(val);
            else if (key == "ALBUMARTIST" || key == "ALBUM ARTIST") meta.album_artist = sanitize_utf8_string(val);
            else if (key == "TRACKNUMBER") meta.track_num = std::atoi(val.c_str());
            else if (key == "SYNCEDLYRICS") {
                meta.embedded_lyrics_synced = val;
            } else if (key == "LYRICS" || key == "UNSYNCEDLYRICS") {
                meta.embedded_lyrics_unsynced = val;
            }
        }
    }
}

TrackMetadata MusicScanner::extract_metadata(const std::string& filepath) {
    TrackMetadata meta;
    meta.full_path = filepath;

    size_t slash = filepath.find_last_of('/');
    meta.filename = (slash != std::string::npos) ? filepath.substr(slash + 1) : filepath;

    size_t dot = meta.filename.find_last_of('.');
    meta.title = (dot != std::string::npos) ? meta.filename.substr(0, dot) : meta.filename;
    meta.artist = "Unknown Artist";
    meta.album = "Unknown Album";
    meta.genre = "Unknown Genre";
    meta.album_artist = "Unknown Album Artist";
    meta.track_num = 1;
    meta.sample_rate = 44100;
    meta.bit_depth = 16;
    meta.channels = 2;
    meta.duration_sec = 180;
    meta.bitrate_kbps = 1411;

    std::string parent_dir = (slash != std::string::npos) ? filepath.substr(0, slash) : "/sdcard/Music";
    struct stat st;
    std::string coverJpg = parent_dir + "/cover.jpg";
    std::string coverPng = parent_dir + "/cover.png";
    std::string coverJpeg = parent_dir + "/cover.jpeg";
    if (stat(coverJpg.c_str(), &st) == 0) meta.cover_path = coverJpg;
    else if (stat(coverPng.c_str(), &st) == 0) meta.cover_path = coverPng;
    else if (stat(coverJpeg.c_str(), &st) == 0) meta.cover_path = coverJpeg;

    std::string lrcPath = (dot != std::string::npos) ? filepath.substr(0, filepath.length() - (meta.filename.length() - dot)) + ".lrc" : filepath + ".lrc";
    if (stat(lrcPath.c_str(), &st) == 0) {
        meta.lrc_path = lrcPath;
    }

    std::ifstream file(filepath, std::ios::binary);
    if (file.is_open()) {
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        char header[4];
        if (file.read(header, 4) && memcmp(header, "fLaC", 4) == 0) {
            bool is_last = false;
            while (!is_last && file.good()) {
                uint8_t block_header[4];
                if (!file.read((char*)block_header, 4)) break;

                is_last = (block_header[0] & 0x80) != 0;
                uint8_t block_type = block_header[0] & 0x7F;
                uint32_t block_length = (block_header[1] << 16) | (block_header[2] << 8) | block_header[3];

                if (block_type == 0) { // STREAMINFO
                    if (block_length >= 18) {
                        uint8_t streaminfo[18];
                        file.read((char*)streaminfo, 18);
                        meta.sample_rate = (streaminfo[10] << 12) | (streaminfo[11] << 4) | (streaminfo[12] >> 4);
                        meta.channels = ((streaminfo[12] >> 1) & 0x07) + 1;
                        meta.bit_depth = (((streaminfo[12] & 0x01) << 4) | (streaminfo[13] >> 4)) + 1;
                        uint64_t total_samples = (((uint64_t)(streaminfo[13] & 0x0F)) << 32) |
                                                (((uint64_t)streaminfo[14]) << 24) |
                                                (((uint64_t)streaminfo[15]) << 16) |
                                                (((uint64_t)streaminfo[16]) << 8) |
                                                ((uint64_t)streaminfo[17]);
                        if (meta.sample_rate > 0) {
                            meta.duration_sec = (uint32_t)(total_samples / meta.sample_rate);
                        }
                        if (meta.duration_sec > 0) {
                            meta.bitrate_kbps = (uint32_t)((fileSize * 8) / (meta.duration_sec * 1000));
                        }
                        if (block_length > 18) file.seekg(block_length - 18, std::ios::cur);
                    } else {
                        file.seekg(block_length, std::ios::cur);
                    }
                } else if (block_type == 4) { // VORBIS_COMMENT
                    std::vector<uint8_t> comment_buf(block_length);
                    file.read((char*)comment_buf.data(), block_length);
                    parse_flac_vorbis_comment(comment_buf.data(), block_length, meta);
                } else {
                    file.seekg(block_length, std::ios::cur);
                }
            }
        }
    }

    return meta;
}

std::vector<TrackMetadata> MusicScanner::full_scan(ScanProgressCallback callback) {
    remove(DB_FILE_PATH);

    std::vector<std::string> audio_files = scan_directory_files(MUSIC_ROOT_PATH);
    std::vector<TrackMetadata> tracks;
    size_t total = audio_files.size();

    for (size_t i = 0; i < total; i++) {
        if (callback) callback(i + 1, total, audio_files[i]);
        tracks.push_back(extract_metadata(audio_files[i]));
    }

    std::sort(tracks.begin(), tracks.end(), [](const TrackMetadata& a, const TrackMetadata& b) {
        if (a.artist != b.artist) return a.artist < b.artist;
        if (a.album != b.album) return a.album < b.album;
        if (a.track_num != b.track_num) return a.track_num < b.track_num;
        return a.title < b.title;
    });

    save_library(tracks);
    return tracks;
}

std::vector<TrackMetadata> MusicScanner::partial_scan(ScanProgressCallback callback) {
    std::vector<TrackMetadata> existing = load_library();
    std::vector<std::string> audio_files = scan_directory_files(MUSIC_ROOT_PATH);

    std::vector<TrackMetadata> updated;
    size_t total = audio_files.size();

    for (size_t i = 0; i < total; i++) {
        const std::string& filePath = audio_files[i];
        if (callback) callback(i + 1, total, filePath);

        auto it = std::find_if(existing.begin(), existing.end(), [&filePath](const TrackMetadata& t) {
            return t.full_path == filePath;
        });

        if (it != existing.end()) {
            updated.push_back(*it);
        } else {
            updated.push_back(extract_metadata(filePath));
        }
    }

    std::sort(updated.begin(), updated.end(), [](const TrackMetadata& a, const TrackMetadata& b) {
        if (a.artist != b.artist) return a.artist < b.artist;
        if (a.album != b.album) return a.album < b.album;
        if (a.track_num != b.track_num) return a.track_num < b.track_num;
        return a.title < b.title;
    });

    save_library(updated);
    return updated;
}
