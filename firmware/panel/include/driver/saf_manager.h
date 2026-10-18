#ifndef SAF_MANAGER_H
#define SAF_MANAGER_H

#include <string>
#include <vector>
#include <cstdint>
#include <stdbool.h>

enum SafStorageSource {
    SAF_SOURCE_SD_CARD = 0,
    SAF_SOURCE_USB_MASS_STORAGE = 1,
    SAF_SOURCE_SMB = 2,
    SAF_SOURCE_ALL = 3
};

struct SmbConfig {
    std::string ip;
    std::string username;
    std::string password;
    std::string share_name;
    bool connected = false;
};

struct SafFileEntry {
    std::string name;
    std::string full_path;
    bool is_directory = false;
    uint64_t size_bytes = 0;
    SafStorageSource source = SAF_SOURCE_SD_CARD;
};

void saf_manager_init(void);

SafStorageSource saf_get_active_source(void);
void saf_set_active_source(SafStorageSource source);

bool saf_is_source_available(SafStorageSource source);
bool saf_is_sd_card_mounted(void);

SmbConfig saf_get_smb_config(void);
void saf_save_smb_config(const SmbConfig& config);

std::vector<SafFileEntry> saf_list_directory(const std::string& relative_path, SafStorageSource source);
bool saf_stat_file(const std::string& path, SafFileEntry* entry);

// File System Operations (New, Delete, Rename, Copy, Move across storages)
bool saf_create_directory(const std::string& full_path);
bool saf_delete_entry(const std::string& full_path);
bool saf_rename_entry(const std::string& old_path, const std::string& new_path);
bool saf_copy_entry(const std::string& src_path, const std::string& dest_path);
bool saf_move_entry(const std::string& src_path, const std::string& dest_path);

// Drive Storage Capacity Info & Formatting Tools
enum SafFsFormat { SAF_FS_FAT32 = 0, SAF_FS_EXFAT = 1 };

struct SafDriveInfo {
    SafStorageSource source;
    std::string label;
    std::string root_path;
    bool mounted = false;
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    std::string fs_type = "FAT32";
};

std::vector<SafDriveInfo> saf_get_available_drives(void);
bool saf_format_drive(SafStorageSource source, SafFsFormat format_type);

#endif // SAF_MANAGER_H
