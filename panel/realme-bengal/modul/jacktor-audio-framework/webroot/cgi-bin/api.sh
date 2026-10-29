#!/system/bin/sh
unset LD_PRELOAD
# Ensure module's built-in busybox is used
if [ -x "/data/adb/modules/jacktor-audio-framework/system/bin/busybox" ]; then
    export PATH="/data/adb/modules/jacktor-audio-framework/system/bin:$PATH"
fi

# Check if invoked via CGI
if [ -n "$REQUEST_METHOD" ]; then
    printf "Content-Type: application/json; charset=utf-8\r\n"
    printf "Access-Control-Allow-Origin: *\r\n"
    printf "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    printf "Access-Control-Allow-Headers: Content-Type, X-Jacktor-Token\r\n"
    printf "\r\n"
    [ "$REQUEST_METHOD" = "OPTIONS" ] && exit 0
fi

# URL decode helper
urldecode() {
    [ -z "$1" ] && return
    case "$1" in
        *%*|*+*)
            echo -e "$(echo "$1" | sed 's/+/ /g;s/%\([0-9A-Fa-f]\{2\}\)/\\x\1/g')"
            ;;
        *)
            echo "$1"
            ;;
    esac
}

# Parse query string or CLI args
if [ -n "$QUERY_STRING" ]; then
    case "$QUERY_STRING" in *action=*) ACTION="${QUERY_STRING#*action=}"; ACTION="${ACTION%%&*}" ;; esac
    case "$QUERY_STRING" in *mode=*) MODE="${QUERY_STRING#*mode=}"; MODE="${MODE%%&*}" ;; esac
    case "$QUERY_STRING" in *val=*) VAL="${QUERY_STRING#*val=}"; VAL="${VAL%%&*}" ;; esac
    case "$QUERY_STRING" in *tweak=*) TWEAK="${QUERY_STRING#*tweak=}"; TWEAK="${TWEAK%%&*}" ;; esac
    case "$QUERY_STRING" in *cmd=*) CMD="${QUERY_STRING#*cmd=}"; CMD="${CMD%%&*}" ;; esac
    case "$QUERY_STRING" in *user=*) SSH_USER="${QUERY_STRING#*user=}"; SSH_USER="${SSH_USER%%&*}"; AUTH_USER="$SSH_USER" ;; esac
    case "$QUERY_STRING" in *port=*) SSH_PORT="${QUERY_STRING#*port=}"; SSH_PORT="${SSH_PORT%%&*}" ;; esac
    case "$QUERY_STRING" in *line=*) SSH_LINE="${QUERY_STRING#*line=}"; SSH_LINE="${SSH_LINE%%&*}" ;; esac
    case "$QUERY_STRING" in *key=*) SSH_KEY="${QUERY_STRING#*key=}"; SSH_KEY="${SSH_KEY%%&*}" ;; esac
    case "$QUERY_STRING" in *b64=*) SSH_B64="${QUERY_STRING#*b64=}"; SSH_B64="${SSH_B64%%&*}" ;; esac
    case "$QUERY_STRING" in *pass=*) AUTH_PASS="${QUERY_STRING#*pass=}"; AUTH_PASS="${AUTH_PASS%%&*}" ;; esac
    case "$QUERY_STRING" in *token=*) AUTH_TOKEN="${QUERY_STRING#*token=}"; AUTH_TOKEN="${AUTH_TOKEN%%&*}" ;; esac
    case "$QUERY_STRING" in *new_pass=*) NEW_PASS="${QUERY_STRING#*new_pass=}"; NEW_PASS="${NEW_PASS%%&*}" ;; esac

    [ -z "$SSH_USER" ] && SSH_USER="root"
    case "$ACTION" in *%*|*+*) ACTION="$(urldecode "$ACTION")" ;; esac
    case "$MODE" in *%*|*+*) MODE="$(urldecode "$MODE")" ;; esac
    case "$VAL" in *%*|*+*) VAL="$(urldecode "$VAL")" ;; esac
    case "$TWEAK" in *%*|*+*) TWEAK="$(urldecode "$TWEAK")" ;; esac
    case "$CMD" in *%*|*+*) CMD="$(urldecode "$CMD")" ;; esac
    case "$SSH_KEY" in *%*|*+*) SSH_KEY="$(urldecode "$SSH_KEY")" ;; esac
    case "$AUTH_PASS" in *%*|*+*) AUTH_PASS="$(urldecode "$AUTH_PASS")" ;; esac
    case "$NEW_PASS" in *%*|*+*) NEW_PASS="$(urldecode "$NEW_PASS")" ;; esac
else
    ACTION="$1"
    TWEAK="$2"
    MODE="$2"
    VAL="$2"
    if [ "$ACTION" = "apply_tweak" ]; then
        TWEAK="$2"
        MODE="$3"
        VAL="$3"
        [ -n "$4" ] && VAL="$4"
    elif [ "$ACTION" = "set_gain" ]; then
        MODE="$2"
        VAL="$2"
        [ -n "$3" ] && VAL="$3"
    else
        [ -n "$3" ] && VAL="$3"
    fi
    CMD="$2"
fi

if [ "$REQUEST_METHOD" = "POST" ] && [ -n "$CONTENT_LENGTH" ] && [ "$CONTENT_LENGTH" -gt 0 ]; then
    POST_DATA="$(head -c "$CONTENT_LENGTH" 2>/dev/null)"
    if [ -z "$ACTION" ]; then
        ACTION="$(echo "$POST_DATA" | grep -oE '(^|[&?])action=[^&]+' | cut -d= -f2)"
        ACTION="$(urldecode "$ACTION")"
    fi
    P_KEY="$(echo "$POST_DATA" | grep -oE '(^|[&?])key=[^&]+' | cut -d= -f2)"
    [ -n "$P_KEY" ] && SSH_KEY="$(urldecode "$P_KEY")"
    P_B64="$(echo "$POST_DATA" | grep -oE '(^|[&?])b64=[^&]+' | cut -d= -f2)"
    [ -n "$P_B64" ] && SSH_B64="$(urldecode "$P_B64")"
    P_PORT="$(echo "$POST_DATA" | grep -oE '(^|[&?])port=[^&]+' | cut -d= -f2)"
    [ -n "$P_PORT" ] && SSH_PORT="$(urldecode "$P_PORT")"
    P_USER="$(echo "$POST_DATA" | grep -oE '(^|[&?])user=[^&]+' | cut -d= -f2)"
    if [ -n "$P_USER" ]; then
        SSH_USER="$(urldecode "$P_USER")"
        AUTH_USER="$(urldecode "$P_USER")"
    fi
    P_VAL="$(echo "$POST_DATA" | grep -oE '(^|[&?])val=[^&]+' | cut -d= -f2)"
    [ -n "$P_VAL" ] && VAL="$(urldecode "$P_VAL")"
    P_MODE="$(echo "$POST_DATA" | grep -oE '(^|[&?])mode=[^&]+' | cut -d= -f2)"
    [ -n "$P_MODE" ] && MODE="$(urldecode "$P_MODE")"
    P_TWK="$(echo "$POST_DATA" | grep -oE '(^|[&?])tweak=[^&]+' | cut -d= -f2)"
    [ -n "$P_TWK" ] && TWEAK="$(urldecode "$P_TWK")"
    P_PASS="$(echo "$POST_DATA" | grep -oE '(^|[&?])pass=[^&]+' | cut -d= -f2)"
    [ -n "$P_PASS" ] && AUTH_PASS="$(urldecode "$P_PASS")"
    P_TOK="$(echo "$POST_DATA" | grep -oE '(^|[&?])token=[^&]+' | cut -d= -f2)"
    [ -n "$P_TOK" ] && AUTH_TOKEN="$(urldecode "$P_TOK")"
    P_NP="$(echo "$POST_DATA" | grep -oE '(^|[&?])new_pass=[^&]+' | cut -d= -f2)"
    [ -n "$P_NP" ] && NEW_PASS="$(urldecode "$P_NP")"
fi

[ -z "$AUTH_TOKEN" ] && [ -n "$HTTP_X_JACKTOR_TOKEN" ] && AUTH_TOKEN="$HTTP_X_JACKTOR_TOKEN"

MODDIR="/data/adb/modules/jacktor-audio-framework"
CONF_DIR="${MODDIR}/config"
mkdir -p "$CONF_DIR" 2>/dev/null

AUTH_CONF="${CONF_DIR}/auth.conf"
SSH_AUTH_CONF="/data/adb/ssh/auth.conf"
TOKEN_FILE="${CONF_DIR}/session_token"

if [ ! -f "$AUTH_CONF" ] && [ -f "$SSH_AUTH_CONF" ]; then
    cp -f "$SSH_AUTH_CONF" "$AUTH_CONF" 2>/dev/null
    chmod 600 "$AUTH_CONF" 2>/dev/null
fi

get_system_password() {
    local pass="admin123"
    if [ -f "$AUTH_CONF" ]; then
        local p="$(grep -E '^[[:space:]]*password=' "$AUTH_CONF" 2>/dev/null | cut -d= -f2- | tr -d '\r\n')"
        [ -n "$p" ] && pass="$p"
    fi
    echo "$pass"
}

get_system_user() {
    local u="root"
    if [ -f "$AUTH_CONF" ]; then
        local found="$(grep -E '^[[:space:]]*username=' "$AUTH_CONF" 2>/dev/null | cut -d= -f2- | tr -d '\r\n')"
        [ -n "$found" ] && u="$found"
    fi
    echo "$u"
}

is_authenticated() {
    [ -z "$AUTH_TOKEN" ] && return 1
    if [ -f "$TOKEN_FILE" ]; then
        local saved="$(cat "$TOKEN_FILE" 2>/dev/null | tr -d '\r\n')"
        [ -n "$saved" ] && [ "$AUTH_TOKEN" = "$saved" ] && return 0
    fi
    local pass="$(get_system_password)"
    local expected="$(printf "%s:jacktor_secret_salt_2026" "$pass" | sha256sum | awk '{print $1}')"
    [ "$AUTH_TOKEN" = "$expected" ] && return 0
    return 1
}

# Require authentication for CGI calls (except login and check)
if [ -n "$REQUEST_METHOD" ]; then
    if [ "$ACTION" != "auth_login" ] && [ "$ACTION" != "auth_check" ]; then
        if ! is_authenticated; then
            echo '{"success":false,"authenticated":false,"error":"Authentication required. Please log in."}'
            exit 0
        fi
    fi
fi

apply_audio_gain() {
    local target_sub="$1"
    local target_val="$2"
    local GAIN_CONF="${CONF_DIR}/audio_gain.conf"
    local DIGITAL_GAIN=84
    local ANALOG_GAIN=20
    local EAR_PA_GAIN="G_6_DB"
    [ -f "$GAIN_CONF" ] && . "$GAIN_CONF"

    case "$target_sub" in
        preset_stock)
            DIGITAL_GAIN=84; ANALOG_GAIN=20; EAR_PA_GAIN="G_6_DB" ;;
        preset_boost)
            DIGITAL_GAIN=90; ANALOG_GAIN=21; EAR_PA_GAIN="G_9_DB" ;;
        preset_loud)
            DIGITAL_GAIN=96; ANALOG_GAIN=22; EAR_PA_GAIN="G_12_DB" ;;
        preset_max)
            DIGITAL_GAIN=104; ANALOG_GAIN=24; EAR_PA_GAIN="G_16_DB" ;;
        digital)
            [ -n "$target_val" ] && DIGITAL_GAIN="$target_val" ;;
        analog)
            [ -n "$target_val" ] && ANALOG_GAIN="$target_val" ;;
        ear_pa)
            [ -n "$target_val" ] && EAR_PA_GAIN="$target_val" ;;
        *)
            if [ -n "$target_val" ] && echo "$target_val" | grep -qE '^[0-9]+$'; then
                DIGITAL_GAIN="$target_val"
            elif [ -n "$target_sub" ] && echo "$target_sub" | grep -qE '^[0-9]+$'; then
                DIGITAL_GAIN="$target_sub"
            fi
            ;;
    esac

    # 1. Apply to Qualcomm Internal Audio Codec (Card 0)
    tinymix "RX_RX0 Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "RX_RX1 Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "RX_RX2 Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "RX_RX0 Mix Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "RX_RX1 Mix Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "RX_RX2 Mix Digital Volume" "$DIGITAL_GAIN" 2>/dev/null
    tinymix "HPHL Volume" "$ANALOG_GAIN" 2>/dev/null
    tinymix "HPHR Volume" "$ANALOG_GAIN" 2>/dev/null
    tinymix "EAR PA GAIN" "$EAR_PA_GAIN" 2>/dev/null

    # 2. Apply to all External USB DACs (Cards 1..9)
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

    # 3. Save Persistent Configuration
    cat << EOF > "$GAIN_CONF"
DIGITAL_GAIN=$DIGITAL_GAIN
ANALOG_GAIN=$ANALOG_GAIN
EAR_PA_GAIN=$EAR_PA_GAIN
EOF
    echo "{\"success\":true,\"message\":\"Hardware Audio Gain applied (Internal & External DAC)\",\"digital\":$DIGITAL_GAIN,\"analog\":$ANALOG_GAIN,\"ear_pa\":\"$EAR_PA_GAIN\"}"
}

[ -z "$ACTION" ] && ACTION="status"

case "$ACTION" in
    auth_login)
        SYS_USER="$(get_system_user)"
        SYS_PASS="$(get_system_password)"
        [ -z "$AUTH_USER" ] && AUTH_USER="root"
        if [ "$AUTH_USER" = "$SYS_USER" ] && [ "$AUTH_PASS" = "$SYS_PASS" ]; then
            TOKEN="$(cat /proc/sys/kernel/random/uuid 2>/dev/null | tr -d '-')"
            [ -z "$TOKEN" ] && TOKEN="$(printf "%s:jacktor_secret_salt_2026" "$SYS_PASS" | sha256sum | awk '{print $1}')"
            echo "$TOKEN" > "$TOKEN_FILE" 2>/dev/null
            chmod 600 "$TOKEN_FILE" 2>/dev/null
            echo "{\"success\":true,\"message\":\"Login successful\",\"token\":\"$TOKEN\",\"user\":\"$SYS_USER\"}"
        else
            echo '{"success":false,"error":"Invalid username or password"}'
        fi
        ;;
    auth_check)
        if is_authenticated; then
            SYS_USER="$(get_system_user)"
            echo "{\"success\":true,\"authenticated\":true,\"user\":\"$SYS_USER\"}"
        else
            echo '{"success":false,"authenticated":false,"error":"Not authenticated"}'
        fi
        ;;
    auth_logout)
        rm -f "$TOKEN_FILE" 2>/dev/null
        echo '{"success":true,"message":"Logged out successfully"}'
        ;;
    auth_change_password)
        SYS_PASS="$(get_system_password)"
        if [ "$AUTH_PASS" = "$SYS_PASS" ]; then
            if [ -n "$NEW_PASS" ] && [ "${#NEW_PASS}" -ge 4 ]; then
                SYS_USER="$(get_system_user)"
                cat << EOF > "$AUTH_CONF"
username=$SYS_USER
password=$NEW_PASS
EOF
                chmod 600 "$AUTH_CONF"
                cp -f "$AUTH_CONF" "$SSH_AUTH_CONF" 2>/dev/null
                chmod 600 "$SSH_AUTH_CONF" 2>/dev/null
                TOKEN="$(cat /proc/sys/kernel/random/uuid 2>/dev/null | tr -d '-')"
                [ -z "$TOKEN" ] && TOKEN="$(printf "%s:jacktor_secret_salt_2026" "$NEW_PASS" | sha256sum | awk '{print $1}')"
                echo "$TOKEN" > "$TOKEN_FILE" 2>/dev/null
                chmod 600 "$TOKEN_FILE" 2>/dev/null
                echo "{\"success\":true,\"message\":\"Password changed successfully for Webroot and SSH\",\"token\":\"$TOKEN\"}"
            else
                echo '{"success":false,"error":"New password must be at least 4 characters long"}'
            fi
        else
            echo '{"success":false,"error":"Current password is incorrect"}'
        fi
        ;;
    otg_mode)
        if [ "$MODE" = "host" ]; then
            /data/adb/modules/jacktor-audio-framework/system/bin/otg-trigger host >/dev/null 2>&1
            echo '{"success":true,"message":"Host Mode aktif & USB re-triggered","usb_target_mode":"host"}'
        elif [ "$MODE" = "device" ] || [ "$MODE" = "peripheral" ] || [ "$MODE" = "gadget" ]; then
            /data/adb/modules/jacktor-audio-framework/system/bin/otg-trigger device >/dev/null 2>&1
            echo '{"success":true,"message":"Device / Gadget Mode aktif","usb_target_mode":"device"}'
        elif [ "$MODE" = "auto" ]; then
            /data/adb/modules/jacktor-audio-framework/system/bin/otg-trigger auto >/dev/null 2>&1
            echo '{"success":true,"message":"Auto Mode aktif (deteksi otomatis)","usb_target_mode":"auto"}'
        else
            /data/adb/modules/jacktor-audio-framework/system/bin/otg-trigger host >/dev/null 2>&1
            echo '{"success":true,"message":"Host Mode aktif","usb_target_mode":"host"}'
        fi
        ;;
    ssh_start)
        /data/adb/modules/jacktor-audio-framework/system/bin/opensshd.init start >/dev/null 2>&1
        echo '{"success":true,"message":"OpenSSH Server started"}'
        ;;
    ssh_stop)
        /data/adb/modules/jacktor-audio-framework/system/bin/opensshd.init stop >/dev/null 2>&1
        echo '{"success":true,"message":"OpenSSH Server stopped"}'
        ;;
    ssh_restart)
        /data/adb/modules/jacktor-audio-framework/system/bin/opensshd.init restart >/dev/null 2>&1
        echo '{"success":true,"message":"OpenSSH Server restarted"}'
        ;;
    ssh_set_autostart)
        if [ "$VAL" = "1" ] || [ "$VAL" = "true" ]; then
            rm -f /data/adb/ssh/no-autostart
            echo '{"success":true,"message":"OpenSSH autostart enabled","autostart":1}'
        else
            touch /data/adb/ssh/no-autostart
            echo '{"success":true,"message":"OpenSSH autostart disabled","autostart":0}'
        fi
        ;;
    ssh_set_port)
        [ -z "$SSH_PORT" ] && SSH_PORT="$VAL"
        if [ -n "$SSH_PORT" ] && echo "$SSH_PORT" | grep -qE '^[0-9]+$'; then
            sed -i -E "s/^[#[:space:]]*Port[[:space:]]+[0-9]+/Port $SSH_PORT/" /data/adb/ssh/sshd_config
            if ! grep -q "^Port $SSH_PORT" /data/adb/ssh/sshd_config; then
                echo "Port $SSH_PORT" >> /data/adb/ssh/sshd_config
            fi
            /data/adb/modules/jacktor-audio-framework/system/bin/opensshd.init restart >/dev/null 2>&1
            echo "{\"success\":true,\"message\":\"SSH port changed to $SSH_PORT and server restarted\",\"port\":$SSH_PORT}"
        else
            echo '{"success":false,"error":"Invalid port number"}'
        fi
        ;;
    ssh_get_keys)
        [ "$SSH_USER" != "shell" ] && SSH_USER="root"
        TARGET_FILE="/data/adb/ssh/${SSH_USER}/.ssh/authorized_keys"
        JSON_KEYS=""
        if [ -f "$TARGET_FILE" ]; then
            LINE_NUM=0
            while IFS= read -r line || [ -n "$line" ]; do
                LINE_NUM=$((LINE_NUM + 1))
                [ -z "$line" ] && continue
                echo "$line" | grep -q '^[[:space:]]*#' && continue
                K_TYPE="$(echo "$line" | awk '{print $1}')"
                K_DATA="$(echo "$line" | awk '{print $2}')"
                K_COMMENT="$(echo "$line" | awk '{$1=""; $2=""; print substr($0, 3)}')"
                [ -z "$K_COMMENT" ] && K_COMMENT="Key #$LINE_NUM"
                FP="${K_DATA:0:16}..."
                ITEM="{\"line\":$LINE_NUM,\"type\":\"$K_TYPE\",\"comment\":\"$K_COMMENT\",\"fingerprint\":\"$FP\"}"
                if [ -z "$JSON_KEYS" ]; then
                    JSON_KEYS="$ITEM"
                else
                    JSON_KEYS="$JSON_KEYS,$ITEM"
                fi
            done < "$TARGET_FILE"
        fi
        echo "{\"success\":true,\"user\":\"$SSH_USER\",\"keys\":[$JSON_KEYS]}"
        ;;
    ssh_add_key)
        [ "$SSH_USER" != "shell" ] && SSH_USER="root"
        TARGET_FILE="/data/adb/ssh/${SSH_USER}/.ssh/authorized_keys"
        mkdir -p "/data/adb/ssh/${SSH_USER}/.ssh" 2>/dev/null
        touch "$TARGET_FILE" 2>/dev/null
        chmod 600 "$TARGET_FILE" 2>/dev/null
        if [ -n "$SSH_B64" ]; then
            NEW_KEY="$(printf '%s' "$SSH_B64" | base64 -d 2>/dev/null | tr -d '\r')"
        else
            NEW_KEY="$(printf '%s' "$SSH_KEY" | tr -d '\r')"
        fi
        if [ -n "$NEW_KEY" ] && echo "$NEW_KEY" | grep -qE '^ssh-|^ecdsa-'; then
            echo "$NEW_KEY" >> "$TARGET_FILE"
            chmod 600 "$TARGET_FILE"
            echo "{\"success\":true,\"message\":\"SSH key added for $SSH_USER\"}"
        else
            echo '{"success":false,"error":"Invalid SSH public key format"}'
        fi
        ;;
    ssh_delete_key)
        [ "$SSH_USER" != "shell" ] && SSH_USER="root"
        TARGET_FILE="/data/adb/ssh/${SSH_USER}/.ssh/authorized_keys"
        if [ -f "$TARGET_FILE" ] && echo "$SSH_LINE" | grep -qE '^[0-9]+$'; then
            sed -i "${SSH_LINE}d" "$TARGET_FILE"
            echo "{\"success\":true,\"message\":\"Key at line $SSH_LINE deleted for $SSH_USER\"}"
        else
            echo '{"success":false,"error":"Invalid line number"}'
        fi
        ;;
    ssh_get_config)
        CONFIG_FILE="/data/adb/ssh/sshd_config"
        if [ -f "$CONFIG_FILE" ]; then
            B64="$(base64 "$CONFIG_FILE" 2>/dev/null | tr -d '\r\n')"
            echo "{\"success\":true,\"config_b64\":\"$B64\"}"
        else
            echo '{"success":false,"error":"sshd_config not found"}'
        fi
        ;;
    ssh_save_config)
        CONFIG_FILE="/data/adb/ssh/sshd_config"
        if [ -n "$SSH_B64" ]; then
            printf '%s' "$SSH_B64" | base64 -d > "$CONFIG_FILE" 2>/dev/null
            chmod 600 "$CONFIG_FILE"
            /data/adb/modules/jacktor-audio-framework/system/bin/opensshd.init restart >/dev/null 2>&1
            echo '{"success":true,"message":"sshd_config saved and server restarted"}'
        else
            echo '{"success":false,"error":"Empty config payload"}'
        fi
        ;;
    ssh_get_log)
        LOG_FILE="/data/adb/ssh/sshd.log"
        if [ -f "$LOG_FILE" ]; then
            B64="$(tail -n 120 "$LOG_FILE" 2>/dev/null | tr -d '\0' | base64 | tr -d '\r\n')"
            echo "{\"success\":true,\"log_b64\":\"$B64\"}"
        else
            echo '{"success":true,"log_b64":""}'
        fi
        ;;
    ssh_clear_log)
        : > /data/adb/ssh/sshd.log 2>/dev/null
        echo '{"success":true,"message":"SSH log cleared"}'
        ;;
    otg_trigger)
        OUT="$(/data/adb/modules/jacktor-audio-framework/system/bin/otg-trigger trigger 2>&1)"
        ESCAPED="$(echo "$OUT" | sed 's/\\/\\\\/g; s/"/\\"/g' | tr '\n\r\t' ' ')"
        echo "{\"success\":true,\"output\":\"$ESCAPED\"}"
        ;;
    restart_audio)
        killall audioserver android.hardware.audio.service 2>/dev/null
        echo '{"success":true,"message":"Audio server restarted"}'
        ;;
    set_volume)
        if [ -n "$VAL" ]; then
            cmd media_session volume --stream 3 --set "$VAL" 2>/dev/null
            echo "{\"success\":true,\"volume\":$VAL}"
        else
            echo '{"success":false,"error":"Missing volume value"}'
        fi
        ;;
    set_swap)
        SWAP_CONF="${CONF_DIR}/swap_size_mb"
        SWAPFILE="/data/swapfile"
        if [ "$VAL" = "0" ] || [ "$VAL" = "off" ] || [ "$VAL" = "disable" ]; then
            swapoff "$SWAPFILE" 2>/dev/null
            echo "0" > "$SWAP_CONF"
            echo '{"success":true,"message":"SWAP deactivated","swap_mb":0}'
        else
            NUM_VAL="$(echo "$VAL" | grep -oE '^[0-9]+$')"
            if [ -n "$NUM_VAL" ] && [ "$NUM_VAL" -ge 256 ] && [ "$NUM_VAL" -le 8192 ]; then
                echo "$NUM_VAL" > "$SWAP_CONF"
                swapoff "$SWAPFILE" 2>/dev/null
                rm -f "$SWAPFILE" 2>/dev/null
                dd if=/dev/zero of="$SWAPFILE" bs=1M count="$NUM_VAL" 2>/dev/null
                chmod 600 "$SWAPFILE"
                mkswap "$SWAPFILE" 2>/dev/null
                swapon -p 10 "$SWAPFILE" 2>/dev/null
                echo "{\"success\":true,\"message\":\"SWAP set to ${NUM_VAL} MB and activated\",\"swap_mb\":$NUM_VAL}"
            else
                echo '{"success":false,"error":"Invalid SWAP size (range: 256 - 8192 MB)"}'
            fi
        fi
        ;;
    set_gain)
        apply_audio_gain "${MODE:-$VAL}" "$VAL"
        ;;
    apply_tweak)
        rm -f "${CONF_DIR}/tweaks.cache" 2>/dev/null
        case "$TWEAK" in
            rotation)
                settings put system accelerometer_rotation 0 2>/dev/null
                settings put system user_rotation "$VAL" 2>/dev/null
                echo "{\"success\":true,\"message\":\"Screen rotation set to $VAL\"}"
                ;;
            resolution)
                if [ "$VAL" = "reset" ]; then
                    wm size reset 2>/dev/null
                    echo '{"success":true,\"message\":\"Resolution reset to default\"}'
                else
                    wm size "$VAL" 2>/dev/null
                    echo "{\"success\":true,\"message\":\"Resolution set to $VAL\"}"
                fi
                ;;
            density)
                if [ "$VAL" = "reset" ]; then
                    wm density reset 2>/dev/null
                    echo '{"success":true,\"message\":\"Density reset to default\"}'
                else
                    wm density "$VAL" 2>/dev/null
                    echo "{\"success\":true,\"message\":\"Density set to $VAL\"}"
                fi
                ;;
            stayon)
                if [ "$VAL" = "true" ]; then
                    svc power stayon true 2>/dev/null
                    settings put system screen_off_timeout 2147483647 2>/dev/null
                    echo '{"success\":true,\"message\":\"Screen set to Always-On 24/7\"}'
                else
                    svc power stayon false 2>/dev/null
                    settings put system screen_off_timeout 30000 2>/dev/null
                    echo '{"success\":true,\"message\":\"Screen timeout restored to standard (30s)\"}'
                fi
                ;;
            lockscreen)
                if [ "$VAL" = "disabled" ]; then
                    locksettings set-disabled true 2>/dev/null
                    settings put secure lockscreen.disabled 1 2>/dev/null
                    echo '{"success\":true,\"message\":\"Lockscreen disabled (Direct Boot)\"}'
                else
                    locksettings set-disabled false 2>/dev/null
                    settings put secure lockscreen.disabled 0 2>/dev/null
                    echo '{"success\":true,\"message\":\"Lockscreen enabled\"}'
                fi
                ;;
            bypass)
                if [ "$VAL" = "true" ]; then
                    dumpsys battery set ac 1 2>/dev/null
                    dumpsys battery set status 2 2>/dev/null
                    dumpsys battery set level 100 2>/dev/null
                    echo '{"success\":true,\"message\":\"24/7 Battery Bypass activated\"}'
                else
                    dumpsys battery reset 2>/dev/null
                    echo '{"success\":true,\"message\":\"Battery mode reset to standard\"}'
                fi
                ;;
            cpu_profile)
                CPU_CONF="${CONF_DIR}/cpu_profile"
                if [ "$VAL" = "balanced" ] || [ "$VAL" = "seimbang" ] || [ "$VAL" = "schedutil" ]; then
                    for p in /sys/devices/system/cpu/cpufreq/policy*; do
                        echo schedutil > "$p/scaling_governor" 2>/dev/null
                    done
                    echo "balanced" > "$CPU_CONF" 2>/dev/null
                    echo '{"success":true,"message":"Profil CPU diatur ke Seimbang (Schedutil)","profile":"balanced","governor":"schedutil"}'
                else
                    for p in /sys/devices/system/cpu/cpufreq/policy*; do
                        echo performance > "$p/scaling_governor" 2>/dev/null
                    done
                    echo "performance" > "$CPU_CONF" 2>/dev/null
                    echo '{"success":true,"message":"Profil CPU diatur ke Performa Maksimal (Performance)","profile":"performance","governor":"performance"}'
                fi
                ;;
            resampler_mode|resampler)
                RES_CONF="${CONF_DIR}/resampler_mode"
                ETC_DIR="/data/adb/modules/jacktor-audio-framework/system/vendor/etc"
                TARGET_POLICY="/vendor/etc/audio_policy_configuration.xml"
                if [ "$VAL" = "match_source" ] || [ "$VAL" = "match" ] || [ "$VAL" = "source" ] || [ "$VAL" = "bitperfect" ]; then
                    echo "match_source" > "$RES_CONF" 2>/dev/null
                    if [ -f "$ETC_DIR/audio_policy_match_source.xml" ]; then
                        cat "$ETC_DIR/audio_policy_match_source.xml" > "$ETC_DIR/audio_policy_configuration.xml" 2>/dev/null
                        cat "$ETC_DIR/audio_policy_match_source.xml" > "$TARGET_POLICY" 2>/dev/null
                    fi
                    (
                        sleep 1
                        killall audioserver android.hardware.audio.service 2>/dev/null
                    ) >/dev/null 2>&1 &
                    echo '{"success":true,"message":"Resampler diset ke Match Source (Bit-Perfect). Audio Policy dimuat ulang.","resampler_mode":"match_source"}'
                else
                    echo "highest" > "$RES_CONF" 2>/dev/null
                    if [ -f "$ETC_DIR/audio_policy_highest.xml" ]; then
                        cat "$ETC_DIR/audio_policy_highest.xml" > "$ETC_DIR/audio_policy_configuration.xml" 2>/dev/null
                        cat "$ETC_DIR/audio_policy_highest.xml" > "$TARGET_POLICY" 2>/dev/null
                    fi
                    (
                        sleep 1
                        killall audioserver android.hardware.audio.service 2>/dev/null
                    ) >/dev/null 2>&1 &
                    echo '{"success":true,"message":"Resampler diset ke Tertinggi (384kHz 32-bit). Audio Policy dimuat ulang.","resampler_mode":"highest"}'
                fi
                ;;
            usb_autosuspend)
                if [ "$VAL" = "disabled" ]; then
                    echo -1 > /sys/module/usbcore/parameters/autosuspend 2>/dev/null
                    for d in /sys/bus/usb/devices/*; do
                        [ -f "$d/power/control" ] && echo on > "$d/power/control" 2>/dev/null
                        [ -f "$d/power/autosuspend_delay_ms" ] && echo -1 > "$d/power/autosuspend_delay_ms" 2>/dev/null
                    done
                    echo '{"success\":true,\"message\":\"USB Autosuspend disabled (No Dropouts)\"}'
                else
                    echo 2 > /sys/module/usbcore/parameters/autosuspend 2>/dev/null
                    for d in /sys/bus/usb/devices/*; do
                        [ -f "$d/power/control" ] && echo auto > "$d/power/control" 2>/dev/null
                    done
                    echo '{"success\":true,\"message\":\"USB Autosuspend restored to standard\"}'
                fi
                ;;
            restart_adb)
                setprop service.adb.tcp.port 5555 2>/dev/null
                stop adbd 2>/dev/null
                start adbd 2>/dev/null
                echo '{"success\":true,\"message\":\"Wi-Fi ADB restarted on port 5555\"}'
                ;;
            swap)
                SWAP_CONF="${CONF_DIR}/swap_size_mb"
                SWAPFILE="/data/swapfile"
                if [ "$VAL" = "0" ] || [ "$VAL" = "off" ] || [ "$VAL" = "disable" ]; then
                    swapoff "$SWAPFILE" 2>/dev/null
                    echo "0" > "$SWAP_CONF"
                    echo '{"success":true,"message":"SWAP deactivated","swap_mb":0}'
                else
                    NUM_VAL="$(echo "$VAL" | grep -oE '^[0-9]+$')"
                    if [ -n "$NUM_VAL" ] && [ "$NUM_VAL" -ge 256 ] && [ "$NUM_VAL" -le 8192 ]; then
                        echo "$NUM_VAL" > "$SWAP_CONF"
                        swapoff "$SWAPFILE" 2>/dev/null
                        rm -f "$SWAPFILE" 2>/dev/null
                        dd if=/dev/zero of="$SWAPFILE" bs=1M count="$NUM_VAL" 2>/dev/null
                        chmod 600 "$SWAPFILE"
                        mkswap "$SWAPFILE" 2>/dev/null
                        swapon -p 10 "$SWAPFILE" 2>/dev/null
                        echo "{\"success\":true,\"message\":\"SWAP set to ${NUM_VAL} MB and activated\",\"swap_mb\":$NUM_VAL}"
                    else
                        echo '{"success":false,"error":"Invalid SWAP size (range: 256 - 8192 MB)"}'
                    fi
                fi
                ;;
            gain|audio_gain)
                apply_audio_gain "${MODE:-$VAL}" "$VAL"
                ;;
            *)
                echo '{"success\":false,\"error\":\"Unknown tweak command\"}'
                ;;
        esac
        ;;
    exec)
        if [ -n "$CMD" ]; then
            OUT="$(sh -c "$CMD" 2>&1)"
            ESCAPED="$(echo "$OUT" | sed 's/\\/\\\\/g; s/"/\\"/g' | tr '\n\r\t' ' ')"
            echo "{\"success\":true,\"output\":\"$ESCAPED\"}"
        else
            echo '{"success":false,"error":"Missing cmd"}'
        fi
        ;;
    status|*)
        OTG_SWITCH=0
        read -r OTG_SWITCH < /sys/class/power_supply/usb/otg_switch 2>/dev/null
        [ -z "$OTG_SWITCH" ] && OTG_SWITCH=0

        OTG_ONLINE=0
        read -r OTG_ONLINE < /sys/class/power_supply/usb/otg_online 2>/dev/null
        [ -z "$OTG_ONLINE" ] && OTG_ONLINE=0

        VOLTAGE=0
        read -r VOLTAGE < /sys/class/power_supply/usb/voltage_now 2>/dev/null
        [ -z "$VOLTAGE" ] && VOLTAGE=0

        EXTCON="$(cat /sys/class/extcon/extcon1/state 2>/dev/null | tr '\n\r' ' ' | xargs)"

        MODE=""
        read -r MODE < /sys/devices/platform/soc/4e00000.ssusb/mode 2>/dev/null

        UDC=""
        [ -f /config/usb_gadget/g1/UDC ] && read -r UDC < /config/usb_gadget/g1/UDC 2>/dev/null

        REAL_CAP=100
        read -r REAL_CAP < /sys/class/power_supply/battery/capacity 2>/dev/null
        [ -z "$REAL_CAP" ] && REAL_CAP=100
        BATT_CAP=100
        BATT_STAT="AC Bypass"

        # Accurate Human-Readable System Uptime from /proc/uptime
        UPTIME_SEC="$(cut -d. -f1 /proc/uptime 2>/dev/null)"
        [ -z "$UPTIME_SEC" ] && UPTIME_SEC=0
        UP_DAYS=$((UPTIME_SEC / 86400))
        UP_HOURS=$(( (UPTIME_SEC % 86400) / 3600 ))
        UP_MINS=$(( (UPTIME_SEC % 3600) / 60 ))
        UP_SECS=$((UPTIME_SEC % 60))

        if [ "$UP_DAYS" -gt 0 ]; then
            UPTIME="${UP_DAYS} days, ${UP_HOURS} hours, ${UP_MINS} mins"
            UPTIME_ID="${UP_DAYS} hari, ${UP_HOURS} jam, ${UP_MINS} menit"
        elif [ "$UP_HOURS" -gt 0 ]; then
            UPTIME="${UP_HOURS} hours, ${UP_MINS} mins"
            UPTIME_ID="${UP_HOURS} jam ${UP_MINS} menit"
        elif [ "$UP_MINS" -gt 0 ]; then
            UPTIME="${UP_MINS} mins ${UP_SECS} secs"
            UPTIME_ID="${UP_MINS} menit ${UP_SECS} detik"
        else
            UPTIME="${UP_SECS} secs"
            UPTIME_ID="${UP_SECS} detik"
        fi

        IP="$(ip addr show wlan0 2>/dev/null | grep 'inet ' | awk '{print $2}' | cut -d/ -f1)"
        [ -z "$IP" ] && IP="$(getprop dhcp.wlan0.ipaddress)"
        [ -z "$IP" ] && IP="192.168.1.11"

        # CPU Telemetry & Dynamic Governor
        CPU0_FREQ=""
        CPU4_FREQ=""
        read -r CPU0_FREQ < /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq 2>/dev/null
        read -r CPU4_FREQ < /sys/devices/system/cpu/cpufreq/policy4/scaling_cur_freq 2>/dev/null
        [ -n "$CPU0_FREQ" ] && CPU0_MHZ="$((CPU0_FREQ / 1000)) MHz" || CPU0_MHZ="1804 MHz"
        [ -n "$CPU4_FREQ" ] && CPU4_MHZ="$((CPU4_FREQ / 1000)) MHz" || CPU4_MHZ="1612 MHz"

        CURR_GOV=""
        read -r CURR_GOV < /sys/devices/system/cpu/cpufreq/policy0/scaling_governor 2>/dev/null
        if [ "$CURR_GOV" = "performance" ]; then
            CPU_GOV_TEXT="performance (Performa Maksimal)"
            CPU_PROFILE="performance"
        else
            CPU_GOV_TEXT="schedutil (Seimbang)"
            CPU_PROFILE="balanced"
        fi

        CPU_TEMP_RAW=""
        read -r CPU_TEMP_RAW < /sys/class/thermal/thermal_zone9/temp 2>/dev/null
        [ -z "$CPU_TEMP_RAW" ] && read -r CPU_TEMP_RAW < /sys/class/thermal/thermal_zone0/temp 2>/dev/null
        if [ -n "$CPU_TEMP_RAW" ] && [ "$CPU_TEMP_RAW" -gt 1000 ]; then
            CPU_TEMP="$((CPU_TEMP_RAW / 1000)) °C"
        else
            CPU_TEMP="48 °C"
        fi

        # Resampler Configuration Mode (Read early for DAC & Pipeline telemetry)
        RES_CONF="${CONF_DIR}/resampler_mode"
        RESAMPLER_MODE="highest"
        [ -f "$RES_CONF" ] && RESAMPLER_MODE="$(cat "$RES_CONF" 2>/dev/null | tr -d ' \r\n')"
        [ -z "$RESAMPLER_MODE" ] && RESAMPLER_MODE="highest"
        if [ "$RESAMPLER_MODE" = "match_source" ]; then
            RESAMPLER_TITLE="Match Source (Bit-Perfect)"
            RESAMPLER_DESC="Mengikuti sample rate asli lagu (1:1 Native)"
        else
            RESAMPLER_TITLE="Tertinggi (384 kHz 32-bit)"
            RESAMPLER_DESC="Upsample ke resolusi hardware tertinggi DAC"
        fi

        # Find active USB DAC (Card with stream0)
        DAC_CARD=""
        DAC_NAME=""
        DAC_STATUS="Standby"
        DAC_FREQ="96000 Hz"
        DAC_FORMAT="S24_3LE"
        DAC_BIT_DEPTH="24-bit PCM"
        DAC_FOUND=0
        DAC_PERIOD="2400"
        DAC_BUFFER="4800"
        DAC_MAX_FREQ="96 kHz"
        DAC_MAX_BITS="24-bit"
        DAC_CHANNELS="Stereo (2 Ch)"
        DAC_RATES_LIST="8k - 96k Hz"
        DAC_HW_CAP="96 kHz • 24-bit • Stereo"

        for c in /proc/asound/card*; do
            if [ -f "$c/stream0" ]; then
                DAC_FOUND=1
                DAC_CARD="${c##*/}"
                read -r DAC_NAME < "$c/id" 2>/dev/null
                [ -z "$DAC_NAME" ] && DAC_NAME="USB-Audio"

                STREAM_OUT="$(cat "$c/stream0" 2>/dev/null)"
                if echo "$STREAM_OUT" | grep -A 4 "Playback:" | grep -q "Status: Running"; then
                    DAC_STATUS="Active (PCM Direct)"
                elif [ -f "$c/pcm0p/sub0/hw_params" ] && ! grep -q "closed" "$c/pcm0p/sub0/hw_params" 2>/dev/null; then
                    DAC_STATUS="Active (DSP Hardware Offload)"
                else
                    DAC_STATUS="Standby"
                fi

                MOM_FREQ="$(echo "$STREAM_OUT" | grep "Momentary freq" | head -n 1 | awk -F'=' '{print $2}' | awk '{print $1}')"
                [ -n "$MOM_FREQ" ] && DAC_FREQ="${MOM_FREQ} Hz"

                FMT="$(echo "$STREAM_OUT" | grep "Format:" | head -n 1 | cut -d: -f2 | tr -d ' ')"
                [ -n "$FMT" ] && DAC_FORMAT="$FMT"

                case "$DAC_FORMAT" in
                    *24*|*S24*) DAC_BIT_DEPTH="24-bit PCM" ;;
                    *32*|*S32*) DAC_BIT_DEPTH="32-bit PCM" ;;
                    *16*|*S16*) DAC_BIT_DEPTH="16-bit PCM" ;;
                    *) DAC_BIT_DEPTH="24-bit PCM" ;;
                esac

                # Extract Hardware Maximum Capabilities from ALSA stream0
                RATES_LINE="$(echo "$STREAM_OUT" | grep "Rates:" | head -n 1 | cut -d: -f2 | tr -d ' ')"
                if [ -n "$RATES_LINE" ]; then
                    MAX_R="$(echo "$RATES_LINE" | tr ',' '\n' | sort -nr | head -n 1)"
                    MIN_R="$(echo "$RATES_LINE" | tr ',' '\n' | sort -n | head -n 1)"
                    if [ -n "$MAX_R" ] && [ "$MAX_R" -gt 0 ] 2>/dev/null; then
                        DAC_MAX_FREQ="$((MAX_R / 1000)) kHz"
                    fi
                    if [ -n "$MIN_R" ] && [ -n "$MAX_R" ]; then
                        DAC_RATES_LIST="$((MIN_R / 1000))k - $((MAX_R / 1000))k Hz"
                    fi
                fi

                if echo "$STREAM_OUT" | grep -qE "S32|32_BIT"; then
                    DAC_MAX_BITS="32-bit"
                elif echo "$STREAM_OUT" | grep -qE "S24|24_BIT"; then
                    DAC_MAX_BITS="24-bit"
                elif echo "$STREAM_OUT" | grep -qE "S16|16_BIT"; then
                    DAC_MAX_BITS="16-bit"
                fi

                CH_NUM="$(echo "$STREAM_OUT" | grep "Channels:" | head -n 1 | awk '{print $2}')"
                if [ "$CH_NUM" = "1" ]; then
                    DAC_CHANNELS="Mono (1 Ch)"
                else
                    DAC_CHANNELS="Stereo (2 Ch)"
                fi

                DAC_HW_CAP="${DAC_MAX_FREQ} • ${DAC_MAX_BITS} • ${DAC_CHANNELS}"

                if [ -f "$c/pcm0p/sub0/hw_params" ]; then
                    HW_P="$(cat "$c/pcm0p/sub0/hw_params" 2>/dev/null)"
                    P_SZ="$(echo "$HW_P" | grep "period_size:" | awk '{print $2}')"
                    B_SZ="$(echo "$HW_P" | grep "buffer_size:" | awk '{print $2}')"
                    [ -n "$P_SZ" ] && DAC_PERIOD="$P_SZ"
                    [ -n "$B_SZ" ] && DAC_BUFFER="$B_SZ"
                fi
                break
            fi
        done

        # Fallback to Internal Audio Hardware (Qualcomm SoC Codec DAC) if no external USB DAC is connected
        if [ "$DAC_FOUND" -eq 0 ] && [ -d /proc/asound/card0 ]; then
            DAC_FOUND=1
            DAC_CARD="card0"
            if [ -d /proc/asound/card0/codecs/wcd937x ]; then
                WCD_VAR="$(cat /proc/asound/card0/codecs/wcd937x/variant 2>/dev/null | tr -d '\r\n')"
                [ -z "$WCD_VAR" ] && WCD_VAR="WCD937x"
                DAC_NAME="Qualcomm $WCD_VAR Aqstic DAC"
            else
                read -r DAC_NAME < /proc/asound/card0/id 2>/dev/null
                [ -z "$DAC_NAME" ] && DAC_NAME="Qualcomm Audio (Internal)"
            fi

            PCM_ACTIVE=0
            for p in /proc/asound/card0/pcm*p/sub*/hw_params; do
                if [ -f "$p" ] && ! grep -q "closed" "$p" 2>/dev/null; then
                    PCM_ACTIVE=1
                    break
                fi
            done

            if [ "$PCM_ACTIVE" -eq 1 ]; then
                DAC_STATUS="Active (PCM Direct)"
            else
                DAC_STATUS="Standby"
            fi

            DAC_FREQ="384000 Hz"
            DAC_FORMAT="S32_LE"
            DAC_BIT_DEPTH="32-bit PCM"
            DAC_PERIOD="240"
            DAC_BUFFER="1920"
            DAC_MAX_FREQ="384 kHz"
            DAC_MAX_BITS="32-bit"
            DAC_CHANNELS="Stereo (2 Ch)"
            DAC_RATES_LIST="44.1k - 384k Hz"
            DAC_HW_CAP="384 kHz • 32-bit • Stereo"
        fi

        # Active Playing Track Telemetry (Accurate Bit-Depth, Sample Rate & Metadata)
        TRACK_TITLE=""
        TRACK_ARTIST=""
        TRACK_ALBUM=""
        TRACK_STATE="Standby"
        TRACK_APP="Symfonium"
        TRACK_FMT_TEXT="16-bit PCM"
        TRACK_RATE_TEXT="44.1 kHz"
        TRACK_SRATE_NUM="44100"

        # Query MediaSession ONCE (Non-Blocking & Ultra-Fast)
        SESSION_DUMP="$(dumpsys media_session 2>/dev/null | grep -E "package=|state=PlaybackState|metadata:|description=|pkg=" | head -n 30)"
        if echo "$SESSION_DUMP" | grep -q "state=PLAYING"; then
            TRACK_STATE="Playing"
            [ "$DAC_FOUND" -eq 1 ] && [ "$DAC_STATUS" = "Standby" ] && DAC_STATUS="Active (PCM Direct)"
        elif echo "$SESSION_DUMP" | grep -q "state=PAUSED"; then
            TRACK_STATE="Paused"
        fi

        DESC_LINE="$(echo "$SESSION_DUMP" | grep "description=" | head -n 1 | awk -F'description=' '{print $2}' | tr -d '\"' | tr '\n\r\t' ' ')"
        if [ -n "$DESC_LINE" ]; then
            TRACK_TITLE="$(echo "$DESC_LINE" | cut -d',' -f1 | sed 's/^[ \t]*//')"
            TRACK_ARTIST="$(echo "$DESC_LINE" | cut -d',' -f2 | sed 's/^[ \t]*//')"
            TRACK_ALBUM="$(echo "$DESC_LINE" | cut -d',' -f3 | sed 's/^[ \t]*//')"
        fi
        [ "$TRACK_TITLE" = "null" ] && TRACK_TITLE=""
        [ "$TRACK_ARTIST" = "null" ] && TRACK_ARTIST=""
        [ "$TRACK_ALBUM" = "null" ] && TRACK_ALBUM=""

        if [ "$TRACK_STATE" = "Standby" ] || [ -z "$TRACK_TITLE" ]; then
            TRACK_TITLE=""
            TRACK_ARTIST=""
            TRACK_ALBUM=""
            TRACK_STATE="Standby"
        fi

        # Detect active player package from the cached SESSION_DUMP
        APP_PKG="$(echo "$SESSION_DUMP" | grep -oE "pkg=[a-zA-Z0-9._]+" | head -n 1 | cut -d= -f2)"
        [ -z "$APP_PKG" ] && APP_PKG="$(echo "$SESSION_DUMP" | grep -oE "package=[a-zA-Z0-9._]+" | head -n 1 | cut -d= -f2)"
        case "$APP_PKG" in
            *symfonik*) TRACK_APP="Symfonium" ;;
            *spotify*) TRACK_APP="Spotify" ;;
            *youtube*) TRACK_APP="YouTube" ;;
            *apple*music*) TRACK_APP="Apple Music" ;;
            *poweramp*) TRACK_APP="Poweramp" ;;
            *hiby*) TRACK_APP="HiBy Music" ;;
            *uapp*|*usbAudioPlayerPRO*) TRACK_APP="UAPP" ;;
            *) [ -n "$APP_PKG" ] && TRACK_APP="$APP_PKG" || TRACK_APP="Symfonium" ;;
        esac

        # Query Media Player Database (Symfonium SQLite) for true source file format & bit-depth
        TRACK_BIT_DEPTH=""
        TRACK_BITRATE=""
        TRACK_COVER=""
        SYM_QUEUE_DB="/data/data/app.symfonik.music.player/databases/queuedatabase.db"
        SYM_CACHE_DB="/data/data/app.symfonik.music.player/databases/media_info_cache_database.db"
        SYM_DB="/data/data/app.symfonik.music.player/databases/database.db"
        SYM_MARKERS="/data/data/app.symfonik.music.player/cache/images/source-markers"
        SYM_IMG_CACHE="/sdcard/Android/data/app.symfonik.music.player/cache/images"
        WEBROOT="/data/adb/modules/jacktor-audio-framework/webroot"

        if [ -f "$SYM_CACHE_DB" ]; then
            MEDIA_JSON=""
            FILE_URI=""
            if [ -f "$SYM_QUEUE_DB" ]; then
                FILE_URI="$(sqlite3 "$SYM_QUEUE_DB" "SELECT coalesce(nullif(external_id, ''), file) FROM media_queues q JOIN media_queues_entries e ON q._id = e.media_queue_id AND q.queue_position = e.position ORDER BY q.last_modified DESC LIMIT 1;" 2>/dev/null)"
                if [ -n "$FILE_URI" ]; then
                    SAFE_URI="$(echo "$FILE_URI" | sed 's/"/""/g')"
                    MEDIA_JSON="$(sqlite3 "$SYM_CACHE_DB" "SELECT media_format FROM media_info_cache_entry WHERE media_file = \"$SAFE_URI\" LIMIT 1;" 2>/dev/null)"
                fi
            fi

            if [ -z "$MEDIA_JSON" ] && [ -n "$TRACK_TITLE" ] && [ "$TRACK_TITLE" != "Audio Playback" ]; then
                CLEAN_TITLE="$(echo "$TRACK_TITLE" | sed 's/[^a-zA-Z0-9 ]//g' | awk '{print $1, $2}')"
                if [ -n "$CLEAN_TITLE" ]; then
                    MEDIA_JSON="$(sqlite3 "$SYM_CACHE_DB" "SELECT media_format FROM media_info_cache_entry WHERE media_file LIKE '%$CLEAN_TITLE%' ORDER BY last_modified DESC LIMIT 1;" 2>/dev/null)"
                fi
            fi

            if [ -n "$MEDIA_JSON" ]; then
                SRC_BPS="$(echo "$MEDIA_JSON" | sed -n 's/.*"bitPerSample":\([0-9]*\).*/\1/p')"
                SRC_SRATE="$(echo "$MEDIA_JSON" | sed -n 's/.*"sampleRate":\([0-9]*\).*/\1/p')"
                SRC_BRATE="$(echo "$MEDIA_JSON" | sed -n 's/.*"bitRate":\([0-9]*\).*/\1/p')"
                SRC_FMT="$(echo "$MEDIA_JSON" | sed -n 's/.*"format":"\([^"]*\)".*/\1/p')"
                [ -z "$SRC_FMT" ] && SRC_FMT="$(echo "$MEDIA_JSON" | sed -n 's/.*"codec":"\([^"]*\)".*/\1/p')"

                SRC_FMT_UP="$(echo "$SRC_FMT" | tr '[:lower:]' '[:upper:]')"

                if [ -n "$SRC_BPS" ] && [ "$SRC_BPS" -gt 0 ]; then
                    TRACK_BIT_DEPTH="${SRC_BPS}-bit"
                    if [ -n "$SRC_FMT_UP" ]; then
                        TRACK_FMT_TEXT="${SRC_BPS}-bit ${SRC_FMT_UP}"
                    else
                        TRACK_FMT_TEXT="${SRC_BPS}-bit Lossless"
                    fi
                elif [ -n "$SRC_FMT_UP" ]; then
                    TRACK_BIT_DEPTH="16-bit"
                    TRACK_FMT_TEXT="$SRC_FMT_UP"
                fi

                if [ -n "$SRC_BRATE" ] && [ "$SRC_BRATE" -gt 1000 ]; then
                    TRACK_BITRATE="$(( (SRC_BRATE + 500) / 1000 )) kbps"
                fi

                if [ -n "$SRC_SRATE" ] && [ "$SRC_SRATE" -gt 0 ]; then
                    TRACK_SRATE_NUM="$SRC_SRATE"
                    case "$TRACK_SRATE_NUM" in
                        44100) TRACK_RATE_TEXT="44.1 kHz" ;;
                        48000) TRACK_RATE_TEXT="48.0 kHz" ;;
                        88200) TRACK_RATE_TEXT="88.2 kHz" ;;
                        96000) TRACK_RATE_TEXT="96.0 kHz" ;;
                        176400) TRACK_RATE_TEXT="176.4 kHz" ;;
                        192000) TRACK_RATE_TEXT="192.0 kHz" ;;
                        352800) TRACK_RATE_TEXT="352.8 kHz" ;;
                        384000) TRACK_RATE_TEXT="384.0 kHz" ;;
                        *)
                            if [ "$TRACK_SRATE_NUM" -gt 1000 ] 2>/dev/null; then
                                TRACK_RATE_TEXT="$((TRACK_SRATE_NUM / 1000)).$(( (TRACK_SRATE_NUM % 1000) / 100 )) kHz"
                            else
                                TRACK_RATE_TEXT="${TRACK_SRATE_NUM} Hz"
                            fi
                            ;;
                    esac
                fi
            fi

            # High-Resolution Album Art Extraction (Cached for Maximum Performance)
            if [ "$TRACK_STATE" = "Playing" ] || [ "$TRACK_STATE" = "Paused" ]; then
                PREV_TRACK="$(cat "$CONF_DIR/current_track" 2>/dev/null)"
                CURRENT_TS="$(cat "$CONF_DIR/current_cover_ts" 2>/dev/null)"
                LOOKUP_KEY="${FILE_URI:-$TRACK_TITLE - $TRACK_ARTIST}"

                if [ -n "$LOOKUP_KEY" ]; then
                    if [ "$LOOKUP_KEY" != "$PREV_TRACK" ] || [ ! -f "$WEBROOT/cover.jpg" ]; then
                        THUMB=""
                        if [ -n "$FILE_URI" ] && [ -f "$SYM_DB" ]; then
                            THUMB="$(sqlite3 "$SYM_DB" "SELECT coalesce(nullif(s.local_thumbnail, ''), nullif(s.thumbnail, ''), nullif(a.local_thumbnail, ''), a.thumbnail) FROM songs s LEFT JOIN albums a ON s.album_id = a._id WHERE s.file = \"$SAFE_URI\" LIMIT 1;" 2>/dev/null)"
                        fi
                        [ -z "$THUMB" ] && THUMB="$FILE_URI"
                        if [ -z "$THUMB" ] || [ "$THUMB" = "$FILE_URI" ]; then
                            if [ -n "$TRACK_TITLE" ] && [ -f "$SYM_DB" ]; then
                                SAFE_TITLE="$(echo "$TRACK_TITLE" | sed 's/"/""/g')"
                                DB_THUMB="$(sqlite3 "$SYM_DB" "SELECT coalesce(nullif(s.local_thumbnail, ''), nullif(s.thumbnail, ''), nullif(a.local_thumbnail, ''), a.thumbnail) FROM songs s LEFT JOIN albums a ON s.album_id = a._id WHERE s.title = \"$SAFE_TITLE\" LIMIT 1;" 2>/dev/null)"
                                [ -n "$DB_THUMB" ] && THUMB="$DB_THUMB"
                            fi
                        fi
                        if [ -z "$THUMB" ] || [ "$THUMB" = "$FILE_URI" ]; then
                            if [ -n "$CLEAN_TITLE" ] && [ -f "$SYM_DB" ]; then
                                DB_THUMB="$(sqlite3 "$SYM_DB" "SELECT coalesce(nullif(s.local_thumbnail, ''), nullif(s.thumbnail, ''), nullif(a.local_thumbnail, ''), a.thumbnail) FROM songs s LEFT JOIN albums a ON s.album_id = a._id WHERE s.title LIKE '%$CLEAN_TITLE%' LIMIT 1;" 2>/dev/null)"
                                [ -n "$DB_THUMB" ] && THUMB="$DB_THUMB"
                            fi
                        fi

                        MARKER=""
                        if [ -n "$THUMB" ] && [ -d "$SYM_MARKERS" ]; then
                            CLEAN_THUMB="${THUMB#tag://}"
                            MARKER="$(grep -rl "$CLEAN_THUMB" "$SYM_MARKERS" 2>/dev/null | head -n 1)"
                            [ -z "$MARKER" ] && MARKER="$(grep -rl "$THUMB" "$SYM_MARKERS" 2>/dev/null | head -n 1)"
                            if [ -z "$MARKER" ]; then
                                REL_NAME="$(echo "$CLEAN_THUMB" | awk -F'/' '{if (NF>=2) print $(NF-1) "/" $NF; else print $NF}')"
                                [ -n "$REL_NAME" ] && MARKER="$(grep -rl "$REL_NAME" "$SYM_MARKERS" 2>/dev/null | head -n 1)"
                            fi
                            if [ -z "$MARKER" ]; then
                                BASE_NAME="$(basename "$THUMB" 2>/dev/null)"
                                [ -n "$BASE_NAME" ] && MARKER="$(grep -rl "$BASE_NAME" "$SYM_MARKERS" 2>/dev/null | head -n 1)"
                            fi
                        fi

                        FOUND_COVER=0
                        if [ -n "$MARKER" ] && [ -f "$MARKER" ]; then
                            TS_MS="$(grep -oE '^[0-9]{13}$' "$MARKER" 2>/dev/null | head -n 1)"
                            if [ -z "$TS_MS" ]; then
                                TS_MS="$(stat -c "%Y %y" "$MARKER" 2>/dev/null | awk '{split($3, t, "."); ms=substr(t[2], 1, 3); while(length(ms)<3) ms=ms "0"; print $1 ms}')"
                            fi

                            if [ -n "$TS_MS" ] && [ -d "$SYM_IMG_CACHE" ]; then
                                TS_SEC="${TS_MS%???}"
                                if [ -n "$TS_SEC" ]; then
                                    TS_PREV="$((TS_SEC - 1))"
                                    TS_NEXT="$((TS_SEC + 1))"

                                    IMG="$(find "$SYM_IMG_CACHE" -name "*.jpg" -exec stat -c "%Y %y %n" {} + 2>/dev/null | grep -E "^($TS_SEC|$TS_PREV|$TS_NEXT) " | awk -v target="$TS_MS" '
                                    {
                                        sec = $1
                                        split($3, t, ".")
                                        ms_part = substr(t[2], 1, 3)
                                        while (length(ms_part) < 3) ms_part = ms_part "0"
                                        file_ms = (sec * 1000) + ms_part
                                        diff = file_ms - target
                                        if (diff < 0) diff = -diff
                                        if (min_diff == "" || diff < min_diff) {
                                            min_diff = diff
                                            best_file = $5
                                        }
                                    }
                                    END {
                                        if (min_diff != "" && min_diff < 1500) print best_file
                                    }')"

                                    if [ -n "$IMG" ] && [ -f "$IMG" ]; then
                                        cp "$IMG" "$WEBROOT/cover.jpg"
                                        chmod 644 "$WEBROOT/cover.jpg"
                                        echo "$LOOKUP_KEY" > "$CONF_DIR/current_track"
                                        echo "$TS_MS" > "$CONF_DIR/current_cover_ts"
                                        TRACK_COVER="/cover.jpg?v=$TS_MS"
                                        FOUND_COVER=1
                                    fi
                                fi
                            fi
                        fi

                        if [ "$FOUND_COVER" -eq 0 ]; then
                            rm -f "$WEBROOT/cover.jpg" "$CONF_DIR/current_cover_ts"
                            echo "$LOOKUP_KEY" > "$CONF_DIR/current_track"
                            TRACK_COVER=""
                        fi
                    elif [ -f "$WEBROOT/cover.jpg" ]; then
                        TRACK_COVER="/cover.jpg?v=${CURRENT_TS:-1}"
                    fi
                fi
            else
                if [ -f "$WEBROOT/cover.jpg" ]; then
                    rm -f "$WEBROOT/cover.jpg" "$CONF_DIR/current_cover_ts" "$CONF_DIR/current_track"
                fi
                TRACK_COVER=""
            fi
        fi

        # Fallback if not found in database
        if [ -z "$TRACK_BIT_DEPTH" ]; then
            if [ "$TRACK_FMT_TEXT" = "32-bit Float PCM" ]; then
                TRACK_FMT_TEXT="PCM Stream (AudioTrack)"
                TRACK_BIT_DEPTH="16/24-bit Stream"
            else
                TRACK_BIT_DEPTH="$TRACK_FMT_TEXT"
            fi
        fi
        [ -z "$TRACK_BITRATE" ] && TRACK_BITRATE="Direct Stream"

        # Dynamic DAC Telemetry: When Resampler Mode is Match Source, DAC output strictly mirrors the source track
        if [ "$RESAMPLER_MODE" = "match_source" ]; then
            if [ -n "$TRACK_SRATE_NUM" ] && [ "$TRACK_SRATE_NUM" -gt 0 ] 2>/dev/null; then
                DAC_FREQ="${TRACK_SRATE_NUM} Hz"
            else
                DAC_FREQ="44100 Hz"
            fi

            case "$TRACK_BIT_DEPTH" in
                *32*|*Float*)
                    DAC_BIT_DEPTH="32-bit PCM"
                    DAC_FORMAT="S32_LE"
                    ;;
                *24*)
                    DAC_BIT_DEPTH="24-bit PCM"
                    DAC_FORMAT="S24_3LE"
                    ;;
                *16*)
                    DAC_BIT_DEPTH="16-bit PCM"
                    DAC_FORMAT="S16_LE"
                    ;;
                *)
                    DAC_BIT_DEPTH="16-bit PCM"
                    DAC_FORMAT="S16_LE"
                    ;;
            esac
        fi

        # JSON escape strings
        JSON_TRACK_TITLE="$(echo "$TRACK_TITLE" | sed 's/\\/\\\\/g; s/"/\\"/g')"
        JSON_TRACK_ARTIST="$(echo "$TRACK_ARTIST" | sed 's/\\/\\\\/g; s/"/\\"/g')"
        JSON_TRACK_ALBUM="$(echo "$TRACK_ALBUM" | sed 's/\\/\\\\/g; s/"/\\"/g')"
        JSON_TRACK_FMT="$(echo "$TRACK_FMT_TEXT" | sed 's/\\/\\\\/g; s/"/\\"/g')"
        JSON_TRACK_BIT="$(echo "$TRACK_BIT_DEPTH" | sed 's/\\/\\\\/g; s/"/\\"/g')"
        JSON_TRACK_BRATE="$(echo "$TRACK_BITRATE" | sed 's/\\/\\\\/g; s/"/\\"/g')"

        # External USB Devices List with Precise Hardware Classification (Fast Builtin Read)
        USB_JSON=""
        EXT_COUNT=0
        for d in /sys/bus/usb/devices/*; do
            if [ -f "$d/idVendor" ]; then
                read -r VID < "$d/idVendor" 2>/dev/null
                read -r PID < "$d/idProduct" 2>/dev/null
                if [ "$VID" != "1d6b" ]; then
                    EXT_COUNT=$((EXT_COUNT + 1))
                    MFR=""
                    PROD=""
                    BUS=""
                    DEV=""
                    DEV_CLASS=""
                    [ -f "$d/manufacturer" ] && read -r MFR < "$d/manufacturer" 2>/dev/null
                    [ -f "$d/product" ] && read -r PROD < "$d/product" 2>/dev/null
                    [ -f "$d/busnum" ] && read -r BUS < "$d/busnum" 2>/dev/null
                    [ -f "$d/devnum" ] && read -r DEV < "$d/devnum" 2>/dev/null
                    [ -f "$d/bDeviceClass" ] && read -r DEV_CLASS < "$d/bDeviceClass" 2>/dev/null

                    # Clean whitespace / quotes
                    MFR="$(echo "$MFR" | tr -d '\"' | tr '\n\r\t' ' ')"
                    PROD="$(echo "$PROD" | tr -d '\"' | tr '\n\r\t' ' ')"

                    # Hardware identification
                    IF_CLASSES="$(cat "$d"/*:*/bInterfaceClass 2>/dev/null | tr '\n' ' ')"
                    NAME_LOWER="$(echo "$PROD $MFR" | tr 'A-Z' 'a-z')"

                    DEV_TYPE="usb"
                    if [ "$VID" = "0a12" ] && [ "$PID" = "4010" ]; then
                        PROD="QCC Module USB 2.0 Hub"
                        MFR="Qualcomm / CSR"
                        DEV_TYPE="hub"
                    elif [ "$VID" = "0a12" ] && [ "$PID" = "4007" ]; then
                        PROD="QCC5125 Hi-Res Audio DAC"
                        MFR="QTIL"
                        DEV_TYPE="audio"
                    elif [ "$VID" = "067b" ] && [ "$PID" = "2586" ]; then
                        PROD="Prolific USB 2.0 Hub"
                        DEV_TYPE="hub"
                    elif [ "$VID" = "10c4" ] && [ "$PID" = "ea60" ]; then
                        PROD="Silicon Labs CP2102 UART Bridge"
                        DEV_TYPE="serial"
                    elif [ "$VID" = "0461" ] && [ "$PID" = "4d81" ]; then
                        PROD="Dell Optical Mouse"
                        MFR="Dell"
                        DEV_TYPE="mouse"
                    elif [ "$VID" = "0461" ]; then
                        [ -z "$MFR" ] && MFR="Dell"
                    elif echo "$NAME_LOWER" | grep -qE "mouse|trackball"; then
                        DEV_TYPE="mouse"
                    elif echo "$NAME_LOWER" | grep -qE "keyboard|keypad"; then
                        DEV_TYPE="keyboard"
                    elif [ "$DEV_CLASS" = "0e" ] || [ "$DEV_CLASS" = "06" ] || echo "$IF_CLASSES" | grep -q "0e" || echo "$NAME_LOWER" | grep -qE "camera|webcam|uvc|video"; then
                        DEV_TYPE="camera"
                    elif echo "$NAME_LOWER" | grep -qE "dvd|cdrom|cd-rom|optical|bluray|bd-rom"; then
                        DEV_TYPE="dvd"
                    elif [ "$DEV_CLASS" = "08" ] || echo "$IF_CLASSES" | grep -q "08" || echo "$NAME_LOWER" | grep -qE "storage|flash|drive|disk|cruzer|sandisk|kingston|transcend|sdcard"; then
                        DEV_TYPE="storage"
                    elif echo "$NAME_LOWER" | grep -qE "mtp|phone|android|iphone|ipad|galaxy|xiaomi|redmi|pixel|oppo|vivo|realme|huawei|mobile"; then
                        DEV_TYPE="mtp"
                    elif [ "$DEV_CLASS" = "09" ] || echo "$IF_CLASSES" | grep -q "09" || echo "$NAME_LOWER" | grep -q "hub"; then
                        DEV_TYPE="hub"
                        [ -z "$PROD" ] && PROD="USB Hub"
                    elif echo "$NAME_LOWER" | grep -qE "ethernet|lan|rtl815|ax88|rndis|cdc_ether|network"; then
                        DEV_TYPE="lan"
                    elif echo "$NAME_LOWER" | grep -qE "wifi|wi-fi|wireless|wlan|802.11|rtl818|rtl819|rtl88|mt76|ath9"; then
                        DEV_TYPE="wifi"
                    elif [ "$DEV_CLASS" = "01" ] || echo "$IF_CLASSES" | grep -q "01" || echo "$NAME_LOWER" | grep -qE "dac|audio|sound|headset|headphone|speaker|pcm|codec|mic"; then
                        DEV_TYPE="audio"
                    elif echo "$NAME_LOWER" | grep -qE "gamepad|joystick|controller|xbox|playstation|dualshock|dualsense"; then
                        DEV_TYPE="gamepad"
                    elif echo "$NAME_LOWER" | grep -qE "serial|uart|cp210|ch34|ftdi|pl2303|bridge"; then
                        DEV_TYPE="serial"
                    elif [ "$DEV_CLASS" = "07" ] || echo "$IF_CLASSES" | grep -q "07" || echo "$NAME_LOWER" | grep -q "printer"; then
                        DEV_TYPE="printer"
                    elif echo "$NAME_LOWER" | grep -qE "bluetooth|bt dongle"; then
                        DEV_TYPE="bluetooth"
                    elif echo "$IF_CLASSES" | grep -q "03"; then
                        DEV_TYPE="hid"
                    fi

                    [ -z "$PROD" ] && PROD="Device $VID:$PID"

                    ITEM="{\"bus\":\"$BUS\",\"dev\":\"$DEV\",\"vid\":\"$VID\",\"pid\":\"$PID\",\"mfr\":\"$MFR\",\"prod\":\"$PROD\",\"type\":\"$DEV_TYPE\"}"
                    if [ -z "$USB_JSON" ]; then
                        USB_JSON="$ITEM"
                    else
                        USB_JSON="$USB_JSON,$ITEM"
                    fi
                fi
            fi
        done

        # Audio Volume (Music Stream / Master from Active AudioService Device)
        VOL_DUMP="$(cmd media_session volume --get 2>/dev/null)"
        VOL="$(echo "$VOL_DUMP" | grep -oE 'volume is [0-9]+' | awk '{print $3}')"
        VOL_MAX="$(echo "$VOL_DUMP" | grep -oE '\[[0-9]+\.\.[0-9]+\]' | sed 's/.*\.\.//; s/\]//')"
        [ -z "$VOL_MAX" ] && VOL_MAX=25
        if [ -z "$VOL" ]; then
            if [ -n "$DAC_CARD" ] && [ "$DAC_CARD" != "card0" ]; then
                VOL="$(settings get system volume_music_usb_headset 2>/dev/null)"
            fi
            [ -z "$VOL" ] && VOL="$(settings get system volume_music 2>/dev/null)"
            [ -z "$VOL" ] && VOL="$(settings get system volume_music_speaker 2>/dev/null)"
            [ -z "$VOL" ] && VOL=15
        fi

        # Cable plugged detection: devices present on bus OR hardware extcon reports USB-HOST
        OTG_CABLE_CONNECTED=0
        if [ "$EXT_COUNT" -gt 0 ]; then
            OTG_CABLE_CONNECTED=1
        elif echo "$EXTCON" | grep -q "USB-HOST=1"; then
            OTG_CABLE_CONNECTED=1
        fi

        # Sound cards raw text
        CARDS="$(cat /proc/asound/cards 2>/dev/null | tr '\n\r\t' ' ' | sed 's/\\/\\\\/g; s/"/\\"/g')"

        # USB Target Mode (Persistent)
        STATE_FILE="${CONF_DIR}/usb_mode_state"
        TARGET_MODE="auto"
        if [ -f "$STATE_FILE" ]; then
            TARGET_MODE="$(cat "$STATE_FILE" 2>/dev/null | tr -d ' \r\n')"
        elif [ -n "$(getprop persist.jacktor.usb_mode)" ]; then
            TARGET_MODE="$(getprop persist.jacktor.usb_mode)"
        fi
        [ -z "$TARGET_MODE" ] && TARGET_MODE="auto"

        # Query module tweak states (Cached for High-Speed Telemetry)
        CACHE_FILE="${CONF_DIR}/tweaks.cache"
        if [ ! -f "$CACHE_FILE" ]; then
            C_ROT="$(settings get system user_rotation 2>/dev/null)"
            C_RES="$(wm size 2>/dev/null | grep -oE '[0-9]+x[0-9]+' | tail -n 1)"
            C_DEN="$(wm density 2>/dev/null | grep -oE '[0-9]+' | tail -n 1)"
            C_STAY="$(settings get global stay_on_while_plugged_in 2>/dev/null)"
            C_LOCK="$(settings get secure lockscreen.disabled 2>/dev/null)"
            cat << EOF > "$CACHE_FILE"
TW_ROT=${C_ROT:-1}
TW_RES=${C_RES:-720x1460}
TW_DEN=${C_DEN:-256}
TW_STAY=${C_STAY:-15}
TW_LOCK=${C_LOCK:-1}
EOF
        fi
        . "$CACHE_FILE" 2>/dev/null
        [ -z "$TW_ROT" ] && TW_ROT=1
        [ -z "$TW_RES" ] && TW_RES="720x1460"
        [ -z "$TW_DEN" ] && TW_DEN=256
        if [ "$TW_STAY" = "15" ] || [ "$TW_STAY" = "3" ] || [ "$TW_STAY" = "7" ]; then
            TW_STAY_ON=1
        else
            TW_STAY_ON=0
        fi
        [ "$TW_LOCK" = "1" ] && TW_LOCK_DIS=1 || TW_LOCK_DIS=0
        TW_AUTO_SUSP=-1
        TW_ADB_PORT="5555"

        # Dynamic Kernel & Device Model info
        K_REL="$(uname -r 2>/dev/null)"
        [ -z "$K_REL" ] && K_REL="$(cut -d' ' -f3 /proc/version 2>/dev/null)"
        K_VER="$(uname -v 2>/dev/null)"
        K_TAG="$(echo "$K_VER" | grep -oE '\([^)]+\)' | tail -n 1)"
        if [ -n "$K_TAG" ]; then
            K_DISP="$K_REL $K_TAG"
        else
            K_DISP="$K_REL"
        fi
        DEV_MODEL="$(getprop ro.product.model 2>/dev/null)"
        DEV_PLAT="$(getprop ro.board.platform 2>/dev/null)"
        [ -z "$DEV_MODEL" ] && DEV_MODEL="RMX2195"
        [ -z "$DEV_PLAT" ] && DEV_PLAT="Bengal SM4250"

        # Dynamic OpenSSH Server status
        SSH_RUNNING=0
        SSH_PID=""
        SSH_PORT="22"
        SSH_AUTOSTART=1
        [ -f /data/adb/ssh/no-autostart ] && SSH_AUTOSTART=0
        [ -f /data/adb/ssh/sshd_config ] && SSH_PORT="$(grep -iE '^[[:space:]]*Port[[:space:]]+' /data/adb/ssh/sshd_config 2>/dev/null | awk '{print $2}' | tail -n 1)"
        [ -z "$SSH_PORT" ] && SSH_PORT="22"
        if [ -f /data/adb/ssh/sshd.pid ]; then
            P="$(cat /data/adb/ssh/sshd.pid 2>/dev/null)"
            if [ -n "$P" ] && kill -0 "$P" 2>/dev/null; then
                SSH_RUNNING=1
                SSH_PID="$P"
            fi
        fi
        if [ -z "$SSH_PID" ]; then
            P="$(pidof sshd 2>/dev/null | awk '{print $1}')"
            if [ -n "$P" ]; then
                SSH_RUNNING=1
                SSH_PID="$P"
            fi
        fi
        KEY_COUNT_ROOT=0
        if [ -f /data/adb/ssh/root/.ssh/authorized_keys ]; then
            KEY_COUNT_ROOT="$(grep -c -v '^[[:space:]]*$' /data/adb/ssh/root/.ssh/authorized_keys 2>/dev/null)"
            [ -z "$KEY_COUNT_ROOT" ] && KEY_COUNT_ROOT=0
        fi
        KEY_COUNT_SHELL=0
        if [ -f /data/adb/ssh/shell/.ssh/authorized_keys ]; then
            KEY_COUNT_SHELL="$(grep -c -v '^[[:space:]]*$' /data/adb/ssh/shell/.ssh/authorized_keys 2>/dev/null)"
            [ -z "$KEY_COUNT_SHELL" ] && KEY_COUNT_SHELL=0
        fi

        # SWAP Memory Status
        SWAP_TOTAL_KB="$(grep -i 'SwapTotal' /proc/meminfo 2>/dev/null | awk '{print $2}')"
        [ -z "$SWAP_TOTAL_KB" ] && SWAP_TOTAL_KB=0
        SWAP_FREE_KB="$(grep -i 'SwapFree' /proc/meminfo 2>/dev/null | awk '{print $2}')"
        [ -z "$SWAP_FREE_KB" ] && SWAP_FREE_KB=0
        SWAP_USED_KB=$((SWAP_TOTAL_KB - SWAP_FREE_KB))
        SWAP_TOTAL_MB=$((SWAP_TOTAL_KB / 1024))
        SWAP_USED_MB=$((SWAP_USED_KB / 1024))
        SWAP_FREE_MB=$((SWAP_FREE_KB / 1024))

        SWAP_CONF="${CONF_DIR}/swap_size_mb"
        SWAP_CFG_MB=3072
        [ -f "$SWAP_CONF" ] && SWAP_CFG_MB="$(cat "$SWAP_CONF" 2>/dev/null | tr -d ' \r\n')"
        [ -z "$SWAP_CFG_MB" ] && SWAP_CFG_MB=3072

        SWAP_ACTIVE_FILE=0
        grep -q '/data/swapfile' /proc/swaps 2>/dev/null && SWAP_ACTIVE_FILE=1

        # Audio Gain Mixer Status
        GAIN_CONF="${CONF_DIR}/audio_gain.conf"
        CURR_DIG_GAIN=""
        CURR_ANA_GAIN=""
        CURR_EAR_GAIN=""
        if [ -f "$GAIN_CONF" ]; then
            . "$GAIN_CONF" 2>/dev/null
            CURR_DIG_GAIN="$DIGITAL_GAIN"
            CURR_ANA_GAIN="$ANALOG_GAIN"
            CURR_EAR_GAIN="$EAR_PA_GAIN"
        fi
        if [ -z "$CURR_DIG_GAIN" ]; then
            CURR_DIG_GAIN="$(tinymix "RX_RX0 Digital Volume" 2>/dev/null | awk -F': ' '{print $2}' | awk '{print $1}')"
        fi
        [ -z "$CURR_DIG_GAIN" ] && CURR_DIG_GAIN=84
        if [ -z "$CURR_ANA_GAIN" ]; then
            CURR_ANA_GAIN="$(tinymix "HPHL Volume" 2>/dev/null | awk -F': ' '{print $2}' | awk '{print $1}')"
        fi
        [ -z "$CURR_ANA_GAIN" ] && CURR_ANA_GAIN=20
        if [ -z "$CURR_EAR_GAIN" ]; then
            CURR_EAR_GAIN="$(tinymix "EAR PA GAIN" 2>/dev/null | grep -oE '>[^ ]+' | tr -d '>')"
        fi
        [ -z "$CURR_EAR_GAIN" ] && CURR_EAR_GAIN="G_6_DB"

        cat << JSON
{
  "success": true,
  "otg_switch": $OTG_SWITCH,
  "otg_online": $OTG_ONLINE,
  "otg_cable_connected": $OTG_CABLE_CONNECTED,
  "voltage_now": $VOLTAGE,
  "extcon_state": "$EXTCON",
  "ssusb_mode": "$MODE",
  "usb_target_mode": "$TARGET_MODE",
  "udc": "$UDC",
  "tweaks": {
    "rotation": $TW_ROT,
    "resolution": "$TW_RES",
    "density": $TW_DEN,
    "stayon": $TW_STAY_ON,
    "lockscreen_disabled": $TW_LOCK_DIS,
    "autosuspend": $TW_AUTO_SUSP,
    "adb_port": "$TW_ADB_PORT"
  },
  "battery": {
    "capacity": $BATT_CAP,
    "status": "$BATT_STAT",
    "raw_capacity": "$REAL_CAP"
  },
  "volume": $VOL,
  "volume_max": $VOL_MAX,
  "uptime": "$UPTIME",
  "uptime_id": "$UPTIME_ID",
  "uptime_sec": $UPTIME_SEC,
  "ip": "$IP",
  "device_model": "Realme C15 ($DEV_MODEL • $DEV_PLAT)",
  "kernel": {
    "release": "$K_REL",
    "version": "$K_VER",
    "display": "$K_DISP"
  },
  "cpu": {
    "little_mhz": "$CPU0_MHZ",
    "big_mhz": "$CPU4_MHZ",
    "temp": "$CPU_TEMP",
    "governor": "$CPU_GOV_TEXT",
    "profile": "$CPU_PROFILE",
    "status": "Optimal & Safe"
  },
  "track": {
    "state": "$TRACK_STATE",
    "title": "$JSON_TRACK_TITLE",
    "artist": "$JSON_TRACK_ARTIST",
    "album": "$JSON_TRACK_ALBUM",
    "app": "$TRACK_APP",
    "format": "$JSON_TRACK_FMT",
    "bit_depth": "$JSON_TRACK_BIT",
    "bitrate": "$JSON_TRACK_BRATE",
    "sample_rate": "$TRACK_RATE_TEXT",
    "raw_rate": "$TRACK_SRATE_NUM",
    "cover": "$TRACK_COVER"
  },
  "dac": {
    "found": $DAC_FOUND,
    "card": "$DAC_CARD",
    "name": "$DAC_NAME",
    "status": "$DAC_STATUS",
    "freq": "$DAC_FREQ",
    "format": "$DAC_FORMAT",
    "bit_depth": "$DAC_BIT_DEPTH",
    "period_size": "$DAC_PERIOD",
    "buffer_size": "$DAC_BUFFER",
    "max_freq": "$DAC_MAX_FREQ",
    "max_bit_depth": "$DAC_MAX_BITS",
    "channels": "$DAC_CHANNELS",
    "rates_list": "$DAC_RATES_LIST",
    "hw_capabilities": "$DAC_HW_CAP"
  },
  "usb_ext_count": $EXT_COUNT,
  "usb_devices_list": [$USB_JSON],
  "audio_cards_raw": "$CARDS",
  "ssh": {
    "running": $SSH_RUNNING,
    "pid": "${SSH_PID:-0}",
    "port": $SSH_PORT,
    "autostart": $SSH_AUTOSTART,
    "keys_root": $KEY_COUNT_ROOT,
    "keys_shell": $KEY_COUNT_SHELL,
    "ip": "$IP"
  },
  "swap": {
    "total_mb": $SWAP_TOTAL_MB,
    "used_mb": $SWAP_USED_MB,
    "free_mb": $SWAP_FREE_MB,
    "config_mb": $SWAP_CFG_MB,
    "file_active": $SWAP_ACTIVE_FILE
  },
  "audio_gain": {
    "digital": $CURR_DIG_GAIN,
    "analog": $CURR_ANA_GAIN,
    "ear_pa": "$CURR_EAR_GAIN"
  },
  "resampler": {
    "mode": "$RESAMPLER_MODE",
    "title": "$RESAMPLER_TITLE",
    "description": "$RESAMPLER_DESC"
  }
}
JSON
        ;;
esac
