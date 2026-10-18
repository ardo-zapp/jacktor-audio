#include "driver/saf_manager.h"
#include "panel_main.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <algorithm>

static const char* NVS_NAMESPACE = "saf_cfg";
static SafStorageSource currentSafSource = SAF_SOURCE_ALL;
static SmbConfig smbConfig;

void saf_manager_init(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t src = 3;
        nvs_get_u8(handle, "active_src", &src);
        currentSafSource = (SafStorageSource)src;

        char buf[128];
        size_t len = sizeof(buf);
        if (nvs_get_str(handle, "smb_ip", buf, &len) == ESP_OK) smbConfig.ip = buf;
        len = sizeof(buf);
        if (nvs_get_str(handle, "smb_user", buf, &len) == ESP_OK) smbConfig.username = buf;
        len = sizeof(buf);
        if (nvs_get_str(handle, "smb_pass", buf, &len) == ESP_OK) smbConfig.password = buf;
        len = sizeof(buf);
        if (nvs_get_str(handle, "smb_share", buf, &len) == ESP_OK) smbConfig.share_name = buf;

        nvs_close(handle);
    }
}

SafStorageSource saf_get_active_source(void) {
    return currentSafSource;
}

void saf_set_active_source(SafStorageSource source) {
    currentSafSource = source;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "active_src", (uint8_t)source);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

bool saf_is_sd_card_mounted(void) {
    struct stat st;
    return (stat("/sdcard", &st) == 0);
}

bool saf_is_source_available(SafStorageSource source) {
    switch (source) {
        case SAF_SOURCE_SD_CARD:
            return saf_is_sd_card_mounted();
        case SAF_SOURCE_USB_MASS_STORAGE: {
            struct stat st;
            return (stat("/usbstorage", &st) == 0);
        }
        case SAF_SOURCE_SMB:
            return !smbConfig.ip.empty() && smbConfig.connected;
        case SAF_SOURCE_ALL:
            return saf_is_source_available(SAF_SOURCE_SD_CARD) ||
                   saf_is_source_available(SAF_SOURCE_USB_MASS_STORAGE) ||
                   saf_is_source_available(SAF_SOURCE_SMB);
    }
    return false;
}

std::vector<SafDriveInfo> saf_get_available_drives(void) {
    std::vector<SafDriveInfo> drives;

    // 1. SD Card Drive
    SafDriveInfo sd;
    sd.source = SAF_SOURCE_SD_CARD;
    sd.label = "SD Card";
    sd.root_path = "/sdcard";
    sd.mounted = panel_hardware_get_sd_info(&sd.total_bytes, &sd.free_bytes);
    sd.fs_type = "FAT32 / exFAT";
    drives.push_back(sd);

    // 2. USB Mass Storage Drive
    SafDriveInfo usb;
    usb.source = SAF_SOURCE_USB_MASS_STORAGE;
    usb.label = "USB Mass Storage";
    usb.root_path = "/usbstorage";
    struct statvfs st_usb;
    if (statvfs("/usbstorage", &st_usb) == 0) {
        usb.mounted = true;
        usb.total_bytes = (uint64_t)st_usb.f_blocks * st_usb.f_frsize;
        usb.free_bytes = (uint64_t)st_usb.f_bfree * st_usb.f_frsize;
    } else {
        usb.mounted = false;
    }
    usb.fs_type = "FAT32 / exFAT";
    drives.push_back(usb);

    // 3. SMB Network Share Drive
    SafDriveInfo smb;
    smb.source = SAF_SOURCE_SMB;
    smb.label = "SMB Network Share";
    smb.root_path = "/smb";
    smb.mounted = !smbConfig.ip.empty() && smbConfig.connected;
    smb.total_bytes = 100ULL * 1024 * 1024 * 1024; // 100GB Virtual share
    smb.free_bytes = 50ULL * 1024 * 1024 * 1024;
    smb.fs_type = "SMB / CIFS";
    drives.push_back(smb);

    return drives;
}

bool saf_format_drive(SafStorageSource source, SafFsFormat format_type) {
    if (source == SAF_SOURCE_SD_CARD) {
        (void)format_type;
        return panel_hardware_format_sd();
    }
    return false;
}

SmbConfig saf_get_smb_config(void) {
    return smbConfig;
}

void saf_save_smb_config(const SmbConfig& config) {
    smbConfig = config;
    smbConfig.connected = !config.ip.empty();

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "smb_ip", config.ip.c_str());
        nvs_set_str(handle, "smb_user", config.username.c_str());
        nvs_set_str(handle, "smb_pass", config.password.c_str());
        nvs_set_str(handle, "smb_share", config.share_name.c_str());
        nvs_commit(handle);
        nvs_close(handle);
    }
}

std::vector<SafFileEntry> saf_list_directory(const std::string& relative_path, SafStorageSource source) {
    std::vector<SafFileEntry> entries;

    auto scan_fs = [&](const std::string& root_path, SafStorageSource src_type) {
        std::string full_target = root_path;
        if (!relative_path.empty()) {
            if (relative_path == root_path) {
                full_target = root_path;
            } else if (relative_path[0] == '/') {
                if (relative_path.rfind(root_path, 0) == 0) full_target = relative_path;
                else full_target += relative_path;
            } else {
                full_target += "/" + relative_path;
            }
        }

        DIR* dir = opendir(full_target.c_str());
        if (!dir) return;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;
            SafFileEntry fe;
            fe.name = entry->d_name;
            fe.full_path = full_target + "/" + entry->d_name;
            fe.source = src_type;

            struct stat st;
            if (stat(fe.full_path.c_str(), &st) == 0) {
                fe.is_directory = S_ISDIR(st.st_mode);
                fe.size_bytes = st.st_size;
            }
            entries.push_back(fe);
        }
        closedir(dir);
    };

    if (source == SAF_SOURCE_SD_CARD || source == SAF_SOURCE_ALL) {
        if (saf_is_source_available(SAF_SOURCE_SD_CARD)) scan_fs("/sdcard", SAF_SOURCE_SD_CARD);
    }
    if (source == SAF_SOURCE_USB_MASS_STORAGE || source == SAF_SOURCE_ALL) {
        if (saf_is_source_available(SAF_SOURCE_USB_MASS_STORAGE)) scan_fs("/usbstorage", SAF_SOURCE_USB_MASS_STORAGE);
    }
    if (source == SAF_SOURCE_SMB || source == SAF_SOURCE_ALL) {
        if (saf_is_source_available(SAF_SOURCE_SMB)) scan_fs("/smb", SAF_SOURCE_SMB);
    }

    return entries;
}

bool saf_stat_file(const std::string& path, SafFileEntry* entry) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (entry) {
            size_t slash = path.find_last_of('/');
            entry->name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
            entry->full_path = path;
            entry->is_directory = S_ISDIR(st.st_mode);
            entry->size_bytes = st.st_size;
            if (path.rfind("/sdcard", 0) == 0) entry->source = SAF_SOURCE_SD_CARD;
            else if (path.rfind("/usbstorage", 0) == 0) entry->source = SAF_SOURCE_USB_MASS_STORAGE;
            else if (path.rfind("/smb", 0) == 0) entry->source = SAF_SOURCE_SMB;
        }
        return true;
    }
    return false;
}

bool saf_create_directory(const std::string& full_path) {
    return (mkdir(full_path.c_str(), 0775) == 0);
}

bool saf_delete_entry(const std::string& full_path) {
    struct stat st;
    if (stat(full_path.c_str(), &st) != 0) return false;

    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(full_path.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                std::string child = full_path + "/" + entry->d_name;
                saf_delete_entry(child);
            }
            closedir(dir);
        }
        return (rmdir(full_path.c_str()) == 0);
    } else {
        return (remove(full_path.c_str()) == 0);
    }
}

bool saf_rename_entry(const std::string& old_path, const std::string& new_path) {
    return (rename(old_path.c_str(), new_path.c_str()) == 0);
}

bool saf_copy_entry(const std::string& src_path, const std::string& dest_path) {
    struct stat st;
    if (stat(src_path.c_str(), &st) != 0) return false;

    if (S_ISDIR(st.st_mode)) {
        saf_create_directory(dest_path);
        DIR* dir = opendir(src_path.c_str());
        if (!dir) return false;

        struct dirent* entry;
        bool ok = true;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            std::string sub_src = src_path + "/" + entry->d_name;
            std::string sub_dest = dest_path + "/" + entry->d_name;
            if (!saf_copy_entry(sub_src, sub_dest)) ok = false;
        }
        closedir(dir);
        return ok;
    } else {
        std::ifstream src(src_path, std::ios::binary);
        if (!src.is_open()) return false;

        std::ofstream dest(dest_path, std::ios::binary | std::ios::trunc);
        if (!dest.is_open()) return false;

        char buffer[4096];
        while (src.read(buffer, sizeof(buffer))) {
            dest.write(buffer, src.gcount());
        }
        if (src.gcount() > 0) {
            dest.write(buffer, src.gcount());
        }
        return true;
    }
}

bool saf_move_entry(const std::string& src_path, const std::string& dest_path) {
    // Try fast rename first (same filesystem)
    if (saf_rename_entry(src_path, dest_path)) {
        return true;
    }
    // Cross-storage move: copy then delete source
    if (saf_copy_entry(src_path, dest_path)) {
        return saf_delete_entry(src_path);
    }
    return false;
}
