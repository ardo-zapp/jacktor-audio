#!/system/bin/sh
# ==============================================================================
# Jacktor Audio Framework & DSP Panel - Unified Engine Service
# Author: Ardo Zapp (incorporating zyhk Audio Samplerate Changer & Jitter Silencer)
# Features:
#   1. Bit-Perfect & Hi-Res (384kHz 32-bit Speaker + Dynamic USB DAC)
#   2. Dynamic Remount of audio_policy_configuration.xml & patched libalsautils.so
#   3. Qualcomm Bengal Low-Latency CPU Tuning & Big Core Pinning
#   4. Audio Jitter Silencer & Doze Whitelist
#   5. Safe Idempotent USB OTG Host Mode Engine
#   6. 24/7 Battery Bypass & Infinite Power Mode
#   7. BusyBox WebUI Server (Port 80)
# ==============================================================================

MODDIR=${0%/*}
CONF_DIR="${MODDIR}/config"
mkdir -p "$CONF_DIR" 2>/dev/null
LOGFILE="/data/local/tmp/jacktor-audio.log"

log() {
    echo "$(date '+[%Y-%m-%d %H:%M:%S]') $1" >> "$LOGFILE"
}

log "=== Jacktor Audio Framework Unified Engine Starting ==="

# Clean up any lingering previous instances of service.sh
for p in $(pgrep -f "service.sh"); do
    if [ "$p" != "$$" ]; then
        kill -9 "$p" 2>/dev/null
    fi
done

# ------------------------------------------------------------------------------
# 0. Early Display & Orientation Service (Runs during "Memulai Android")
# ------------------------------------------------------------------------------
apply_early_display_and_stayon() {
    (
        log "Early display worker launched, waiting for WindowManager & SettingsProvider..."
        local applied=0
        for i in $(seq 1 120); do
            if settings get system user_rotation >/dev/null 2>&1; then
                settings put system accelerometer_rotation 0 2>/dev/null
                settings put system user_rotation 1 2>/dev/null
                settings put system screen_off_timeout 2147483647 2>/dev/null
                settings put system font_scale 0.90 2>/dev/null
                settings put secure lockscreen.disabled 1 2>/dev/null
                locksettings set-disabled true 2>/dev/null
                svc power stayon true 2>/dev/null
                
                wm size 720x1460 2>/dev/null
                wm density 256 2>/dev/null
                
                CURR_SIZE="$(wm size 2>/dev/null | grep -o '720x1460')"
                CURR_ROT="$(settings get system user_rotation 2>/dev/null)"
                if [ "$CURR_SIZE" = "720x1460" ] && [ "$CURR_ROT" = "1" ]; then
                    log "Early display settings successfully applied during 'Memulai Android' (attempt $i)!"
                    applied=1
                    break
                fi
            fi
            sleep 1.0
        done
        [ "$applied" = "0" ] && log "Warning: Early display settings timed out, fallback will apply after boot_completed."
    ) &
}
apply_early_display_and_stayon

# ------------------------------------------------------------------------------
# 1. Tunggu Android Selesai Boot
# ------------------------------------------------------------------------------
until [ "$(getprop sys.boot_completed)" = "1" ]; do
    sleep 2
done
log "Android boot completed."
sleep 2

# ------------------------------------------------------------------------------
# 2. Wi-Fi ADB Remote Service & SELinux Audio Policies
# ------------------------------------------------------------------------------
init_wifi_adb_and_sepolicy() {
    CURR_PORT="$(getprop service.adb.tcp.port)"
    if [ "$CURR_PORT" != "5555" ]; then
        log "Enabling Wi-Fi ADB on port 5555..."
        setprop persist.adb.tcp.port 5555
        setprop service.adb.tcp.port 5555
        stop adbd
        start adbd
    fi

    # Berikan izin SELinux lengkap untuk Audio HAL, USB DAC, System Suspend, dan GMS (bebas avc: denied)
    if [ -x /data/adb/ap/bin/magiskpolicy ]; then
        /data/adb/ap/bin/magiskpolicy --live \
            "allow hal_audio_default system_prop file { read open getattr map }" \
            "allow audioserver vendor_default_prop file { read open getattr map }" \
            "allow hal_audio_sysbta vendor_default_prop file { read open getattr map }" \
            "allow system_suspend sysfs dir { read open search getattr }" \
            "allow system_suspend vendor_sysfs_battery_supply dir { read open search getattr }" \
            "allow system_suspend sysfs_net dir { read open search getattr }" \
            "allow zygote vendor_default_prop file { read open getattr map }" \
            "allow hal_graphics_composer_default graphics_config_prop file { read open getattr map }" \
            "allow hal_graphics_composer_default graphics_config_writable_prop file { read open getattr map }" \
            "allow vendor_sensors vendor_diag_device chr_file { read write open ioctl getattr }" \
            "allow hal_sensors_default vendor_diag_device chr_file { read write open ioctl getattr }" \
            "allow gmscore_app system_adbd_prop file { read open getattr map }" \
            "allow gmscore_app adbd_prop file { read open getattr map }" \
            "allow untrusted_app usb_device dir { open read search getattr }" \
            "allow untrusted_app usb_device chr_file { open read write ioctl getattr }" \
            "allow untrusted_app device dir { open read search getattr }" 2>/dev/null
        log "SELinux Direct USB & System Stability rules applied."
    fi

    # Buat direktori log BSP Oplus agar kernel worker tidak error saat menulis bright/blackscreen_count
    mkdir -p /data/oplus/log/bsp 2>/dev/null
    touch /data/oplus/log/bsp/brightscreen_count.txt /data/oplus/log/bsp/blackscreen_count.txt 2>/dev/null
    chmod 666 /data/oplus/log/bsp/brightscreen_count.txt /data/oplus/log/bsp/blackscreen_count.txt 2>/dev/null

    # Berikan izin WRITE_DEVICE_CONFIG ke GMS & matikan sinkronisasi paksa agar tidak memicu SecurityException di logcat
    pm grant com.google.android.gms android.permission.WRITE_DEVICE_CONFIG 2>/dev/null
    pm grant com.google.android.gms android.permission.READ_DEVICE_CONFIG 2>/dev/null
    cmd device_config set_sync_disabled_for_tests persistent 2>/dev/null

    # Matikan interface modem seluler rmnet_ipa0 jika tidak ada kartu SIM (mencegah NETDEV WATCHDOG timeout di kernel)
    SIM_STATE="$(getprop gsm.sim.state 2>/dev/null)"
    if echo "$SIM_STATE" | grep -qE "ABSENT|NOT_READY"; then
        svc data disable 2>/dev/null
        ip link set rmnet_ipa0 down 2>/dev/null
    fi
}
init_wifi_adb_and_sepolicy

# ------------------------------------------------------------------------------
# 2b. OpenSSH Server & SFTP Subsystem (PatchedMagiskSSH Engine)
# ------------------------------------------------------------------------------
init_ssh_subsystem() {
    log "Initializing OpenSSH Server Subsystem..."
    
    # Mount overlayfs on /system/usr so libexec and lib are available without modifying stock /system/usr
    if [ ! -d /system/usr/libexec/ssh-core ]; then
        mount -t overlay overlay -o lowerdir="$MODDIR/system/usr:/system/usr" /system/usr 2>/dev/null
        log "Mounted overlayfs on /system/usr"
    fi

    # Symlink SSH utilities into /data/adb/ap/bin for global terminal access
    mkdir -p /data/adb/ap/bin 2>/dev/null
    for f in sshd ssh scp sftp sftp-server ssh-keygen rsync; do
        ln -sf "/system/usr/libexec/ssh-core/wrapper" "/data/adb/ap/bin/$f" 2>/dev/null
    done

    # Start SSH service if autostart enabled (default: on)
    if [ ! -f /data/adb/ssh/no-autostart ]; then
        "$MODDIR/system/bin/opensshd.init" start >/dev/null 2>&1
        log "OpenSSH server started."
    else
        log "OpenSSH autostart disabled (/data/adb/ssh/no-autostart present)."
    fi
}
init_ssh_subsystem

# ------------------------------------------------------------------------------
# 3. Dynamic Remount of Audio Policy & Unlocked HAL Libraries (Bit-Perfect Mode)
# ------------------------------------------------------------------------------
remount_audio_files() {
    local force_reload=0

    # Pastikan file audio policy sesuai konfigurasi resampler_mode (Match Source vs Tertinggi)
    RES_CONF="${CONF_DIR}/resampler_mode"
    R_MODE="highest"
    [ -f "$RES_CONF" ] && R_MODE="$(cat "$RES_CONF" 2>/dev/null | tr -d ' \r\n')"
    [ -z "$R_MODE" ] && R_MODE="highest" && echo "highest" > "$RES_CONF" 2>/dev/null
    
    ETC_DIR="${MODDIR}/system/vendor/etc"
    TARGET_POLICY="/vendor/etc/audio_policy_configuration.xml"
    if [ "$R_MODE" = "match_source" ] && [ -f "$ETC_DIR/audio_policy_match_source.xml" ]; then
        cat "$ETC_DIR/audio_policy_match_source.xml" > "$ETC_DIR/audio_policy_configuration.xml" 2>/dev/null
        [ -f "$TARGET_POLICY" ] && cat "$ETC_DIR/audio_policy_match_source.xml" > "$TARGET_POLICY" 2>/dev/null
        log "Active audio policy: Match Source (Bit-Perfect Native)"
    elif [ -f "$ETC_DIR/audio_policy_highest.xml" ]; then
        cat "$ETC_DIR/audio_policy_highest.xml" > "$ETC_DIR/audio_policy_configuration.xml" 2>/dev/null
        [ -f "$TARGET_POLICY" ] && cat "$ETC_DIR/audio_policy_highest.xml" > "$TARGET_POLICY" 2>/dev/null
        log "Active audio policy: Tertinggi (384kHz 32-bit Upsample)"
    fi

    # Pastikan file vendor XML dan libalsautils termount sempurna
    for target in /vendor/etc/audio_policy_configuration.xml /vendor/lib64/libalsautils.so /vendor/lib/libalsautils.so; do
        source="${MODDIR}/system${target}"
        if [ -r "$source" ] && [ -r "$target" ]; then
            if ! cmp "$source" "$target" >/dev/null 2>&1; then
                umount "$target" >/dev/null 2>&1
                umount "$target" >/dev/null 2>&1
                mount -o bind "$source" "$target"
                log "Mounted: $source -> $target"
                force_reload=1
            fi
        fi
    done

    if [ "$force_reload" = "1" ]; then
        log "Restarting audioserver to apply 384kHz 32-bit audio policy..."
        killall audioserver android.hardware.audio.service 2>/dev/null
        sleep 2
    fi
}
remount_audio_files

# ------------------------------------------------------------------------------
# 4. Audio Resampler & Jitter Silencer Settings (Bit-Perfect & Underrun Immune)
# ------------------------------------------------------------------------------
apply_audio_resampler_and_jitter() {
    log "Applying Audio Resampler and Anti-Jitter Tuning..."

    # Delete extreme 520-tap SINC properties that cause 100% CPU starvation and underruns
    resetprop --delete ro.audio.resampler.psd.enable_at_samplerate 2>/dev/null
    resetprop --delete ro.audio.resampler.psd.stopband 2>/dev/null
    resetprop --delete ro.audio.resampler.psd.halflength 2>/dev/null
    resetprop --delete ro.audio.resampler.psd.cutoff_percent 2>/dev/null
    resetprop --delete ro.audio.resampler.psd.tbwcheat 2>/dev/null
    resetprop --delete af.resampler.quality 2>/dev/null

    # Remove unstable low-latency period overrides on Bengal (causes buffer underruns)
    resetprop --delete vendor.audio.usb.perio 2>/dev/null
    resetprop --delete vendor.audio.usb.out.period_us 2>/dev/null
    resetprop --delete vendor.audio.usb.out.period_count 2>/dev/null
    resetprop --delete vendor.audio_hal.period_size 2>/dev/null
    resetprop --delete vendor.audio_hal.period_multiplier 2>/dev/null
    resetprop --delete vendor.audio.adm.buffering.ms 2>/dev/null

    # Disable Android adaptive throttling & doze jitter
    settings put global adaptive_battery_management_enabled 0 2>/dev/null
    settings put secure adaptive_charging_enabled 0 2>/dev/null
    settings put secure adaptive_connectivity_enabled 0 2>/dev/null
    settings put global wifi_suspend_optimizations_enabled 0 2>/dev/null

    # Run I/O jitter reduction
    if [ -f "$CONF_DIR/jitter-reducer-functions.shlib" ]; then
        . "$CONF_DIR/jitter-reducer-functions.shlib"
        reduceIoJitter 1 '*' 'boost' 0 >/dev/null 2>&1
        log "reduceIoJitter executed."
    fi

    # Doze Whitelist
    if [ -f "$CONF_DIR/audio-deviceidle.xml" ] && [ ! -f "$CONF_DIR/run_once_doze" ]; then
        dumpsys deviceidle disable all 1>/dev/null 2>&1
        target_wl="/data/system/deviceidle.xml"
        if [ -f "$target_wl" ]; then
            cp -f "$target_wl" "${target_wl}.bak_jacktor"
            { grep '<wl' "$target_wl"; grep '<wl' "$CONF_DIR/audio-deviceidle.xml"; } | sort | uniq > /data/local/tmp/merged_idle.xml
            echo "<?xml version='1.0' encoding='utf-8' standalone='yes' ?>" > "$target_wl"
            echo "<config>" >> "$target_wl"
            cat /data/local/tmp/merged_idle.xml >> "$target_wl"
            echo "</config>" >> "$target_wl"
            rm -f /data/local/tmp/merged_idle.xml
            chmod 600 "$target_wl"
            chown system:system "$target_wl"
            dumpsys deviceidle enable all 1>/dev/null 2>&1
            touch "$CONF_DIR/run_once_doze"
            log "Doze whitelist merged."
        fi
    fi
}
apply_audio_resampler_and_jitter

# ------------------------------------------------------------------------------
# 5. Battery Bypass & Infinite Power Mode (24/7 Amplifier Operation)
# ------------------------------------------------------------------------------
apply_battery_bypass() {
    log "Activating 24/7 Battery Bypass Mode..."
    dumpsys battery set ac 1 2>/dev/null
    dumpsys battery set status 2 2>/dev/null
    dumpsys battery set level 100 2>/dev/null
    settings put global low_power 0 2>/dev/null
    settings put global low_power_trigger_level 0 2>/dev/null
    settings put global automatic_power_save_mode 0 2>/dev/null
    settings put secure icon_blacklist battery 2>/dev/null
}
apply_battery_bypass

# ------------------------------------------------------------------------------
# 5b. Headunit Display, Resolution & Screen Always-On Configuration
# ------------------------------------------------------------------------------
apply_display_and_stayon() {
    log "Applying landscape display, resolution & screen always-on settings..."
    settings put system accelerometer_rotation 0 2>/dev/null
    settings put system user_rotation 1 2>/dev/null
    wm size 720x1460 2>/dev/null
    wm density 256 2>/dev/null
    svc power stayon true 2>/dev/null
    settings put system screen_off_timeout 2147483647 2>/dev/null
    settings put system font_scale 0.90 2>/dev/null
    locksettings set-disabled true 2>/dev/null
    settings put secure lockscreen.disabled 1 2>/dev/null
}
apply_display_and_stayon

# ------------------------------------------------------------------------------
# 6. CPU Performance Tuning & Thread Affinity
# ------------------------------------------------------------------------------
tune_cpu_and_scheduler() {
    log "Configuring CPU clusters, USB interrupts & power for jitter-free audio..."

    CPU_CONF="$CONF_DIR/cpu_profile"
    TARGET_PROF="performance"
    [ -f "$CPU_CONF" ] && TARGET_PROF="$(cat "$CPU_CONF" 2>/dev/null | tr -d ' \r\n')"
    [ -z "$TARGET_PROF" ] && TARGET_PROF="performance" && echo "performance" > "$CPU_CONF" 2>/dev/null

    if [ "$TARGET_PROF" = "balanced" ] || [ "$TARGET_PROF" = "seimbang" ]; then
        for p in /sys/devices/system/cpu/cpufreq/policy*; do
            echo schedutil > "$p/scaling_governor" 2>/dev/null
        done
        log "CPU Profile applied: Balanced (schedutil)"
    else
        for p in /sys/devices/system/cpu/cpufreq/policy*; do
            echo performance > "$p/scaling_governor" 2>/dev/null
        done
        log "CPU Profile applied: Maximum Performance (performance - zero jitter)"
    fi

    # Pin audioserver, audio HAL & player to Big Cores (f0 = CPU 4-7)
    for pid in $(pgrep -E "audioserver|audio.service|uaudio|symfonik"); do
        renice -n -20 -p "$pid" 2>/dev/null
        taskset -a -p f0 "$pid" 2>/dev/null
    done

    # Disable USB autosuspend on all USB devices & hubs to eliminate audio dropout/stutter
    echo -1 > /sys/module/usbcore/parameters/autosuspend 2>/dev/null
    for d in /sys/bus/usb/devices/*; do
        if [ -f "$d/power/control" ]; then
            echo on > "$d/power/control" 2>/dev/null
            echo -1 > "$d/power/autosuspend_delay_ms" 2>/dev/null
        fi
    done

    # Set USB DWC3 & xHCI interrupt affinity to Big Cores (f0)
    for irq in $(grep -iE "dwc3|xhci" /proc/interrupts 2>/dev/null | awk -F: '{print $1}' | tr -d ' '); do
        echo f0 > "/proc/irq/$irq/smp_affinity" 2>/dev/null
    done
}
tune_cpu_and_scheduler

# ------------------------------------------------------------------------------
# 6b. High-Performance SWAP & Virtual Memory Engine
# ------------------------------------------------------------------------------
apply_swap_configuration() {
    log "Configuring and activating SWAP memory..."
    local SWAP_CONF="$CONF_DIR/swap_size_mb"
    local SWAP_SIZE="3072" # 3GB Recommended for Bengal 4GB RAM
    [ -f "$SWAP_CONF" ] && SWAP_SIZE="$(cat "$SWAP_CONF" 2>/dev/null | tr -d ' \r\n')"
    [ -z "$SWAP_SIZE" ] && SWAP_SIZE="3072"

    # Virtual Memory tuning for audio stability & zero order-0 page allocation failures
    echo 32768 > /proc/sys/vm/min_free_kbytes 2>/dev/null
    echo 24576 > /proc/sys/vm/extra_free_kbytes 2>/dev/null
    echo 60 > /proc/sys/vm/swappiness 2>/dev/null
    echo 0 > /proc/sys/vm/page-cluster 2>/dev/null
    echo 100 > /proc/sys/vm/vfs_cache_pressure 2>/dev/null
    echo 15 > /proc/sys/vm/dirty_ratio 2>/dev/null
    echo 5 > /proc/sys/vm/dirty_background_ratio 2>/dev/null

    local SWAPFILE="/data/swapfile"
    if [ "$SWAP_SIZE" -gt 0 ] 2>/dev/null; then
        local NEED_CREATE=0
        if [ -f "$SWAPFILE" ]; then
            local CURR_BYTES="$(stat -c %s "$SWAPFILE" 2>/dev/null)"
            local TARGET_BYTES=$((SWAP_SIZE * 1024 * 1024))
            local MIN_BYTES=$((TARGET_BYTES - 10485760))
            local MAX_BYTES=$((TARGET_BYTES + 10485760))
            if [ -z "$CURR_BYTES" ] || [ "$CURR_BYTES" -lt "$MIN_BYTES" ] || [ "$CURR_BYTES" -gt "$MAX_BYTES" ]; then
                NEED_CREATE=1
            fi
        else
            NEED_CREATE=1
        fi

        if [ "$NEED_CREATE" -eq 1 ]; then
            log "Allocating SWAP file (${SWAP_SIZE} MB) at $SWAPFILE..."
            swapoff "$SWAPFILE" 2>/dev/null
            rm -f "$SWAPFILE" 2>/dev/null
            dd if=/dev/zero of="$SWAPFILE" bs=1M count="$SWAP_SIZE" 2>/dev/null
            chmod 600 "$SWAPFILE"
            mkswap "$SWAPFILE" 2>/dev/null
        fi

        if ! grep -q "$SWAPFILE" /proc/swaps 2>/dev/null; then
            swapon -p 10 "$SWAPFILE" 2>/dev/null
            log "SWAP file active: ${SWAP_SIZE} MB (Priority 10, paired with zRAM)"
        fi
    else
        if grep -q "$SWAPFILE" /proc/swaps 2>/dev/null; then
            swapoff "$SWAPFILE" 2>/dev/null
            log "SWAP file deactivated by config"
        fi
    fi
}
apply_swap_configuration

# ------------------------------------------------------------------------------
# 6c. Qualcomm Bengal Hardware Audio Gain Control
# ------------------------------------------------------------------------------
apply_audio_gain_configuration() {
    log "Applying hardware Audio Gain parameters..."
    local GAIN_CONF="$CONF_DIR/audio_gain.conf"
    local DIGITAL_GAIN=84
    local ANALOG_GAIN=20
    local EAR_PA_GAIN="G_6_DB"

    if [ -f "$GAIN_CONF" ]; then
        . "$GAIN_CONF"
    else
        cat << 'EOF' > "$GAIN_CONF"
DIGITAL_GAIN=84
ANALOG_GAIN=20
EAR_PA_GAIN=G_6_DB
EOF
    fi

    [ -z "$DIGITAL_GAIN" ] && DIGITAL_GAIN=84
    [ -z "$ANALOG_GAIN" ] && ANALOG_GAIN=20
    [ -z "$EAR_PA_GAIN" ] && EAR_PA_GAIN="G_6_DB"

    tinymix "RX_RX0 Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "RX_RX1 Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "RX_RX2 Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "RX_RX0 Mix Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "RX_RX1 Mix Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "RX_RX2 Mix Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "HPHL Volume" "$ANALOG_GAIN" 2>/dev/null
    tinymix "HPHR Volume" "$ANALOG_GAIN" 2>/dev/null
    tinymix "EAR PA GAIN" "$EAR_PA_GAIN" 2>/dev/null

    # Apply to all External USB DACs (Cards 1..9)
    for c in /proc/asound/card[1-9]*; do
        [ -d "$c" ] || continue
        local cnum="$(basename "$c" | tr -cd '0-9')"
        [ -z "$cnum" ] && continue

        tinymix -D "$cnum" 2>/dev/null | grep -iE 'playback.*volume|master.*volume|headphone.*volume|pcm.*volume' | while read -r line; do
            local ctl_id="$(echo "$line" | awk '{print $1}')"
            [ -z "$ctl_id" ] && continue
            local INFO="$(tinymix -D "$cnum" "$ctl_id" 2>/dev/null)"
            local MAX="$(echo "$INFO" | grep -oE 'dsrange [0-9]+->[0-9]+' | awk -F'->' '{print $2}')"
            local MIN="$(echo "$INFO" | grep -oE 'dsrange [0-9]+->[0-9]+' | awk -F'->' '{print $1}' | awk '{print $2}')"
            if [ -n "$MAX" ] && [ "$MAX" -gt 0 ]; then
                [ -z "$MIN" ] && MIN=0
                local SPAN=$(( MAX - MIN ))
                local PCT=75
                if [ "$DIGITAL_GAIN" -le 84 ]; then
                    PCT=$(( 40 + (DIGITAL_GAIN - 64) * 35 / 20 ))
                else
                    PCT=$(( 75 + (DIGITAL_GAIN - 84) * 25 / 20 ))
                    [ "$PCT" -gt 100 ] && PCT=100
                fi
                local USB_VAL=$(( MIN + (PCT * SPAN / 100) ))
                [ "$USB_VAL" -gt "$MAX" ] && USB_VAL="$MAX"
                [ "$USB_VAL" -lt "$MIN" ] && USB_VAL="$MIN"
                tinymix -D "$cnum" "$ctl_id" "$USB_VAL" 2>/dev/null
            fi
        done
    done
    log "Audio Gain applied to Internal and External DAC: Digital=$DIGITAL_GAIN, Analog=$ANALOG_GAIN, EarPA=$EAR_PA_GAIN"
}
apply_audio_gain_configuration

# ------------------------------------------------------------------------------
# 7. USB Mode Engine (Host / Device / Auto - User Configured & Persistent)
# ------------------------------------------------------------------------------
apply_saved_usb_mode() {
    while [ "$(getprop sys.boot_completed)" != "1" ]; do
        sleep 2
    done

    STATE_FILE="$CONF_DIR/usb_mode_state"
    TARGET_MODE="auto"
    if [ -f "$STATE_FILE" ]; then
        TARGET_MODE="$(cat "$STATE_FILE" 2>/dev/null | tr -d ' \r\n')"
    elif [ -n "$(getprop persist.jacktor.usb_mode)" ]; then
        TARGET_MODE="$(getprop persist.jacktor.usb_mode)"
    fi
    [ -z "$TARGET_MODE" ] && TARGET_MODE="auto"

    log "Applying saved persistent USB mode: $TARGET_MODE..."
    "$MODDIR/system/bin/otg-trigger" "$TARGET_MODE" >/dev/null 2>&1
}
apply_saved_usb_mode

# ------------------------------------------------------------------------------
# 7b. Audio & USB Performance Watchdog Daemon (Anti-Jitter & Zero Dropouts)
# ------------------------------------------------------------------------------
(
    unset LD_PRELOAD
    LAST_APIDS=""
    LAST_MODE=""
    
    # 1. Enforce permanent anti-suspend across all USB audio endpoints
    echo -1 > /sys/module/usbcore/parameters/autosuspend 2>/dev/null
    for d in /sys/bus/usb/devices/*; do
        [ -f "$d/power/control" ] && echo on > "$d/power/control" 2>/dev/null
        [ -f "$d/power/autosuspend_delay_ms" ] && echo -1 > "$d/power/autosuspend_delay_ms" 2>/dev/null
    done

    # 2. Prevent CPU frequency throttling during Hi-Res 32-bit streaming
    echo 1017600 > /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq 2>/dev/null
    echo 1056000 > /sys/devices/system/cpu/cpufreq/policy4/scaling_min_freq 2>/dev/null

    while true; do
        sleep 5
        unset LD_PRELOAD

        # Keep USB autosuspend disabled
        for d in /sys/bus/usb/devices/*; do
            if [ -f "$d/power/control" ] && [ "$(cat "$d/power/control" 2>/dev/null)" != "on" ]; then
                echo on > "$d/power/control" 2>/dev/null
                echo -1 > "$d/power/autosuspend_delay_ms" 2>/dev/null
            fi
        done

        # Maintain USB Mode without disruptive bus resets
        STATE_FILE="$CONF_DIR/usb_mode_state"
        TARGET_MODE="auto"
        [ -f "$STATE_FILE" ] && TARGET_MODE="$(cat "$STATE_FILE" 2>/dev/null | tr -d ' \r\n')"
        [ -z "$TARGET_MODE" ] && TARGET_MODE="auto"
        MODE_NOW="$(cat /sys/devices/platform/soc/4e00000.ssusb/mode 2>/dev/null)"

        if [ "$TARGET_MODE" = "host" ]; then
            if [ "$MODE_NOW" != "host" ]; then
                "$MODDIR/system/bin/otg-trigger" host >/dev/null 2>&1
            fi
        elif [ "$TARGET_MODE" = "device" ] || [ "$TARGET_MODE" = "gadget" ]; then
            if [ "$MODE_NOW" != "peripheral" ]; then
                "$MODDIR/system/bin/otg-trigger" device >/dev/null 2>&1
            fi
        elif [ "$TARGET_MODE" = "auto" ]; then
            if [ "$(cat /sys/class/power_supply/usb/otg_switch 2>/dev/null)" != "1" ]; then
                echo 1 > /sys/class/power_supply/usb/otg_switch 2>/dev/null
                echo 1 > /sys/class/oplus_chg/usb/otg_switch 2>/dev/null
            fi
            EXTCON_NOW="$(cat /sys/class/extcon/extcon1/state 2>/dev/null)"
            if echo "$EXTCON_NOW" | grep -q "USB-HOST=1"; then
                if [ "$MODE_NOW" != "host" ]; then
                    echo "" > /config/usb_gadget/g1/UDC 2>/dev/null
                    echo host > /sys/devices/platform/soc/4e00000.ssusb/mode 2>/dev/null
                fi
            else
                if [ "$MODE_NOW" != "peripheral" ]; then
                    echo peripheral > /sys/devices/platform/soc/4e00000.ssusb/mode 2>/dev/null
                    CONF="$(getprop persist.sys.usb.config)"
                    [ -z "$CONF" ] && CONF="adb"
                    setprop sys.usb.config "$CONF"
                fi
            fi
        fi

        # Maintain CPU Governor Profile according to user configuration
        CPU_CONF="$CONF_DIR/cpu_profile"
        W_PROF="performance"
        [ -f "$CPU_CONF" ] && W_PROF="$(cat "$CPU_CONF" 2>/dev/null | tr -d ' \r\n')"
        [ -z "$W_PROF" ] && W_PROF="performance"
        if [ "$W_PROF" = "balanced" ] || [ "$W_PROF" = "seimbang" ]; then
            TARGET_GOV="schedutil"
        else
            TARGET_GOV="performance"
        fi
        for p in /sys/devices/system/cpu/cpufreq/policy*; do
            if [ -f "$p/scaling_governor" ] && [ "$(cat "$p/scaling_governor" 2>/dev/null)" != "$TARGET_GOV" ]; then
                echo "$TARGET_GOV" > "$p/scaling_governor" 2>/dev/null
            fi
        done

        # Maintain Audio Server Priority & Affinity (Only apply on PID change, zero jitter!)
        CURR_APIDS="$(pgrep -E "audioserver|audio.service" 2>/dev/null | tr '\n' ' ')"
        if [ "$CURR_APIDS" != "$LAST_APIDS" ] && [ -n "$CURR_APIDS" ]; then
            for p in $CURR_APIDS; do
                taskset -p f0 "$p" 2>/dev/null
                renice -n -19 -p "$p" 2>/dev/null
            done
            LAST_APIDS="$CURR_APIDS"
        fi

        # Remount Audio Policy & Libs if needed
        for target in /vendor/etc/audio_policy_configuration.xml /vendor/lib64/libalsautils.so; do
            source="${MODDIR}/system${target}"
            if [ -r "$source" ] && [ -r "$target" ]; then
                if ! cmp "$source" "$target" >/dev/null 2>&1; then
                    umount "$target" >/dev/null 2>&1
                    mount -o bind "$source" "$target"
                fi
            fi
        done

        # Maintain WebUI HTTP Server
        if ! pgrep -f "busybox httpd -p 80 " >/dev/null 2>&1; then
            start_webui_server
        fi

        # Maintain Wi-Fi ADB
        if [ "$(getprop service.adb.tcp.port)" != "5555" ]; then
            setprop service.adb.tcp.port 5555
        fi

        # Maintain SWAP File if configured
        SWAP_CONF="$CONF_DIR/swap_size_mb"
        if [ -f "$SWAP_CONF" ]; then
            SW_SZ="$(cat "$SWAP_CONF" 2>/dev/null | tr -d ' \r\n')"
            if [ -n "$SW_SZ" ] && [ "$SW_SZ" -gt 0 ] 2>/dev/null; then
                if [ -f /data/swapfile ] && ! grep -q "/data/swapfile" /proc/swaps 2>/dev/null; then
                    swapon -p 10 /data/swapfile 2>/dev/null
                fi
            fi
        fi
    done
) >/dev/null 2>&1 &

# ------------------------------------------------------------------------------
# 8. WebUI HTTP Server (Port 80)
# ------------------------------------------------------------------------------
start_webui_server() {
    if [ -d "$MODDIR/webroot" ]; then
        for p in $(pidof busybox 2>/dev/null); do
            grep -q "httpd" "/proc/$p/cmdline" 2>/dev/null && kill "$p" 2>/dev/null
        done
        sleep 1
        log "Starting WebUI server on port 80..."
        "$MODDIR/system/bin/busybox" httpd -p 80 -h "$MODDIR/webroot" -c "$CONF_DIR/httpd.conf" 2>/dev/null
    fi
}
start_webui_server

log "=== Jacktor Audio Framework Service Initialized Successfully ==="
