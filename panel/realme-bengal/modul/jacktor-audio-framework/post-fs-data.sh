#!/system/bin/sh
# Jacktor Audio Framework - Post-FS-Data Boot Guard
# Prevents early-boot OTG trigger race condition & PMI632 bootloop
# Author: Ardo Zapp

# 1. Hold OTG switch OFF during early boot to prevent UDC binding crash
[ -e /sys/class/power_supply/usb/otg_switch ] && echo 0 > /sys/class/power_supply/usb/otg_switch 2>/dev/null
[ -e /sys/class/oplus_chg/usb/otg_switch ] && echo 0 > /sys/class/oplus_chg/usb/otg_switch 2>/dev/null

# 2. Reset persist.sys.phh.oppo.usbotg to 0 for the boot phase
if [ -x /data/adb/ap/bin/resetprop ]; then
    /data/adb/ap/bin/resetprop -n persist.sys.phh.oppo.usbotg 0 2>/dev/null
    /data/adb/ap/bin/resetprop -p persist.sys.phh.oppo.usbotg 0 2>/dev/null
elif [ -x /data/adb/ksu/bin/resetprop ]; then
    /data/adb/ksu/bin/resetprop -n persist.sys.phh.oppo.usbotg 0 2>/dev/null
    /data/adb/ksu/bin/resetprop -p persist.sys.phh.oppo.usbotg 0 2>/dev/null
fi
setprop persist.sys.phh.oppo.usbotg 0 2>/dev/null

# 3. Ensure valid default USB config (never none)
CURR_USB="$(getprop persist.sys.usb.config)"
if [ -z "$CURR_USB" ] || [ "$CURR_USB" = "none" ]; then
    setprop persist.sys.usb.config adb 2>/dev/null
fi

# 4. Create Oplus kernel BSP log directory to prevent kworker bright/blackscreen file errors
mkdir -p /data/oplus/log/bsp 2>/dev/null
touch /data/oplus/log/bsp/brightscreen_count.txt /data/oplus/log/bsp/blackscreen_count.txt 2>/dev/null
chmod 666 /data/oplus/log/bsp/brightscreen_count.txt /data/oplus/log/bsp/blackscreen_count.txt 2>/dev/null

# 5. Apply early SELinux rules before HALs and Zygote start to prevent avc: denied & audit rate-limit
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
fi
