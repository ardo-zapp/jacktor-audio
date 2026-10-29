#!/system/bin/sh
##########################################################################################
# Jacktor Audio Framework & DSP Panel - Module Customization / Installer Script
# Supported Managers: KernelSU, APatch, Magisk
# Architecture: Qualcomm Bengal (SM4250/SM6115) & Generic Android GSI
##########################################################################################

SKIPUNZIP=0

ui_print "********************************************************"
ui_print "       JACKTOR AUDIO FRAMEWORK & DSP PANEL              "
ui_print "      Hi-Fi Bit-Perfect Engine & WebUI Manager          "
ui_print "********************************************************"

# 1. Device and Environment Check
ui_print "- Checking environment..."
DEVICE_MODEL=$(getprop ro.product.model 2>/dev/null)
DEVICE_BOARD=$(getprop ro.board.platform 2>/dev/null)
ui_print "- Device: ${DEVICE_MODEL:-Bengal} (${DEVICE_BOARD:-SM4250})"

# 2. Permissions Setup
ui_print "- Setting up permissions..."
mkdir -p "$MODPATH/config"
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm_recursive "$MODPATH/config" 0 0 0755 0644
set_perm_recursive "$MODPATH/system/bin" 0 0 0755 0755
set_perm_recursive "$MODPATH/system/usr/libexec" 0 0 0755 0755
set_perm_recursive "$MODPATH/webroot" 0 0 0755 0644
set_perm_recursive "$MODPATH/webroot/cgi-bin" 0 0 0755 0755
set_perm_recursive "$MODPATH/system/vendor/etc" 0 0 0755 0644
set_perm_recursive "$MODPATH/system/vendor/lib" 0 0 0755 0644
set_perm_recursive "$MODPATH/system/vendor/lib64" 0 0 0755 0644

# Executable permissions on critical scripts
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/post-fs-data.sh" 0 0 0755
chmod 755 "$MODPATH/system/bin/"* 2>/dev/null
chmod 755 "$MODPATH/webroot/cgi-bin/"* 2>/dev/null

# 3. BusyBox NDK Setup (ARM64) & Dynamic Applet Symlinks
# Architecture: ARM64-v8a (Qualcomm Bengal)
# Reference: osm0sis @ xda-developers (busybox-ndk)
ui_print "- Configuring BusyBox NDK (ARM64)..."
mkdir -p "$MODPATH/system/bin"
chmod 755 "$MODPATH/system/bin/busybox"

# Generate applet symlinks dynamically against ROM /system/bin
ui_print "- Generating BusyBox symlinks tailored to environment..."
sysbin="$(ls /system/bin 2>/dev/null)"
cd "$MODPATH/system/bin" || exit 1
for applet in $(./busybox --list 2>/dev/null); do
    if [ ! "$(echo "$sysbin" | ./busybox grep -xF "$applet")" ]; then
        ./busybox ln -sf busybox "$applet"
    fi
done
ui_print "- BusyBox applets symlinked successfully."

# 4. Default Configuration Setup (Consolidated in $MODPATH/config)
ui_print "- Initializing default configurations in config/..."

# Recommended SWAP size: 3072 MB (3 GB)
if [ ! -f "$MODPATH/config/swap_size_mb" ]; then
    echo "3072" > "$MODPATH/config/swap_size_mb"
    ui_print "- Default SWAP configured: 3072 MB (3 GB Recommended)"
fi

# Audio Gain initial settings
if [ ! -f "$MODPATH/config/audio_gain.conf" ]; then
    cat << 'EOF' > "$MODPATH/config/audio_gain.conf"
DIGITAL_GAIN=84
ANALOG_GAIN=20
EAR_PA_GAIN=G_6_DB
EOF
    ui_print "- Default Audio Gain: Unity (0 dB) / Max Clean Output"
fi

[ ! -f "$MODPATH/config/cpu_profile" ] && echo "performance" > "$MODPATH/config/cpu_profile"
[ ! -f "$MODPATH/config/resampler_mode" ] && echo "highest" > "$MODPATH/config/resampler_mode"
[ ! -f "$MODPATH/config/usb_mode_state" ] && echo "auto" > "$MODPATH/config/usb_mode_state"
[ ! -f "$MODPATH/config/httpd.conf" ] && printf "/:*\n" > "$MODPATH/config/httpd.conf"
if [ ! -f "$MODPATH/config/auth.conf" ]; then
    cat << 'EOF' > "$MODPATH/config/auth.conf"
username=root
password=admin123
EOF
fi
chmod 600 "$MODPATH/config/auth.conf" 2>/dev/null

# Display Density: 256 DPI
ui_print "- Display Profile: 256 DPI (Compact Landscape Headunit)"

# 5. KernelPatch Module (KPM) Universal USB-Serial Driver (/dev/ttyUSB0)
if [ -d "/data/adb/ap" ] && [ -f "$MODPATH/kpm/usb-serial.kpm" ]; then
    ui_print "- Installing KernelPatch Module: usb-serial.kpm (/dev/ttyUSB0)..."
    rm -rf /data/adb/ap/kpm/kpm-cp210x /data/adb/ap/kpm/cp210x /data/adb/ap/kpm/jacktor-cp210x 2>/dev/null
    mkdir -p /data/adb/ap/kpm/usb-serial
    cp -f "$MODPATH/kpm/usb-serial.kpm" /data/adb/ap/kpm/usb-serial/usb-serial.kpm
    chmod 644 /data/adb/ap/kpm/usb-serial/usb-serial.kpm
    ui_print "- KPM usb-serial installed to /data/adb/ap/kpm/usb-serial/usb-serial.kpm"
fi

# 6. Clean up installer-only files so the active module remains clean
ui_print "- Cleaning up installer-only artifacts from active module..."
rm -rf "$MODPATH/META-INF" "$MODPATH/kpm" "$MODPATH/customize.sh" "$MODPATH/busybox-"* 2>/dev/null

ui_print "********************************************************"
ui_print "  Installation completed successfully!                 "
ui_print "  WebUI will be available at http://localhost or :80    "
ui_print "********************************************************"
