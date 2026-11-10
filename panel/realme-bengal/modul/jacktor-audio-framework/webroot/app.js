        // --- MODULAR I18N AUTO-DISCOVERY & HARDWARE DYNAMICS ---
        const urlParams = new URLSearchParams(window.location.search);
        let currentLang = (window.jacktorI18n && typeof window.jacktorI18n.getCurrentLang === 'function') 
            ? window.jacktorI18n.getCurrentLang() 
            : (urlParams.get('lang') || localStorage.getItem('jacktor_lang') || 'id');

        window.addEventListener('jacktor-language-changed', (e) => {
            currentLang = e.detail.lang;
            if (window.lastStatusData) {
                renderTelemetry(window.lastStatusData);
            }
            if (window.lastAmpState) {
                updateAmpUi(window.lastAmpState);
            }
        });

        // Transparent proxy for backward-compatible i18n[currentLang][key] accesses
        const i18n = new Proxy({}, {
            get(target, prop) {
                return new Proxy({}, {
                    get(t, key) {
                        if (window.jacktorI18n && typeof window.jacktorI18n.t === 'function') {
                            return window.jacktorI18n.t(key, key);
                        }
                        return key;
                    }
                });
            }
        });

        function toggleLanguage() {
            if (window.jacktorI18n && typeof window.jacktorI18n.toggleSelector === 'function') {
                window.jacktorI18n.toggleSelector(document.getElementById('btn-lang'));
            }
        }

        function applyLanguage(lang) {
            if (window.jacktorI18n && typeof window.jacktorI18n.applyLanguage === 'function') {
                window.jacktorI18n.applyLanguage(lang);
            }
            const cmdIn = document.getElementById('cmd-input');
            if (cmdIn) {
                cmdIn.placeholder = (lang === 'id') 
                    ? 'Ketik perintah (contoh: lsusb, amp-status, tinymix)...'
                    : 'Type command (e.g. lsusb, amp-status, tinymix)...';
            }
            if (window.lastStatusData) {
                renderTelemetry(window.lastStatusData);
            }
        }

        // --- SESSION & AUTHENTICATION MANAGER ---
        function getSessionToken() {
            return sessionStorage.getItem('jacktor_token') || localStorage.getItem('jacktor_token') || '';
        }

        function setSessionToken(token, remember) {
            sessionStorage.setItem('jacktor_token', token);
            if (remember) {
                localStorage.setItem('jacktor_token', token);
            } else {
                localStorage.removeItem('jacktor_token');
            }
        }

        function clearSessionToken() {
            sessionStorage.removeItem('jacktor_token');
            localStorage.removeItem('jacktor_token');
        }

        function togglePasswordVisibility(inputId, btn) {
            const el = document.getElementById(inputId);
            if (!el) return;
            if (el.type === 'password') {
                el.type = 'text';
                if (btn) btn.innerHTML = '<i class="fa-solid fa-eye-slash"></i>';
            } else {
                el.type = 'password';
                if (btn) btn.innerHTML = '<i class="fa-solid fa-eye"></i>';
            }
        }

        // Danger Action Password Confirmation Engine
        let pendingDangerAction = null;

        function promptDangerAction(title, desc, confirmCallback, btnText = 'Konfirmasi & Eksekusi') {
            pendingDangerAction = confirmCallback;
            const titleEl = document.getElementById('danger-confirm-title');
            const descEl = document.getElementById('danger-confirm-desc');
            const btnTextEl = document.getElementById('danger-confirm-btn-text');
            const passInput = document.getElementById('danger-confirm-pass');
            const errBox = document.getElementById('danger-confirm-error');
            const modal = document.getElementById('modal-danger-confirm');

            if (titleEl) titleEl.innerText = title;
            if (descEl) descEl.innerText = desc;
            if (btnTextEl) btnTextEl.innerText = btnText;
            if (passInput) passInput.value = '';
            if (errBox) errBox.style.display = 'none';

            if (modal) {
                modal.classList.add('open');
                setTimeout(() => passInput?.focus(), 120);
            }
        }

        function closeDangerConfirmModal() {
            const modal = document.getElementById('modal-danger-confirm');
            if (modal) modal.classList.remove('open');
            pendingDangerAction = null;
        }

        async function executeDangerAction() {
            const passInput = document.getElementById('danger-confirm-pass');
            const errBox = document.getElementById('danger-confirm-error');
            const submitBtn = document.getElementById('btn-danger-confirm-submit');
            const password = passInput ? passInput.value.trim() : '';

            if (!password) {
                if (errBox) {
                    errBox.innerText = 'Password tidak boleh kosong!';
                    errBox.style.display = 'block';
                }
                passInput?.focus();
                return;
            }

            if (submitBtn) {
                submitBtn.disabled = true;
                submitBtn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> <span>Verifikasi...</span>';
            }

            try {
                const resp = await fetch('/cgi-bin/jacktor-api', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: `action=auth_login&user=root&pass=${encodeURIComponent(password)}`
                });
                const data = await resp.json();

                if (data && (data.authenticated || data.success)) {
                    const actionToRun = pendingDangerAction;
                    closeDangerConfirmModal();
                    if (typeof actionToRun === 'function') {
                        actionToRun();
                    }
                    showToast('Otentikasi berhasil! Perintah dieksekusi.', 'success');
                } else {
                    if (errBox) {
                        errBox.innerText = 'Password salah! Perintah dibatalkan.';
                        errBox.style.display = 'block';
                    }
                    passInput?.select();
                }
            } catch (e) {
                if (errBox) {
                    errBox.innerText = 'Gagal verifikasi password: ' + e.message;
                    errBox.style.display = 'block';
                }
            } finally {
                if (submitBtn) {
                    submitBtn.disabled = false;
                    submitBtn.innerHTML = '<i class="fa-solid fa-check"></i> <span id="danger-confirm-btn-text">Konfirmasi & Eksekusi</span>';
                }
            }
        }

        let pollTimer = null;
        let isPollingActive = false;
        let telemetryEventSource = null;
        let sseReconnectTimer = null;
        let isSseConnected = false;
        let lastSsePacketTime = 0;

        // --- REAL-TIME SERIAL TELEMETRY STREAM (SSE) ---
        function startTelemetryStream() {
            stopTelemetryStream();
            const token = getSessionToken();
            if (!token) return;

            const sseUrl = `/cgi-bin/jacktor-api?action=stream&token=${encodeURIComponent(token)}`;
            try {
                telemetryEventSource = new EventSource(sseUrl);

                telemetryEventSource.onopen = function() {
                    isSseConnected = true;
                    lastSsePacketTime = Date.now();
                    setLiveStatus(true);
                    consecutiveFailures = 0;
                };

                telemetryEventSource.onmessage = function(event) {
                    if (!event.data) return;
                    lastSsePacketTime = Date.now();
                    isSseConnected = true;
                    try {
                        const packet = JSON.parse(event.data);
                        handleStreamPacket(packet);
                    } catch (e) {
                        console.warn("[SSE] Parse error:", e);
                    }
                };

                telemetryEventSource.onerror = function(err) {
                    isSseConnected = false;
                    if (!sseReconnectTimer && isPollingActive) {
                        sseReconnectTimer = setTimeout(() => {
                            sseReconnectTimer = null;
                            if (isPollingActive && (!isSseConnected || (Date.now() - lastSsePacketTime > 4000))) {
                                startTelemetryStream();
                            }
                        }, 2500);
                    }
                };
            } catch (err) {
                console.warn("[SSE] Failed to initialize EventSource:", err);
                isSseConnected = false;
            }
        }

        function stopTelemetryStream() {
            if (telemetryEventSource) {
                try { telemetryEventSource.close(); } catch(e) {}
                telemetryEventSource = null;
            }
            if (sseReconnectTimer) {
                clearTimeout(sseReconnectTimer);
                sseReconnectTimer = null;
            }
            isSseConnected = false;
        }

        function handleStreamPacket(packet) {
            if (!packet) return;
            setLiveStatus(true);
            consecutiveFailures = 0;

            if (packet.changed && packet.changed === 'power') {
                lastAmpState.rt.pwr = !!packet.value;
            }

            if (packet.type === 'temp_scan_result') {
                renderTempScanResult(packet);
                return;
            }

            updateAmpTelemetryUI(packet);
        }

        async function pollLoop() {
            if (!isPollingActive) return;
            await fetchStatus();
            if (isPollingActive) {
                // When SSE stream is healthy and active, poll system stats every 3500ms;
                // If SSE is inactive or disconnected, poll more frequently (every 1200ms) as fallback.
                const interval = (isSseConnected && (Date.now() - lastSsePacketTime < 5000)) ? 3500 : 1200;
                if (isPollingActive && (!isSseConnected || (Date.now() - lastSsePacketTime > 6000))) {
                    startTelemetryStream();
                }
                pollTimer = setTimeout(pollLoop, interval);
            }
        }

        function startPolling() {
            stopPolling();
            isPollingActive = true;
            startTelemetryStream();
            fetchStatus(); // Fetch immediately on 0ms!
            pollTimer = setTimeout(pollLoop, 1000);
            setTimeout(loadSshKeys, 400); // Load SSH keys asynchronously without blocking initial telemetry
        }

        function stopPolling() {
            isPollingActive = false;
            stopTelemetryStream();
            if (pollTimer) {
                clearTimeout(pollTimer);
                pollTimer = null;
            }
        }

        async function manualRefresh() {
            const isId = (currentLang === 'id');
            showToast(isId ? 'Memperbarui data realtime...' : 'Refreshing realtime telemetry...', 'loading');
            startTelemetryStream();
            await fetchStatus();
            showToast(isId ? 'Data berhasil diperbarui' : 'Data updated successfully', 'success', 1500);
        }

        document.addEventListener('visibilitychange', () => {
            if (document.hidden) {
                stopPolling();
            } else {
                startPolling();
            }
        });

        function onAuthRequired() {
            stopPolling();
            clearSessionToken();
            window.location.replace('/login.html');
        }

        async function initAuth() {
            const urlToken = urlParams.get('token');
            if (urlToken) {
                setSessionToken(urlToken, true);
            }
            const token = getSessionToken();
            if (!token) {
                window.location.replace('/login.html');
                return;
            }

            // Restore from session cache INSTANTLY on load/refresh (0ms perceived latency)
            try {
                const cached = sessionStorage.getItem('jacktor_cached_status');
                if (cached) {
                    const parsed = JSON.parse(cached);
                    if (parsed && parsed.success) {
                        window.lastStatusData = parsed;
                        setLiveStatus(true);
                        renderTelemetry(parsed);
                    }
                }
            } catch (e) {}

            // Immediately launch telemetry fetch and live polling with ZERO DELAY
            const tabParam = urlParams.get('tab');
            if (tabParam) switchSshTab(tabParam);
            startPolling();

            // Background async auth token verification
            try {
                const res = await fetch(`/cgi-bin/jacktor-api?action=auth_check&token=${encodeURIComponent(token)}`);
                if (res.ok) {
                    const data = await res.json();
                    if (!data || !data.authenticated) {
                        clearSessionToken();
                        stopPolling();
                        window.location.replace('/login.html');
                    }
                }
            } catch (e) {
                console.warn("auth_check network warning:", e);
            }
        }

        async function handleLogout() {
            try {
                const token = getSessionToken();
                await fetch(`/cgi-bin/jacktor-api?action=auth_logout&token=${encodeURIComponent(token)}`);
            } catch (e) {}
            clearSessionToken();
            stopPolling();
            window.location.replace('/login.html');
        }

        async function submitChangePassword(event) {
            if (event) event.preventDefault();
            const currInput = document.getElementById('input-curr-pwd');
            const newInput = document.getElementById('input-new-pwd');
            const confInput = document.getElementById('input-confirm-pwd');
            const btn = document.getElementById('btn-save-pwd');

            const currPass = currInput ? currInput.value : '';
            const newPass = newInput ? newInput.value : '';
            const confPass = confInput ? confInput.value : '';

            const dict = i18n[currentLang] || i18n.en;

            if (newPass !== confPass) {
                showToast(dict.pwd_err_match);
                return;
            }
            if (newPass.length < 4) {
                showToast(dict.pwd_err_len);
                return;
            }

            if (btn) {
                btn.disabled = true;
                btn.innerHTML = `<i class="fa-solid fa-spinner fa-spin"></i> <span>${dict.pwd_updating}</span>`;
            }

            try {
                const token = getSessionToken();
                const bodyData = `action=auth_change_password&token=${encodeURIComponent(token)}&pass=${encodeURIComponent(currPass)}&new_pass=${encodeURIComponent(newPass)}`;
                let res = null;
                try {
                    const resp = await fetch('/cgi-bin/jacktor-api', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                        body: bodyData
                    });
                    if (resp.ok) res = await resp.json();
                } catch (e1) {
                    const resp = await fetch(`/cgi-bin/jacktor-api?${bodyData}`);
                    if (resp.ok) res = await resp.json();
                }

                if (res && res.success) {
                    if (res.token) {
                        setSessionToken(res.token, true);
                    }
                    showToast(dict.pwd_success);
                    if (currInput) currInput.value = '';
                    if (newInput) newInput.value = '';
                    if (confInput) confInput.value = '';
                } else {
                    showToast(res && res.error ? res.error : "Failed to change password");
                }
            } catch (err) {
                showToast("Error: " + err.message);
            } finally {
                if (btn) {
                    btn.disabled = false;
                    btn.innerHTML = `<i class="fa-solid fa-floppy-disk"></i> <span data-i18n="pwd_btn_save">${dict.pwd_btn_save}</span>`;
                }
            }
        }

        let toastTimer = null;
        function showToast(msg, type = 'info', duration = 3000) {
            const t = document.getElementById('toast');
            if (!t) return;
            if (toastTimer) clearTimeout(toastTimer);

            let icon = '<i class="fa-solid fa-circle-info" style="color: var(--accent-cyan); font-size: 1.1rem;"></i>';
            let borderColor = 'var(--accent-cyan)';

            if (type === 'loading') {
                icon = '<i class="fa-solid fa-spinner fa-spin" style="color: var(--accent-cyan); font-size: 1.1rem;"></i>';
                borderColor = 'var(--accent-cyan)';
            } else if (type === 'success') {
                icon = '<i class="fa-solid fa-circle-check" style="color: var(--accent-green); font-size: 1.1rem;"></i>';
                borderColor = 'var(--accent-green)';
            } else if (type === 'error') {
                icon = '<i class="fa-solid fa-circle-exclamation" style="color: #ef4444; font-size: 1.1rem;"></i>';
                borderColor = '#ef4444';
            } else if (type === 'warning') {
                icon = '<i class="fa-solid fa-triangle-exclamation" style="color: #f59e0b; font-size: 1.1rem;"></i>';
                borderColor = '#f59e0b';
            }

            t.innerHTML = `<div style="display: flex; align-items: center; gap: 10px;">${icon}<span>${msg}</span></div>`;
            t.style.borderColor = borderColor;
            t.style.display = 'block';

            if (duration > 0) {
                toastTimer = setTimeout(() => {
                    t.style.display = 'none';
                }, duration);
            }
        }

        async function getApiData(actionParams, timeoutMs = 4000) {
            const token = getSessionToken();
            const tokenParam = token ? `&token=${encodeURIComponent(token)}` : '';
            const cacheBust = `&_t=${Date.now()}`;

            if (window.ksu && typeof window.ksu.exec === 'function') {
                try {
                    let cmd = `/data/adb/modules/jacktor-audio-framework/system/bin/jacktor-api ${actionParams.replace(/&/g, ' ')} token=${token}`;
                    const res = await window.ksu.exec(cmd);
                    return JSON.parse(res);
                } catch (e) {
                    console.warn("ksu.exec error:", e);
                }
            }

            const controller = typeof AbortController !== 'undefined' ? new AbortController() : null;
            const signal = controller ? controller.signal : undefined;
            const timer = controller ? setTimeout(() => controller.abort(), timeoutMs) : null;

            try {
                const res = await fetch(`/cgi-bin/jacktor-api?action=${actionParams}${tokenParam}${cacheBust}`, {
                    headers: { 'X-Jacktor-Token': token },
                    cache: 'no-store',
                    signal: signal
                });
                if (timer) clearTimeout(timer);
                if (res.ok) {
                    const data = await res.json();
                    if (data && data.authenticated === false) {
                        onAuthRequired();
                        return null;
                    }
                    return data;
                }
            } catch (e) {
                if (timer) clearTimeout(timer);
                console.warn("getApiData warning:", e.message || e);
            }
            return null;
        }

        async function setUsbMode(mode) {
            const isId = (currentLang === 'id');
            showToast(isId ? `Mengalihkan ke Mode ${mode.toUpperCase()}...` : `Switching to ${mode.toUpperCase()} Mode...`, 'loading');
            updateUsbButtons(mode);
            try {
                const res = await getApiData(`otg_mode&mode=${mode}`);
                showToast(res && res.message ? res.message : (isId ? `Mode ${mode.toUpperCase()} Aktif` : `${mode.toUpperCase()} Mode Active`), 'success', 2500);
                await fetchStatus();
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
            }
        }

        function updateUsbButtons(targetMode) {
            const hostBtn = document.getElementById('btn-mode-host');
            const devBtn = document.getElementById('btn-mode-device');
            const autoBtn = document.getElementById('btn-mode-auto');
            if (hostBtn) hostBtn.classList.toggle('active', targetMode === 'host');
            if (devBtn) devBtn.classList.toggle('active', targetMode === 'device' || targetMode === 'peripheral' || targetMode === 'gadget');
            if (autoBtn) autoBtn.classList.toggle('active', targetMode === 'auto');
        }

        async function sendMediaAction(action) {
            try {
                await getApiData(`media_action&val=${encodeURIComponent(action)}`);
                setTimeout(manualRefresh, 200);
            } catch (e) {}
        }

        async function applyNotifSplit(val) {
            const isId = (currentLang === 'id');
            if (window.lastServerData && window.lastServerData.resampler && window.lastServerData.resampler.mode === 'match_source') {
                showToast(isId ? 'Pemisahan notifikasi dikunci ON saat mode Match Source' : 'Notification separation is locked ON during Match Source mode', 'warning', 3000);
                return;
            }
            showToast(isId ? `Mengubah pemisahan notifikasi ke ${val.toUpperCase()}...` : `Setting notification separation to ${val.toUpperCase()}...`, 'loading');
            try {
                await getApiData(`notif_split&val=${encodeURIComponent(val)}`);
                showToast(isId ? `Pemisahan notifikasi diset ke ${val.toUpperCase()}` : `Notification separation set to ${val.toUpperCase()}`, 'success', 2500);
                setTimeout(manualRefresh, 200);
            } catch (e) {
                showToast(isId ? 'Gagal mengubah pemisahan notifikasi' : 'Failed to update notification separation', 'error', 3000);
            }
        }

        async function applyAudioOutput(val) {
            const isId = (currentLang === 'id');
            showToast(isId ? 'Mengubah rute output audio...' : 'Changing audio output routing...', 'loading');
            try {
                await getApiData(`audio_output&val=${encodeURIComponent(val)}`);
                showToast(isId ? 'Output audio berhasil dialihkan' : 'Audio output routed successfully', 'success', 2500);
                setTimeout(manualRefresh, 300);
            } catch (e) {
                showToast(isId ? 'Gagal mengalihkan output audio' : 'Failed to route audio output', 'error', 3000);
            }
        }

                let g_audio_devices = [];
        let g_selected_device_id = "internal_speaker";
        try {
            const savedDev = localStorage.getItem("jacktor_selected_target_device");
            if (savedDev) g_selected_device_id = savedDev;
        } catch (e) {}

        function onConfigDeviceChange(devId) {
            g_selected_device_id = devId;
            try {
                localStorage.setItem("jacktor_selected_target_device", devId);
            } catch (e) {}
            updateDeviceConfigUI();
        }

        function updateDeviceConfigUI() {
            if (!g_audio_devices || g_audio_devices.length === 0) return;
            const dev = g_audio_devices.find(d => d.id === g_selected_device_id) || g_audio_devices[0];
            if (!dev) return;

            const isId = (currentLang === "id");

            // 1. Sinkronisasi target selector dropdown
            const selDev = document.getElementById("select-config-device");
            if (selDev && document.activeElement !== selDev && selDev.value !== dev.id) {
                selDev.value = dev.id;
            }

            // 2. Resampler Mode Buttons
            const isMatchSource = (dev.resampler_mode === "match_source");
            document.getElementById("tw-resampler-match")?.classList.toggle("active", isMatchSource);
            document.getElementById("tw-resampler-highest")?.classList.toggle("active", !isMatchSource);

            // 3. Fixed Quality Dropdown
            const selFixedRate = document.getElementById("select-fixed-rate");
            const ALL_HARDWARE_QUALITIES = [
                { value: "44100_16", label: "44.1 kHz / 16-bit" },
                { value: "44100_24", label: "44.1 kHz / 24-bit" },
                { value: "48000_16", label: "48.0 kHz / 16-bit" },
                { value: "48000_24", label: "48.0 kHz / 24-bit" },
                { value: "88200_24", label: "88.2 kHz / 24-bit" },
                { value: "96000_24", label: "96.0 kHz / 24-bit" },
                { value: "176400_24", label: "176.4 kHz / 24-bit" },
                { value: "192000_24", label: "192.0 kHz / 24-bit" },
                { value: "352800_32", label: "352.8 kHz / 32-bit" },
                { value: "384000_32", label: "384.0 kHz / 32-bit" }
            ];
            const quals = (dev.supported_qualities && dev.supported_qualities.length > 0)
                ? dev.supported_qualities
                : ALL_HARDWARE_QUALITIES;

            if (selFixedRate) {
                if (selFixedRate.dataset.devId !== dev.id || selFixedRate.options.length !== quals.length) {
                    selFixedRate.innerHTML = "";
                    quals.forEach(q => {
                        const opt = document.createElement("option");
                        opt.value = q.value;
                        opt.innerText = q.label;
                        selFixedRate.appendChild(opt);
                    });
                    selFixedRate.dataset.devId = dev.id;
                }
                let activeFixedQual = dev.fixed_quality;
                if (!activeFixedQual) {
                    try {
                        activeFixedQual = localStorage.getItem("jacktor_fixed_quality_" + dev.id);
                    } catch (e) {}
                }
                if (document.activeElement !== selFixedRate) {
                    selFixedRate.value = activeFixedQual || (quals[0] ? quals[0].value : "96000_24");
                }
            }

            // 4. Volume Slider & Label untuk Target Terpilih
            const volSlider = document.getElementById("vol-slider");
            const volLabel = document.getElementById("vol-label");
            const volTitle = document.getElementById("vol-title");

            if (volTitle) {
                const targetShortName = dev.name ? dev.name.split(" (")[0] : dev.id;
                volTitle.innerHTML = `<i class="fa-solid fa-volume-high"></i> Hardware Volume: <strong>${targetShortName}</strong>`;
            }

            const targetVol = (typeof dev.volume === "number") ? dev.volume : 25;
            if (volSlider && !volSlider.matches(":active") && document.activeElement !== volSlider && !isVolumeSending && pendingVolume === null && (Date.now() - lastVolumeInteraction > 1500)) {
                volSlider.max = dev.volume_max || 25;
                volSlider.value = targetVol;
            }
            if (volLabel && (Date.now() - lastVolumeInteraction > 1500)) {
                volLabel.innerText = `${targetVol} / ${dev.volume_max || 25}`;
            }
        }

        async function applyDeviceResamplerMode(mode) {
            const isId = (currentLang === "id");
            const dev = g_audio_devices.find(d => d.id === g_selected_device_id) || { id: g_selected_device_id, name: g_selected_device_id };
            const devName = dev.name || g_selected_device_id;
            const selFixedRate = document.getElementById("select-fixed-rate");
            const qual = selFixedRate ? selFixedRate.value : (dev.fixed_quality || "96000_24");

            // Optimistic update
            dev.resampler_mode = mode;
            updateDeviceConfigUI();

            showToast(isId ? `Mengatur mode resampler untuk ${devName}...` : `Updating resampler for ${devName}...`, "loading");
            try {
                await getApiData(`set_device_audio_config&target_device=${encodeURIComponent(g_selected_device_id)}&resampler_mode=${encodeURIComponent(mode)}&fixed_quality=${encodeURIComponent(qual)}`);
                showToast(isId ? `Mode resampler ${devName} berhasil diperbarui` : `Resampler mode for ${devName} updated`, "success", 2500);
            } catch (e) {
                showToast(isId ? "Gagal mengubah mode resampler" : "Failed to update resampler mode", "error", 3000);
            }
        }

        async function applyDeviceFixedQuality(qual) {
            const isId = (currentLang === "id");
            const dev = g_audio_devices.find(d => d.id === g_selected_device_id) || { id: g_selected_device_id, name: g_selected_device_id };
            const devName = dev.name || g_selected_device_id;

            // Optimistic update
            dev.fixed_quality = qual;
            dev.resampler_mode = "fixed";
            updateDeviceConfigUI();

            showToast(isId ? `Mengatur kualitas fixed untuk ${devName}...` : `Updating fixed quality for ${devName}...`, "loading");
            try {
                await getApiData(`set_device_audio_config&target_device=${encodeURIComponent(g_selected_device_id)}&resampler_mode=fixed&fixed_quality=${encodeURIComponent(qual)}`);
                showToast(isId ? `Kualitas fixed ${devName} diatur ke ${qual}` : `Fixed quality for ${devName} updated`, "success", 2500);
            } catch (e) {
                showToast(isId ? "Gagal mengatur kualitas fixed" : "Failed to set fixed quality", "error", 3000);
            }
        }

        async function controlSerialBridge(cmd) {
            const isId = (currentLang === "id");
            showToast(isId ? `Mengirim perintah ${cmd} ke Serial Bridge...` : `Sending ${cmd} to Serial Bridge...`, "loading");
            try {
                await getApiData(`serial_bridge_control&bridge_cmd=${encodeURIComponent(cmd)}`);
                showToast(isId ? `Serial Bridge: ${cmd} berhasil` : `Serial Bridge: ${cmd} successful`, "success", 2500);
                setTimeout(manualRefresh, 400);
            } catch (e) {
                showToast(isId ? "Gagal mengontrol Serial Bridge" : "Failed to control Serial Bridge", "error", 3000);
            }
        }

        async function setSerialBridgeBaud(baud) {
            const isId = (currentLang === "id");
            showToast(isId ? `Mengatur Baud Rate socat ke ${baud}...` : `Setting socat Baud Rate to ${baud}...`, "loading");
            try {
                await getApiData(`serial_bridge_control&bridge_cmd=set-baud&val=${encodeURIComponent(baud)}&baud=${encodeURIComponent(baud)}`);
                showToast(isId ? `Baud Rate socat diubah ke ${baud}` : `socat Baud Rate updated to ${baud}`, "success", 2500);
                setTimeout(manualRefresh, 500);
            } catch (e) {
                showToast(isId ? "Gagal mengubah Baud Rate" : "Failed to change Baud Rate", "error", 3000);
            }
        }

        async function setSerialBridgePort(port) {
            if (!port) return;
            const isId = (currentLang === "id");
            showToast(isId ? `Mengubah port Serial Node ke ${port}...` : `Switching Serial Node to ${port}...`, "loading");
            try {
                await getApiData(`serial_bridge_control&bridge_cmd=set-port&val=${encodeURIComponent(port)}`);
                showToast(isId ? `Serial Node dialihkan ke ${port}` : `Serial Node switched to ${port}`, "success", 2500);
                setTimeout(manualRefresh, 500);
            } catch (e) {
                showToast(isId ? "Gagal mengubah port Serial Node" : "Failed to change Serial Node", "error", 3000);
            }
        }

async function applyFixedRate(val) {
            const isId = (currentLang === 'id');
            const khz = (parseInt(val) / 1000).toFixed(1);
            showToast(isId ? `Mengatur kualitas fixed ke ${khz} kHz...` : `Setting fixed quality to ${khz} kHz...`, 'loading');
            try {
                await getApiData(`fixed_rate&val=${encodeURIComponent(val)}`);
                showToast(isId ? `Kualitas fixed diatur ke ${khz} kHz` : `Fixed quality set to ${khz} kHz`, 'success', 2500);
                setTimeout(manualRefresh, 300);
            } catch (e) {
                showToast(isId ? 'Gagal mengatur kualitas fixed' : 'Failed to set fixed quality', 'error', 3000);
            }
        }

        async function applyTweak(tweak, val) {
            let label = tweak;
            const isId = (currentLang === 'id');
            if (tweak === 'resampler_mode') {
                label = val === 'match_source' ? (isId ? 'Mode Resampler: Match Source (Bit-Perfect)' : 'Resampler Mode: Match Source (Bit-Perfect)') : (isId ? 'Mode Kualitas: Fixed Quality' : 'Quality Mode: Fixed Quality');
            } else if (tweak === 'cpu_profile') {
                if (val === 'perf') label = isId ? 'Profil CPU: Performa Maksimal' : 'CPU Profile: Max Performance';
                else if (val === 'powersave') label = isId ? 'Profil CPU: Hemat Daya' : 'CPU Profile: Powersave';
                else label = isId ? 'Profil CPU: Seimbang' : 'CPU Profile: Balanced';
            } else if (tweak === 'usb_autosuspend') {
                label = val === 'disabled' ? (isId ? 'USB Autosuspend: Dinonaktifkan (Zero Dropout)' : 'USB Autosuspend: Disabled (Zero Dropout)') : (isId ? 'USB Autosuspend: Standar Auto' : 'USB Autosuspend: Standard Auto');
            } else if (tweak === 'bypass') {
                label = val === 'true' ? (isId ? 'Bypass Baterai: Aktif (AC Direct)' : 'Battery Bypass: Enabled') : (isId ? 'Bypass Baterai: Standar' : 'Battery Bypass: Standard');
            } else if (tweak === 'rotation') {
                label = isId ? `Rotasi Layar: ${val === 1 ? 'Landscape' : (val === 0 ? 'Portrait' : 'Reverse')}` : `Display Rotation: ${val === 1 ? 'Landscape' : (val === 0 ? 'Portrait' : 'Reverse')}`;
            } else if (tweak === 'resolution') {
                label = isId ? `Resolusi Layar: ${val}` : `Display Resolution: ${val}`;
            } else if (tweak === 'density') {
                label = isId ? `Density Layar: ${val} DPI` : `Display Density: ${val} DPI`;
            } else if (tweak === 'stayon') {
                label = isId ? `Layar Tetap Menyala: ${val ? 'Aktif' : 'Nonaktif'}` : `Screen Stay-On: ${val ? 'Enabled' : 'Disabled'}`;
            } else if (tweak === 'lockscreen') {
                label = isId ? `Kunci Layar: ${val ? 'Dinonaktifkan' : 'Diaktifkan'}` : `Lockscreen: ${val ? 'Disabled' : 'Enabled'}`;
            }

            showToast(isId ? `Menerapkan ${label}...` : `Applying ${label}...`, 'loading');

            const fb = document.getElementById('tweak-feedback');
            if (fb) {
                fb.style.display = 'block';
                fb.style.color = 'var(--accent-cyan)';
                fb.innerText = isId ? 'Menerapkan konfigurasi...' : 'Applying setting...';
            }
            try {
                const res = await getApiData(`apply_tweak&tweak=${encodeURIComponent(tweak)}&val=${encodeURIComponent(val)}`);
                const msg = res && res.message ? res.message : (isId ? `${label} berhasil diterapkan` : `${label} applied successfully`);
                showToast(msg, 'success', 2500);
                if (fb) {
                    fb.style.color = '#4ade80';
                    fb.innerText = msg;
                    setTimeout(() => { fb.style.display = 'none'; }, 3000);
                }
                await fetchStatus();
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
                if (fb) {
                    fb.style.color = '#ef4444';
                    fb.innerText = 'Error: ' + err.message;
                }
            }
        }

        function updateTweakButtons(tw, data) {
            if (!tw) return;
            const isId = (currentLang === 'id');
            // Rotation
            const rot = parseInt(tw.rotation);
            document.getElementById('tw-rot-1')?.classList.toggle('active', rot === 1);
            document.getElementById('tw-rot-0')?.classList.toggle('active', rot === 0);
            document.getElementById('tw-rot-3')?.classList.toggle('active', rot === 3);

            // Resolution
            const isCustomRes = tw.resolution === '720x1460';
            document.getElementById('tw-res-custom')?.classList.toggle('active', isCustomRes);
            document.getElementById('tw-res-reset')?.classList.toggle('active', !isCustomRes);

            // Density
            const isCustomDen = tw.density === 256 || tw.density === '256';
            document.getElementById('tw-den-custom')?.classList.toggle('active', isCustomDen);

            // Stayon
            const isStay = tw.stayon === 1 || tw.stayon === true;
            document.getElementById('tw-stay-1')?.classList.toggle('active', isStay);
            document.getElementById('tw-stay-0')?.classList.toggle('active', !isStay);

            // Lockscreen
            const isLockDis = tw.lockscreen_disabled === 1 || tw.lockscreen_disabled === true;
            document.getElementById('tw-lock-1')?.classList.toggle('active', isLockDis);
            document.getElementById('tw-lock-0')?.classList.toggle('active', !isLockDis);

            // Battery Bypass
            const isBypass = (data && data.battery && data.battery.is_bypass !== undefined)
                ? (data.battery.is_bypass === true || data.battery.is_bypass === 1)
                : (data && data.battery && data.battery.status && (data.battery.status.includes('Bypass') || data.battery.status.includes('AC')));
            document.getElementById('tw-bypass-1')?.classList.toggle('active', !!isBypass);
            document.getElementById('tw-bypass-0')?.classList.toggle('active', !isBypass);

            // USB Autosuspend - System returns -1 for disabled, 2 (or positive) for enabled
            const isAutoSuspDis = tw.autosuspend === -1 || tw.autosuspend === '-1';
            document.getElementById('tw-autosuspend-off')?.classList.toggle('active', isAutoSuspDis);
            document.getElementById('tw-autosuspend-on')?.classList.toggle('active', !isAutoSuspDis);
            const badgeSusp = document.getElementById('badge-usb-autosuspend');
            if (badgeSusp) {
                badgeSusp.innerText = isAutoSuspDis ? 'ZERO DROPOUT' : 'STANDARD';
                badgeSusp.className = isAutoSuspDis ? 'badge active-green' : 'badge';
            }

            // CPU Profile
            const gov = ((data && data.cpu && data.cpu.governor) || '').toLowerCase();
            const prof = ((data && data.cpu && data.cpu.profile) || '').toLowerCase();
            const isPerf = prof === 'performance' || prof === 'perf' || gov.includes('performance');
            const isPower = prof === 'powersave' || prof === 'hemat' || gov.includes('powersave');
            const isBal = !isPerf && !isPower;

            document.getElementById('tw-cpu-perf')?.classList.toggle('active', isPerf);
            document.getElementById('tw-cpu-powersave')?.classList.toggle('active', isPower);
            document.getElementById('tw-cpu-balanced')?.classList.toggle('active', isBal);
            const cpuBadge = document.getElementById('badge-cpu-status');
            if (cpuBadge) {
                if (isPerf) {
                    cpuBadge.innerText = (typeof currentLang !== 'undefined' && currentLang === 'en') ? 'Max Performance' : 'Performa Maksimal';
                    cpuBadge.className = 'badge active';
                } else if (isPower) {
                    cpuBadge.innerText = (typeof currentLang !== 'undefined' && currentLang === 'en') ? 'Powersave' : 'Hemat Daya';
                    cpuBadge.className = 'badge';
                } else {
                    cpuBadge.innerText = (typeof currentLang !== 'undefined' && currentLang === 'en') ? 'Balanced' : 'Seimbang';
                    cpuBadge.className = 'badge';
                }
            }

            // USB Controller Mode (Host / Device / Auto)
            const usbMode = ((data && (data.ssusb_mode || data.usb_target_mode)) || '').toLowerCase();
            document.getElementById('btn-mode-host')?.classList.toggle('active', usbMode === 'host');
            document.getElementById('btn-mode-device')?.classList.toggle('active', usbMode === 'device' || usbMode === 'peripheral');
            document.getElementById('btn-mode-auto')?.classList.toggle('active', usbMode === 'auto');

            // Audio Resampler Mode
            const isMatchSource = data && data.resampler && data.resampler.mode === 'match_source';
            document.getElementById('tw-resampler-match')?.classList.toggle('active', !!isMatchSource);
            document.getElementById('tw-resampler-highest')?.classList.toggle('active', !isMatchSource);
            const resEl = document.getElementById('stat-resampler-mode');
            if (resEl) {
                if (isMatchSource) {
                    resEl.innerText = 'Match Source (Bit-Perfect)';
                    resEl.className = 'stat-val val-green';
                } else {
                    const fRate = data.fixed_sample_rate ? (parseInt(data.fixed_sample_rate) / 1000) + ' kHz' : '96.0 kHz';
                    resEl.innerText = `Fixed (${fRate})`;
                    resEl.className = 'stat-val val-cyan';
                }
            }

            // Notification Separation
            const notifMode = (data && data.notification_split) || 'auto';
            const badgeNotif = document.getElementById('badge-notif-split');
            const btnNotifAuto = document.getElementById('tw-notif-auto');
            const btnNotifOn = document.getElementById('tw-notif-on');
            const btnNotifOff = document.getElementById('tw-notif-off');

            if (isMatchSource) {
                if (badgeNotif) {
                    badgeNotif.innerHTML = '<i class="fa-solid fa-lock"></i> ON (LOCKED)';
                    badgeNotif.className = 'badge active-cyan';
                }
                if (btnNotifOn) {
                    btnNotifOn.disabled = true;
                    btnNotifOn.className = 'btn btn-tweak locked-active';
                    btnNotifOn.innerHTML = '<i class="fa-solid fa-lock"></i> On';
                    btnNotifOn.style.opacity = '0.9';
                    btnNotifOn.style.cursor = 'not-allowed';
                }
                if (btnNotifAuto) {
                    btnNotifAuto.disabled = true;
                    btnNotifAuto.className = 'btn btn-tweak';
                    btnNotifAuto.innerHTML = 'Auto';
                    btnNotifAuto.style.opacity = '0.35';
                    btnNotifAuto.style.cursor = 'not-allowed';
                }
                if (btnNotifOff) {
                    btnNotifOff.disabled = true;
                    btnNotifOff.className = 'btn btn-tweak';
                    btnNotifOff.innerHTML = 'Off';
                    btnNotifOff.style.opacity = '0.35';
                    btnNotifOff.style.cursor = 'not-allowed';
                }
            } else {
                if (badgeNotif) {
                    badgeNotif.innerText = notifMode.toUpperCase();
                    badgeNotif.className = notifMode === 'on' ? 'badge active-green' : (notifMode === 'auto' ? 'badge active-cyan' : 'badge');
                }
                ['auto', 'on', 'off'].forEach(m => {
                    const btn = document.getElementById(`tw-notif-${m}`);
                    if (btn) {
                        btn.disabled = false;
                        btn.style.opacity = '1';
                        btn.style.cursor = 'pointer';
                        btn.className = 'btn btn-tweak';
                        btn.innerHTML = m === 'auto' ? 'Auto' : (m === 'on' ? 'On' : 'Off');
                        btn.classList.toggle('active', notifMode === m);
                    }
                });
            }

            // Audio Output Routing Dropdown
            const selAudioOut = document.getElementById('select-audio-output');
            if (selAudioOut && data && data.audio_outputs_list && data.audio_outputs_list.length > 0) {
                const targetVal = data.audio_output || 'auto';
                if (!selAudioOut.dataset.loaded || selAudioOut.dataset.cardsCount != data.audio_outputs_list.length) {
                    selAudioOut.innerHTML = '';
                    data.audio_outputs_list.forEach(out => {
                        const opt = document.createElement('option');
                        opt.value = out.id;
                        opt.innerText = out.name;
                        selAudioOut.appendChild(opt);
                    });
                    selAudioOut.dataset.loaded = 'true';
                    selAudioOut.dataset.cardsCount = data.audio_outputs_list.length;
                    selAudioOut.value = targetVal;
                } else if (document.activeElement !== selAudioOut) {
                    selAudioOut.value = targetVal;
                }
            }

            // Populate Target Config Devices & Per-Device settings
            if (data && data.audio_devices && data.audio_devices.length > 0) {
                g_audio_devices = data.audio_devices;
                const savedTarget = localStorage.getItem("jacktor_selected_target_device");
                if (savedTarget && g_audio_devices.some(d => d.id === savedTarget)) {
                    g_selected_device_id = savedTarget;
                } else if (!g_audio_devices.some(d => d.id === g_selected_device_id)) {
                    g_selected_device_id = g_audio_devices[0].id;
                }

                const selDev = document.getElementById("select-config-device");
                if (selDev) {
                    const devHash = g_audio_devices.map(d => d.id + ":" + d.name).join("|");
                    if (selDev.dataset.devHash !== devHash) {
                        selDev.innerHTML = "";
                        g_audio_devices.forEach(d => {
                            const opt = document.createElement("option");
                            opt.value = d.id;
                            opt.innerText = d.name;
                            selDev.appendChild(opt);
                        });
                        selDev.dataset.devHash = devHash;
                    }
                    if (document.activeElement !== selDev) {
                        selDev.value = g_selected_device_id;
                    }
                }
                updateDeviceConfigUI();
            }

            // Update Serial Bridge Telemetry
            if (data && data.serial_bridge) {
                const sb = data.serial_bridge;
                const badgeSb = document.getElementById("badge-serial-bridge");
                const statStatus = document.getElementById("stat-bridge-status");
                const statHw = document.getElementById("stat-bridge-hw-port");
                const statMode = document.getElementById("stat-bridge-mode");
                const statClients = document.getElementById("stat-bridge-clients");

                const isRunning = (sb.running === 1 || sb.running === true);
                if (badgeSb) {
                    badgeSb.innerText = isRunning ? "RUNNING" : "STOPPED";
                    badgeSb.className = isRunning ? "badge active-green" : "badge";
                }
                if (statStatus) {
                    const pidText = (sb.pid && sb.pid > 0) ? `PID ${sb.pid}` : "PID -";
                    statStatus.innerText = isRunning ? (isId ? `Aktif (${pidText})` : `Active (${pidText})`) : (isId ? "Nonaktif" : "Stopped");
                    statStatus.className = isRunning ? "stat-val val-green" : "stat-val";
                }
                if (statHw && sb.hw_port) statHw.innerText = sb.hw_port;

                // Dynamic Serial Node Selector (populate device names & select active node)
                const selPort = document.getElementById("select-bridge-port");
                if (selPort && Array.isArray(sb.available_ports) && sb.available_ports.length > 0) {
                    const newPortsJson = JSON.stringify(sb.available_ports);
                    if (selPort.dataset.portsCache !== newPortsJson && document.activeElement !== selPort) {
                        selPort.innerHTML = "";
                        sb.available_ports.forEach(p => {
                            const opt = document.createElement("option");
                            opt.value = p.port;
                            opt.textContent = p.name || p.port;
                            if (p.port === sb.hw_port) opt.selected = true;
                            selPort.appendChild(opt);
                        });
                        selPort.dataset.portsCache = newPortsJson;
                    }
                    if (document.activeElement !== selPort && sb.hw_port) {
                        selPort.value = sb.hw_port;
                    }
                } else if (selPort && sb.hw_port && document.activeElement !== selPort) {
                    if (!Array.from(selPort.options).some(o => o.value === sb.hw_port)) {
                        const opt = document.createElement("option");
                        opt.value = sb.hw_port;
                        opt.textContent = sb.hw_port;
                        selPort.appendChild(opt);
                    }
                    selPort.value = sb.hw_port;
                }

                if (statMode) statMode.innerText = `${sb.baud || 921600} bps • Mode: ${sb.mode || "tcp_pty"}`;

                // Dynamic Baud Rate Selector (auto-sync to active connected baud)
                const selBaud = document.getElementById("select-bridge-baud");
                if (selBaud && sb.baud && document.activeElement !== selBaud) {
                    const baudStr = String(sb.baud);
                    let opt = Array.from(selBaud.options).find(o => o.value === baudStr);
                    if (!opt) {
                        opt = document.createElement("option");
                        opt.value = baudStr;
                        opt.textContent = `${baudStr} bps`;
                        selBaud.appendChild(opt);
                    }
                    selBaud.value = baudStr;
                }
                if (statClients) statClients.innerText = `Port ${sb.tcp_port || 7777} (${sb.clients_connected || 0} client aktif)`;
            }

            // SWAP Status and Buttons
            if (data && data.swap) {
                const swapCfg = parseInt(data.swap.config_mb) || 0;
                [0, 2048, 3072, 4096].forEach(sz => {
                    document.getElementById(`tw-swap-${sz}`)?.classList.toggle('active', swapCfg === sz);
                });
                const sTotal = (data.swap.total_mb / 1024).toFixed(1);
                const sUsed = (data.swap.used_mb / 1024).toFixed(1);
                const statSwapEl = document.getElementById('stat-swap-text');
                if (statSwapEl) {
                    statSwapEl.innerText = `${sUsed} GB / ${sTotal} GB (${data.swap.file_active ? 'Active' : 'zRAM'})`;
                }
            }

            // Audio Gain (Digital, Analog, Ear PA)
            if (data && data.audio_gain) {
                // 1. Digital Gain
                const dig = parseInt(data.audio_gain.digital) || 84;
                const gainSlider = document.getElementById('gain-slider');
                if (gainSlider && document.activeElement !== gainSlider && !isGainUpdating && targetGain === null) {
                    gainSlider.value = dig;
                }
                updateGainPreview(dig);

                // 2. Analog Gain
                const ana = parseInt(data.audio_gain.analog);
                const anaVal = !isNaN(ana) ? ana : 20;
                const anaSlider = document.getElementById('gain-analog-slider');
                if (anaSlider && document.activeElement !== anaSlider && !isAnalogGainUpdating && targetAnalogGain === null) {
                    anaSlider.value = anaVal;
                }
                updateAnalogGainPreview(anaVal);

                // 3. Ear PA Gain
                const ear = data.audio_gain.ear_pa || 'G_6_DB';
                const earLbl = document.getElementById('gain-ear-label');
                if (earLbl) {
                    const earDb = ear.replace('G_', '+').replace('_DB', ' dB').replace('+0', '0');
                    earLbl.innerText = earDb;
                }
                ['G_0_DB', 'G_6_DB', 'G_9_DB', 'G_12_DB', 'G_16_DB'].forEach(g => {
                    document.getElementById(`tw-ear-${g}`)?.classList.toggle('active', ear === g);
                });
            }
        }

        let targetGain = null;
        let isGainUpdating = false;

        function updateGainPreview(val) {
            const dig = parseInt(val, 10);
            const dB = ((dig - 84) * 0.5).toFixed(1);
            const sign = parseFloat(dB) > 0 ? '+' : '';
            const gainLabel = document.getElementById('gain-digital-label');
            if (gainLabel) {
                gainLabel.innerText = `${sign}${dB} dB (${dig})`;
            }
            [84, 90, 96, 104].forEach(g => {
                document.getElementById(`tw-gain-${g}`)?.classList.toggle('active', dig === g);
            });
        }

        function handleGainInput(val) {
            const intVal = parseInt(val, 10);
            updateGainPreview(intVal);
            targetGain = intVal;
            if (!isGainUpdating) {
                processGainQueue();
            }
        }

        function handleGainChange(val) {
            handleGainInput(val);
        }

        async function processGainQueue() {
            if (targetGain === null) {
                isGainUpdating = false;
                return;
            }

            isGainUpdating = true;
            while (targetGain !== null) {
                const valToSend = targetGain;
                targetGain = null;
                try {
                    await getApiData(`set_gain&val=${valToSend}`);
                } catch (e) {
                    console.error("Gain sync failed:", e);
                }
            }
            isGainUpdating = false;
        }

        let targetAnalogGain = null;
        let isAnalogGainUpdating = false;

        function updateAnalogGainPreview(val) {
            const ana = parseInt(val, 10);
            const lbl = document.getElementById('gain-analog-label');
            if (lbl) lbl.innerText = `${ana} / 24`;
        }

        function handleAnalogGainInput(val) {
            const intVal = parseInt(val, 10);
            updateAnalogGainPreview(intVal);
            targetAnalogGain = intVal;
            if (!isAnalogGainUpdating) {
                processAnalogGainQueue();
            }
        }

        function handleAnalogGainChange(val) {
            handleAnalogGainInput(val);
        }

        async function processAnalogGainQueue() {
            if (targetAnalogGain === null) {
                isAnalogGainUpdating = false;
                return;
            }
            isAnalogGainUpdating = true;
            while (targetAnalogGain !== null) {
                const valToSend = targetAnalogGain;
                targetAnalogGain = null;
                try {
                    await getApiData(`set_gain&mode=analog&val=${valToSend}`);
                } catch (e) {
                    console.error("Analog gain sync failed:", e);
                }
            }
            isAnalogGainUpdating = false;
        }

        async function applyEarPaGain(val) {
            const isId = (currentLang === 'id');
            const earDb = val.replace('G_', '+').replace('_DB', ' dB').replace('+0', '0');
            showToast(isId ? `Mengatur Ear PA Gain ${earDb}...` : `Setting Ear PA Gain ${earDb}...`, 'loading');
            ['G_0_DB', 'G_6_DB', 'G_9_DB', 'G_12_DB', 'G_16_DB'].forEach(g => {
                document.getElementById(`tw-ear-${g}`)?.classList.toggle('active', val === g);
            });
            const earLbl = document.getElementById('gain-ear-label');
            if (earLbl) earLbl.innerText = earDb;

            try {
                const res = await getApiData(`set_gain&mode=ear_pa&val=${encodeURIComponent(val)}`);
                showToast(res && res.message ? res.message : `Ear PA Gain ${earDb} aktif`, 'success', 2000);
            } catch (e) {
                showToast(isId ? 'Gagal mengatur Ear PA Gain' : 'Failed to set Ear PA Gain', 'error', 3000);
            }
        }

        async function runAudioTest(mode, dur = 5) {
            const isId = (currentLang === 'id');
            const badge = document.getElementById('badge-audio-test');
            if (mode === 'stop') {
                showToast(isId ? 'Menghentikan sinyal uji audio...' : 'Stopping audio test...', 'loading');
                try {
                    await getApiData(`audio_test&mode=stop`);
                    showToast(isId ? 'Sinyal uji audio dihentikan' : 'Audio test stopped', 'success', 2000);
                    if (badge) {
                        badge.innerText = 'IDLE';
                        badge.className = 'badge';
                    }
                } catch (e) {
                    showToast(isId ? 'Gagal menghentikan tes' : 'Failed to stop test', 'error', 2500);
                }
                return;
            }

            const desc = mode === 'sweep' ? 'Sweep 20Hz - 20kHz' : `${mode} Hz`;
            showToast(isId ? `Memutar sinyal kalibrasi: ${desc} (${dur} detik)...` : `Playing calibration tone: ${desc} (${dur}s)...`, 'loading');
            if (badge) {
                badge.innerText = 'PLAYING';
                badge.className = 'badge active pulse';
            }
            try {
                await getApiData(`audio_test&mode=${encodeURIComponent(mode)}&val=${dur}`);
                showToast(isId ? `Sinyal kalibrasi ${desc} aktif` : `Calibration tone ${desc} playing`, 'success', 3000);
                setTimeout(() => {
                    if (badge && badge.innerText === 'PLAYING') {
                        badge.innerText = 'IDLE';
                        badge.className = 'badge';
                    }
                }, dur * 1000);
            } catch (e) {
                showToast(isId ? 'Gagal memutar sinyal tes' : 'Failed to play test tone', 'error', 3000);
                if (badge) {
                    badge.innerText = 'IDLE';
                    badge.className = 'badge';
                }
            }
        }

        async function changeGain(type, val) {
            handleGainInput(val);
        }

        async function applyGainPreset(preset) {
            const isId = (currentLang === 'id');
            const pName = preset === 'preset_stock' ? 'Stock (0 dB)' : (preset === 'preset_boost' ? 'Boost (+3 dB)' : (preset === 'preset_loud' ? 'Loud (+6 dB)' : 'Max (+10 dB)'));
            
            // Optimistic update of UI
            let pDig = 84, pAna = 20, pEar = 'G_6_DB';
            if (preset === 'preset_boost') { pDig = 90; pAna = 21; pEar = 'G_9_DB'; }
            else if (preset === 'preset_loud') { pDig = 96; pAna = 22; pEar = 'G_12_DB'; }
            else if (preset === 'preset_max') { pDig = 104; pAna = 24; pEar = 'G_16_DB'; }

            updateGainPreview(pDig);
            updateAnalogGainPreview(pAna);
            const gSlider = document.getElementById('gain-slider');
            if (gSlider) gSlider.value = pDig;
            const aSlider = document.getElementById('gain-analog-slider');
            if (aSlider) aSlider.value = pAna;
            ['G_0_DB', 'G_6_DB', 'G_9_DB', 'G_12_DB', 'G_16_DB'].forEach(g => {
                document.getElementById(`tw-ear-${g}`)?.classList.toggle('active', pEar === g);
            });
            const earLbl = document.getElementById('gain-ear-label');
            if (earLbl) earLbl.innerText = pEar.replace('G_', '+').replace('_DB', ' dB').replace('+0', '0');

            showToast(isId ? `Menerapkan Preset Gain ${pName}...` : `Applying Gain Preset ${pName}...`, 'loading');
            try {
                const res = await getApiData(`set_gain&mode=${preset}`);
                showToast(res && res.message ? res.message : (isId ? `Preset Gain ${pName} aktif` : `Gain Preset ${pName} active`), 'success', 2500);
                setTimeout(fetchStatus, 300);
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
            }
        }

        async function applySwapTweak(mb) {
            const isId = (currentLang === 'id');
            showToast(isId ? `Mengatur SWAP ${mb} MB (Mohon tunggu)...` : `Configuring SWAP ${mb} MB (Please wait)...`, 'loading');
            const btn = document.getElementById(`tw-swap-${mb}`);
            const origHtml = btn ? btn.innerHTML : '';
            if (btn) btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> ...';
            try {
                const res = await getApiData(`apply_tweak&tweak=swap&val=${mb}`);
                showToast(res && res.message ? res.message : (isId ? `SWAP ${mb} MB berhasil dikonfigurasi` : `SWAP ${mb} MB configured successfully`), 'success', 3000);
            } catch (e) {
                showToast("SWAP Error: " + e.message, 'error', 4000);
            } finally {
                if (btn) btn.innerHTML = origHtml;
                setTimeout(fetchStatus, 1000);
            }
        }

        async function restartAudio() {
            const isId = (currentLang === 'id');
            showToast(isId ? "Memuat ulang AudioServer..." : "Restarting AudioServer...", 'loading');
            try {
                const res = await getApiData('restart_audio');
                showToast(res && res.message ? res.message : (isId ? "AudioServer berhasil dimuat ulang" : "AudioServer restarted successfully"), 'success', 3000);
                setTimeout(fetchStatus, 1000);
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
            }
        }

        let isVolumeSending = false;
        let pendingVolume = null;
        let lastVolumeInteraction = 0;

        function updateVolumePreview(val) {
            const slider = document.getElementById('vol-slider');
            const vMax = slider ? slider.max || 25 : 25;
            const lbl = document.getElementById('vol-label');
            if (lbl) lbl.innerText = `${val} / ${vMax}`;
        }

        function handleDeviceVolumeInput(val) {
            const intVal = parseInt(val, 10);
            lastVolumeInteraction = Date.now();
            updateVolumePreview(intVal);
            pendingVolume = intVal;
            const curDev = g_audio_devices.find(d => d.id === g_selected_device_id);
            if (curDev) curDev.volume = intVal;
            dispatchVolume();
        }

        function handleDeviceVolumeChange(val) {
            handleDeviceVolumeInput(val);
        }

        function handleVolumeInput(val) {
            handleDeviceVolumeInput(val);
        }

        function handleVolumeChange(val) {
            handleDeviceVolumeInput(val);
        }

        async function dispatchVolume() {
            if (isVolumeSending) return;
            if (pendingVolume === null) return;

            isVolumeSending = true;
            while (pendingVolume !== null) {
                const toSend = pendingVolume;
                pendingVolume = null;
                try {
                    await getApiData(`set_device_volume&target_device=${encodeURIComponent(g_selected_device_id)}&val=${toSend}`);
                } catch (e) {
                    console.error("Volume set failed:", e);
                }
            }
            isVolumeSending = false;
        }

        async function changeVolume(val) {
            handleDeviceVolumeChange(val);
        }

        function updateMarquee(el, box, animId) {
            if (!el || !box) return;
            requestAnimationFrame(() => {
                const textW = el.scrollWidth;
                const boxW = box.clientWidth;

                if (el.dataset.textW == textW && el.dataset.boxW == boxW) return;
                el.dataset.textW = textW;
                el.dataset.boxW = boxW;

                let styleEl = document.getElementById(`style-${animId}`);
                if (!styleEl) {
                    styleEl = document.createElement('style');
                    styleEl.id = `style-${animId}`;
                    document.head.appendChild(styleEl);
                }

                if (textW > boxW) {
                    box.classList.add('has-overflow');
                    const scrollDist = textW - boxW + 24;
                    const duration = Math.max(5, scrollDist / 20);

                    styleEl.textContent = `
                        @keyframes kf-${animId} {
                            0%, 12% { transform: translateX(0px); }
                            50%, 65% { transform: translateX(-${scrollDist}px); }
                            90%, 100% { transform: translateX(0px); }
                        }
                    `;
                    void el.offsetWidth;
                    el.style.animation = `kf-${animId} ${duration}s ease-in-out infinite`;
                } else {
                    box.classList.remove('has-overflow');
                    styleEl.textContent = '';
                    el.style.animation = 'none';
                    el.style.transform = 'none';
                }
            });
        }

        let isFetchingStatus = false;
        let consecutiveFailures = 0;
        let lastLivePulseTimer = null;

        function setLiveStatus(isOnline) {
            const badgeLive = document.getElementById('badge-live');
            if (!badgeLive) return;

            const lang = currentLang;
            const dict = i18n[lang] || i18n.en;

            if (isOnline) {
                const label = dict.badge_live || "ONLINE";
                badgeLive.className = 'badge active-green';

                let dot = document.getElementById('badge-live-dot');
                let text = document.getElementById('badge-live-text');

                if (!dot || !text) {
                    badgeLive.innerHTML = `<i class="fa-solid fa-circle-dot badge-dot" id="badge-live-dot"></i> <span id="badge-live-text">${label}</span>`;
                    dot = document.getElementById('badge-live-dot');
                    text = document.getElementById('badge-live-text');
                } else {
                    if (text.textContent !== label) text.textContent = label;
                }

                // Berkedip halus hanya pada ikon lingkaran (bukan detak pada seluruh badge)
                if (dot) {
                    dot.className = 'fa-solid fa-circle-dot badge-dot';
                    void dot.offsetWidth; // Force reflow untuk me-restart animasi kedip halus
                    dot.className = 'fa-solid fa-circle-dot badge-dot live-blink';

                    if (lastLivePulseTimer) clearTimeout(lastLivePulseTimer);
                    lastLivePulseTimer = setTimeout(() => {
                        if (dot) dot.className = 'fa-solid fa-circle-dot badge-dot';
                    }, 650);
                }
            } else {
                const label = dict.badge_offline || "OFFLINE";
                badgeLive.className = 'badge active-red';
                badgeLive.innerHTML = `<i class="fa-solid fa-circle-xmark badge-dot" id="badge-live-dot"></i> <span id="badge-live-text">${label}</span>`;
            }
        }

        async function fetchStatus() {
            if (isFetchingStatus) return;
            isFetchingStatus = true;
            try {
                const data = await getApiData('status');
                if (!data || !data.success) {
                    if (!isSseConnected) {
                        consecutiveFailures++;
                        if (consecutiveFailures >= 2) {
                            setLiveStatus(false);
                        }
                    }
                    return;
                }
                consecutiveFailures = 0;
                setLiveStatus(true);
                window.lastStatusData = data;
                try {
                    sessionStorage.setItem('jacktor_cached_status', JSON.stringify(data));
                } catch(e) {}
                renderTelemetry(data);
            } catch (err) {
                console.error("fetchStatus error:", err);
                if (!isSseConnected) {
                    consecutiveFailures++;
                    if (consecutiveFailures >= 2) {
                        setLiveStatus(false);
                    }
                }
            } finally {
                isFetchingStatus = false;
            }
        }

        function getUsbDeviceInfo(dev, lang) {
            const type = (dev.type || '').toLowerCase();
            const prod = (dev.prod || '').toLowerCase();
            const mfr = (dev.mfr || '').toLowerCase();
            const text = `${type} ${prod} ${mfr}`;
            const isId = lang === 'id';

            // 0. Programmer & Debugging Interface (Qualcomm / CSR, ST-Link, J-Link, etc.)
            if (type === 'programmer' || type === 'debug' || text.includes('programmer') || text.includes('debug') || text.includes('spi converter') || text.includes('jtag')) {
                return {
                    icon: 'fa-solid fa-microchip',
                    color: '#f97316',
                    badge: isId ? 'Programmer / Debug' : 'Programmer / Debug',
                    badgeBg: 'rgba(249, 115, 22, 0.12)'
                };
            }

            // 1. Mouse
            if (type === 'mouse' || text.includes('mouse') || text.includes('trackball')) {
                return {
                    icon: 'fa-solid fa-computer-mouse',
                    color: '#38bdf8',
                    badge: isId ? 'Mouse' : 'Mouse',
                    badgeBg: 'rgba(56, 189, 248, 0.12)'
                };
            }
            // 2. Keyboard
            if (type === 'keyboard' || text.includes('keyboard') || text.includes('keypad')) {
                return {
                    icon: 'fa-solid fa-keyboard',
                    color: '#a78bfa',
                    badge: isId ? 'Keyboard' : 'Keyboard',
                    badgeBg: 'rgba(167, 139, 250, 0.12)'
                };
            }
            // 3. Camera / Webcam
            if (type === 'camera' || text.includes('camera') || text.includes('webcam') || text.includes('uvc') || text.includes('video')) {
                return {
                    icon: 'fa-solid fa-video',
                    color: '#f43f5e',
                    badge: isId ? 'Kamera' : 'Camera',
                    badgeBg: 'rgba(244, 63, 94, 0.12)'
                };
            }
            // 4. DVD / CD / Optical Drive
            if (type === 'dvd' || text.includes('dvd') || text.includes('cd-rom') || text.includes('cdrom') || text.includes('optical') || text.includes('bluray') || text.includes('bd-rom')) {
                return {
                    icon: 'fa-solid fa-compact-disc',
                    color: '#fbbf24',
                    badge: isId ? 'Drive Optik (DVD/CD)' : 'Optical Drive (DVD/CD)',
                    badgeBg: 'rgba(251, 191, 36, 0.12)'
                };
            }
            // 5. Storage / Flashdisk / External Drive
            if (type === 'storage' || text.includes('storage') || text.includes('flash') || text.includes('drive') || text.includes('disk') || text.includes('cruzer') || text.includes('sandisk') || text.includes('kingston') || text.includes('transcend') || text.includes('sdcard')) {
                return {
                    icon: 'fa-solid fa-hard-drive',
                    color: '#34d399',
                    badge: isId ? 'Penyimpanan (Storage)' : 'Mass Storage',
                    badgeBg: 'rgba(52, 211, 153, 0.12)'
                };
            }
            // 6. MTP / Smartphone / Mobile Device
            if (type === 'mtp' || text.includes('mtp') || text.includes('phone') || text.includes('android') || text.includes('iphone') || text.includes('ipad') || text.includes('mobile')) {
                return {
                    icon: 'fa-solid fa-mobile-screen',
                    color: '#60a5fa',
                    badge: isId ? 'Perangkat Ponsel (MTP)' : 'Mobile Phone (MTP)',
                    badgeBg: 'rgba(96, 165, 250, 0.12)'
                };
            }
            // 7. USB Hub
            if (type === 'hub' || text.includes('hub')) {
                return {
                    icon: 'fa-solid fa-network-wired',
                    color: '#818cf8',
                    badge: isId ? 'USB Hub' : 'USB Hub',
                    badgeBg: 'rgba(129, 140, 248, 0.12)'
                };
            }
            // 8. LAN / Ethernet Adapter
            if (type === 'lan' || text.includes('ethernet') || text.includes('lan') || text.includes('rndis') || text.includes('rtl815') || text.includes('ax88') || text.includes('network')) {
                return {
                    icon: 'fa-solid fa-ethernet',
                    color: '#2dd4bf',
                    badge: isId ? 'Ethernet LAN' : 'Ethernet LAN',
                    badgeBg: 'rgba(45, 212, 191, 0.12)'
                };
            }
            // 9. Wi-Fi Wireless Adapter
            if (type === 'wifi' || text.includes('wifi') || text.includes('wi-fi') || text.includes('wireless') || text.includes('wlan') || text.includes('802.11')) {
                return {
                    icon: 'fa-solid fa-wifi',
                    color: '#38bdf8',
                    badge: isId ? 'Adaptor Wi-Fi' : 'Wi-Fi Adapter',
                    badgeBg: 'rgba(56, 189, 248, 0.12)'
                };
            }
            // 10. Audio DAC / Sound Device / Headset
            if (type === 'audio' || text.includes('audio') || text.includes('dac') || text.includes('sound') || text.includes('headset') || text.includes('headphone') || text.includes('speaker') || text.includes('pcm') || text.includes('codec')) {
                return {
                    icon: 'fa-solid fa-headphones',
                    color: '#4ade80',
                    badge: isId ? 'Audio DAC' : 'Audio DAC',
                    badgeBg: 'rgba(74, 222, 128, 0.12)'
                };
            }
            // 11. Serial / UART Bridge
            if (type === 'serial' || text.includes('serial') || text.includes('uart') || text.includes('cp210') || text.includes('ch34') || text.includes('ftdi') || text.includes('pl2303') || text.includes('bridge')) {
                return {
                    icon: 'fa-solid fa-terminal',
                    color: '#f59e0b',
                    badge: isId ? 'Serial UART' : 'Serial UART',
                    badgeBg: 'rgba(245, 158, 11, 0.12)'
                };
            }
            // 12. Gamepad / Game Controller
            if (type === 'gamepad' || text.includes('gamepad') || text.includes('joystick') || text.includes('controller') || text.includes('xbox') || text.includes('playstation') || text.includes('dualshock')) {
                return {
                    icon: 'fa-solid fa-gamepad',
                    color: '#ec4899',
                    badge: isId ? 'Gamepad' : 'Gamepad',
                    badgeBg: 'rgba(236, 72, 153, 0.12)'
                };
            }
            // 13. Printer
            if (type === 'printer' || text.includes('printer') || text.includes('print')) {
                return {
                    icon: 'fa-solid fa-print',
                    color: '#94a3b8',
                    badge: isId ? 'Printer' : 'Printer',
                    badgeBg: 'rgba(148, 163, 184, 0.12)'
                };
            }
            // 14. Bluetooth Dongle
            if (type === 'bluetooth' || text.includes('bluetooth') || text.includes('bt dongle')) {
                return {
                    icon: 'fa-brands fa-bluetooth',
                    color: '#3b82f6',
                    badge: isId ? 'Bluetooth' : 'Bluetooth',
                    badgeBg: 'rgba(59, 130, 246, 0.12)'
                };
            }

            // Default: Generic Unknown USB Device -> General
            return {
                icon: 'fa-brands fa-usb',
                color: '#94a3b8',
                badge: 'General',
                badgeBg: 'rgba(148, 163, 184, 0.12)'
            };
        }

        function renderTelemetry(data) {
            const lang = currentLang;
            const dict = i18n[lang] || i18n.en;

            // 1. Header & IP Badge
            const ip = (data.ip || '').trim();
            const badgeIp = document.getElementById('badge-ip');
            if (badgeIp) {
                if (ip && ip !== '127.0.0.1' && ip !== '0.0.0.0') {
                    badgeIp.innerHTML = `<i class="fa-solid fa-network-wired"></i> IP: ${ip}`;
                    badgeIp.className = 'badge active';
                } else {
                    badgeIp.innerHTML = `<i class="fa-solid fa-network-wired"></i> IP: Offline`;
                    badgeIp.className = 'badge';
                }
            }

            const statIpPort = document.getElementById('stat-ip-port');
            if (statIpPort) {
                statIpPort.innerText = ip || 'Offline';
            }

            // 2. Header USB Mode Badge (USB: HOST / USB: PERIPHERAL / USB: AUTO)
            const mode = (data.ssusb_mode || 'none').trim();
            const badgeOtg = document.getElementById('badge-otg');
            if (badgeOtg) {
                badgeOtg.innerText = `USB: ${mode.toUpperCase()}`;
                badgeOtg.className = mode === 'host' ? 'badge active' : 'badge';
            }

            const statMode = document.getElementById('stat-ssusb-mode');
            if (statMode) statMode.innerText = mode.toUpperCase();

            // USB Controller Buttons
            const targetMode = data.usb_target_mode || (mode === 'host' ? 'host' : 'device');
            updateUsbButtons(targetMode);

            const modeLabel = document.getElementById('otg-mode-label');
            if (modeLabel) {
                modeLabel.innerText = mode.toUpperCase();
                modeLabel.className = mode === 'host' ? 'badge active-green' : 'badge active-warn';
            }

            // 3. VBUS Voltage & Cable Connection Telemetry
            const v = data.voltage_now || 0;
            const volts = (v / 1000000).toFixed(2);
            const isPlugged = data.otg_cable_connected === 1 || (data.usb_ext_count && data.usb_ext_count > 0);
            
            const vElem = document.getElementById('stat-vbus');
            if (vElem) {
                if (v > 4000000) {
                    if (isPlugged) {
                        vElem.innerText = `${volts} V (${dict.vbus_active})`;
                        vElem.className = 'stat-val val-green';
                    } else {
                        vElem.innerText = `${volts} V (${dict.vbus_idle})`;
                        vElem.className = 'stat-val val-warn';
                    }
                } else {
                    vElem.innerText = `${volts} V (${dict.vbus_off})`;
                    vElem.className = 'stat-val val-red';
                }
            }

            const statOnline = document.getElementById('stat-otg-online');
            if (statOnline) {
                if (isPlugged) {
                    const devCount = data.usb_ext_count || 0;
                    statOnline.innerText = devCount > 0 ? `${dict.cable_plugged} (${devCount} Dev)` : dict.cable_plugged;
                    statOnline.className = 'stat-val val-green';
                } else {
                    statOnline.innerText = dict.cable_disconnected;
                    statOnline.className = 'stat-val val-red';
                }
            }

            const statUdc = document.getElementById('stat-udc');
            if (statUdc) statUdc.innerText = (data.udc || '').trim() || '(Unbound / Host)';

            // 4. Track Telemetry & Auto-Marquee (No "null" display!)
            const trTitle = document.getElementById('track-title');
            const boxTitle = document.getElementById('box-track-title');
            const trArtist = document.getElementById('track-artist');
            const boxArtist = document.getElementById('box-track-artist');

            const hasActiveSong = data.track && 
                                  data.track.title && 
                                  data.track.title.trim() !== '' && 
                                  data.track.title !== 'null' && 
                                  data.track.state !== 'Standby';

            // Album Art or Headphone Icon
            const artBox = document.getElementById('track-art-box');
            if (artBox) {
                if (hasActiveSong && data.track && data.track.cover) {
                    artBox.classList.add('has-cover');
                    const currentImg = artBox.querySelector('img');
                    if (!currentImg) {
                        artBox.innerHTML = `<img src="${data.track.cover}" alt="Cover Art" class="track-cover-img" onerror="this.onerror=null;this.parentElement.classList.remove('has-cover');this.parentElement.innerHTML='<i class=\\\'fa-solid fa-headphones\\\'></i>';">`;
                    } else if (currentImg.getAttribute('src') !== data.track.cover) {
                        currentImg.src = data.track.cover;
                    }
                } else {
                    artBox.classList.remove('has-cover');
                    if (!artBox.querySelector('i.fa-headphones')) {
                        artBox.innerHTML = '<i class="fa-solid fa-headphones"></i>';
                    }
                }
            }

            if (hasActiveSong) {
                const newTitle = data.track.title;
                let artistText = data.track.artist || 'Audio Engine';
                if (data.track.album && data.track.album !== 'null') artistText += ` • ${data.track.album}`;
                
                if (trTitle && trTitle.innerText !== newTitle) {
                    trTitle.innerText = newTitle;
                }
                updateMarquee(trTitle, boxTitle, 'title');

                if (trArtist && trArtist.innerText !== artistText) {
                    trArtist.innerText = artistText;
                }
                updateMarquee(trArtist, boxArtist, 'artist');
            } else {
                if (trTitle) {
                    trTitle.innerText = dict.track_placeholder_title;
                    trTitle.style.animation = 'none';
                    trTitle.style.transform = 'none';
                }
                if (boxTitle) boxTitle.classList.remove('has-overflow');

                if (trArtist) {
                    trArtist.innerText = dict.track_placeholder_artist;
                    trArtist.style.animation = 'none';
                    trArtist.style.transform = 'none';
                }
                if (boxArtist) boxArtist.classList.remove('has-overflow');
            }

            const badgeTrack = document.getElementById('track-status-badge');
            if (badgeTrack) {
                const st = (data.track && data.track.state) || 'Standby';
                badgeTrack.innerText = st;
                badgeTrack.className = st === 'Playing' ? 'badge active pulse' : 'badge';
            }

            const isPlaying = (data.track && data.track.state === "Playing");
            const isPaused = (data.track && data.track.state === "Paused");
            const isStandby = (!isPlaying && !isPaused);

            const trBit = document.getElementById("track-bit-depth");
            if (trBit) trBit.innerText = isStandby ? "-" : ((data.track && data.track.format && data.track.format !== "-") ? data.track.format : "-");

            const rowBrate = document.getElementById("row-track-bitrate");
            const trBrate = document.getElementById("track-bitrate");
            if (!isStandby && data.track && data.track.bitrate && data.track.bitrate !== "-") {
                if (rowBrate) rowBrate.style.display = "flex";
                if (trBrate) trBrate.innerText = data.track.bitrate;
            } else if (rowBrate) {
                rowBrate.style.display = "none";
            }

            const trRate = document.getElementById("track-sample-rate");
            if (trRate) trRate.innerText = isStandby ? "-" : ((data.track && data.track.sample_rate && data.track.sample_rate !== "-") ? data.track.sample_rate : "-");

            const trApp = document.getElementById("track-app");
            if (trApp) trApp.innerText = isStandby ? "-" : ((data.track && data.track.app && data.track.app !== "-") ? data.track.app : "-");

            const trState = document.getElementById('track-state');
            if (trState) {
                const st = (data.track && data.track.state) || 'Standby';
                trState.innerText = st;
                trState.className = st === 'Playing' ? 'stat-val val-green' : (st === 'Paused' ? 'stat-val val-warn' : 'stat-val');
            }

            // Media Controls Play/Pause Icon
            const iconPlay = document.getElementById('icon-media-play');
            if (iconPlay) {
                const isPlaying = data.track && data.track.state === 'Playing';
                iconPlay.className = isPlaying ? 'fa-solid fa-pause' : 'fa-solid fa-play';
            }

            // Pipeline Flow Text
            const pSource = document.getElementById("pipe-source");
            if (pSource) {
                if (isStandby) {
                    pSource.innerText = "-";
                } else {
                    const sRate = (data.track && data.track.sample_rate && data.track.sample_rate !== "-") ? data.track.sample_rate.replace(" kHz", "k") : "-";
                    const sBit = (data.track && data.track.bit_depth && data.track.bit_depth !== "-") ? data.track.bit_depth.replace("-bit", "b") : "-";
                    pSource.innerText = (sRate !== "-" && sBit !== "-") ? `${sRate} • ${sBit}` : "-";
                }
            }

            const pDsp = document.getElementById('pipe-dsp');
            if (pDsp) {
                if (data.resampler && data.resampler.mode === 'match_source') {
                    pDsp.innerText = 'ALSA Direct Engine';
                    pDsp.className = 'pipe-val val-green';
                } else {
                    const fRate = data.fixed_sample_rate ? (parseInt(data.fixed_sample_rate) / 1000) + 'k' : '96k';
                    pDsp.innerText = `ALSA Fixed (${fRate})`;
                    pDsp.className = 'pipe-val val-cyan';
                }
            }

            const pOut = document.getElementById('pipe-out');
            if (pOut) {
                const dFreq = (data.dac && data.dac.freq && data.dac.freq !== '-') ? (parseInt(data.dac.freq) / 1000) + 'k' : '96k';
                const dBit = (data.dac && data.dac.bit_depth && data.dac.bit_depth !== '-') ? data.dac.bit_depth.replace('-bit PCM', 'b') : '24b';
                pOut.innerText = `${dFreq} • ${dBit}`;
            }

            // 5. Hardware DAC Telemetry
            const dacFound = data.dac && (data.dac.found === 1 || data.dac.card !== '');
            const statDacName = document.getElementById('stat-dac-name');
            if (statDacName) {
                statDacName.innerText = dacFound ? data.dac.name || 'External USB DAC' : 'None Detected';
                statDacName.className = dacFound ? 'stat-val val-green' : 'stat-val val-warn';
            }

            // Hardware Maximum DAC Capabilities
            const statDacHwCap = document.getElementById('stat-dac-hw-cap');
            if (statDacHwCap) {
                statDacHwCap.innerText = (data.dac && data.dac.hw_capabilities) ? data.dac.hw_capabilities : '96 kHz • 24-bit • Stereo (2 Ch)';
            }

            const statDacRates = document.getElementById('stat-dac-rates');
            if (statDacRates) {
                statDacRates.innerText = (data.dac && data.dac.rates_list) ? `${data.dac.rates_list} (Hi-Res Audio)` : '8k - 96k Hz (Hi-Res Audio)';
            }

            const statDacBits = document.getElementById('stat-dac-bits');
            if (statDacBits) {
                statDacBits.innerText = (data.dac && data.dac.bit_depth && data.dac.bit_depth !== '-') ? data.dac.bit_depth : '24-bit PCM';
            }

            const statDacFreq = document.getElementById('stat-dac-freq');
            if (statDacFreq) {
                statDacFreq.innerText = (data.dac && data.dac.freq && data.dac.freq !== '-') ? `${data.dac.freq}` : '96000 Hz';
            }

            const statDacFmt = document.getElementById('stat-dac-format');
            if (statDacFmt) {
                statDacFmt.innerText = (data.dac && data.dac.format && data.dac.format !== '-') ? data.dac.format : 'S24_3LE';
            }

            const statDacCard = document.getElementById('stat-dac-card');
            if (statDacCard) {
                statDacCard.innerText = (data.dac && data.dac.card) ? data.dac.card : 'card1';
            }

            const statDacBuf = document.getElementById('stat-dac-buffer');
            if (statDacBuf) {
                statDacBuf.innerText = (data.dac && data.dac.buffer_size && data.dac.buffer_size !== '-') ? `${data.dac.buffer_size} Frames` : '4800 Frames';
            }

            const badgeDac = document.getElementById('dac-status-badge');
            if (badgeDac) {
                const isRun = data.dac && (data.dac.status === 'RUNNING' || (data.dac.status && data.dac.status.indexOf('Active') !== -1));
                badgeDac.innerText = isRun ? 'STREAMING' : (dacFound ? 'STANDBY' : 'OFFLINE');
                badgeDac.className = isRun ? 'badge active pulse' : 'badge';
            }

            // Volume Slider (Sync with currently selected target device)
            const curTargetDev = g_audio_devices.find(d => d.id === g_selected_device_id);
            const targetDevVol = curTargetDev ? curTargetDev.volume : data.volume;
            if (targetDevVol !== undefined) {
                const slider = document.getElementById('vol-slider');
                const vMax = (curTargetDev && curTargetDev.volume_max) || data.volume_max || 25;
                const now = Date.now();
                if (slider && !slider.matches(':active') && document.activeElement !== slider && !isVolumeSending && pendingVolume === null && (now - lastVolumeInteraction > 1500)) {
                    slider.max = vMax;
                    slider.value = targetDevVol;
                    const lbl = document.getElementById('vol-label');
                    if (lbl) lbl.innerText = `${targetDevVol} / ${vMax}`;
                }
            }

            // 6. CPU & Thermal Telemetry
            if (data.cpu) {
                const cLit = document.getElementById('stat-cpu-little');
                if (cLit) cLit.innerText = data.cpu.little_mhz || '1017 - 1804 MHz';

                const cBig = document.getElementById('stat-cpu-big');
                if (cBig) cBig.innerText = data.cpu.big_mhz || '1056 - 1612 MHz';

                const cTemp = document.getElementById('stat-cpu-temp');
                if (cTemp) {
                    const tempNum = parseInt(data.cpu.temp || '48');
                    cTemp.innerText = `${data.cpu.temp} (${tempNum < 55 ? 'Cool' : 'Warm'})`;
                    cTemp.className = tempNum < 55 ? 'stat-val val-green' : (tempNum < 70 ? 'stat-val val-warn' : 'stat-val val-red');
                }
            }

            const statKernel = document.getElementById('stat-kernel');
            if (statKernel) {
                if (data.kernel && data.kernel.display) {
                    statKernel.innerText = data.kernel.display;
                } else if (data.kernel && data.kernel.release) {
                    statKernel.innerText = data.kernel.release;
                } else if (data.kernel_release) {
                    statKernel.innerText = data.kernel_release;
                }
            }

            const statDevice = document.getElementById('stat-device');
            if (statDevice && data.device_model) {
                statDevice.innerText = data.device_model;
            }

            const statBatt = document.getElementById('stat-battery');
            if (statBatt && data.battery) {
                statBatt.innerText = `${data.battery.capacity}% (${data.battery.status})`;
            }

            const statUptime = document.getElementById('stat-uptime');
            if (statUptime) {
                if (currentLang === 'id' && data.uptime_id) {
                    statUptime.innerText = data.uptime_id;
                } else if (data.uptime_sec !== undefined && data.uptime_sec !== null) {
                    const sec = parseInt(data.uptime_sec, 10) || 0;
                    const d = Math.floor(sec / 86400);
                    const h = Math.floor((sec % 86400) / 3600);
                    const m = Math.floor((sec % 3600) / 60);
                    const s = sec % 60;
                    if (currentLang === 'id') {
                        statUptime.innerText = d > 0 ? `${d} hari, ${h} jam, ${m} menit` : (h > 0 ? `${h} jam ${m} menit` : (m > 0 ? `${m} menit ${s} detik` : `${s} detik`));
                    } else {
                        statUptime.innerText = d > 0 ? `${d} days, ${h} hours, ${m} mins` : (h > 0 ? `${h} hours, ${m} mins` : (m > 0 ? `${m} mins ${s} secs` : `${s} secs`));
                    }
                } else {
                    statUptime.innerText = data.uptime || '-';
                }
            }

            // 7. USB Device Topology List
            const countBadge = document.getElementById('usb-count');
            if (countBadge) countBadge.innerText = `${data.usb_ext_count || 0} Devices`;

            const usbContainer = document.getElementById('usb-device-container');
            if (usbContainer) {
                if (!data.usb_devices_list || data.usb_devices_list.length === 0) {
                    usbContainer.innerHTML = `<div class="usb-device-card" style="color: var(--text-dim); justify-content: center;">${dict.usb_no_devices || 'No external USB devices detected.'}</div>`;
                } else {
                    let html = '';
                    data.usb_devices_list.forEach(dev => {
                        const devInfo = getUsbDeviceInfo(dev, lang);
                        let mfrName = (dev.mfr || '').trim();
                        if (dev.vid === '0461' && (!mfrName || mfrName === 'Generic')) {
                            mfrName = 'Dell';
                        }
                        if (!mfrName) mfrName = 'Generic';

                        html += `
                            <div class="usb-device-card">
                                <div class="usb-device-icon" style="color: ${devInfo.color}; font-size: 1.2rem; display: flex; align-items: center; justify-content: center; width: 28px; height: 28px;">
                                    <i class="${devInfo.icon}"></i>
                                </div>
                                <div style="flex: 1; min-width: 0;">
                                    <div style="display: flex; justify-content: space-between; align-items: center; gap: 8px;">
                                        <div style="font-weight: 700; color: #fff; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">${dev.prod || 'USB Device'}</div>
                                        <span style="font-size: 0.65rem; padding: 2px 7px; border-radius: 4px; background: ${devInfo.badgeBg}; color: ${devInfo.color}; font-weight: 600; white-space: nowrap;">${devInfo.badge}</span>
                                    </div>
                                    <div style="font-size: 0.7rem; color: var(--text-dim); overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">${mfrName} • ${dev.vid}:${dev.pid} (Bus ${dev.bus || '001'} Dev ${dev.dev || '001'})</div>
                                </div>
                            </div>
                        `;
                    });
                    usbContainer.innerHTML = html;
                }
            }

            // 8. Update Manual Tweaks Button Active Highlights
            if (data.tweaks) {
                try {
                    updateTweakButtons(data.tweaks, data);
                } catch (eTwk) {
                    console.warn("updateTweakButtons error:", eTwk);
                }
            }

            // 9. Update OpenSSH Server Card Telemetry
            if (data.ssh) {
                try {
                    updateSshTelemetry(data.ssh, data.ip);
                } catch (eSsh) {
                    console.warn("updateSshTelemetry error:", eSsh);
                }
            }

            // 10. Amplifier Telemetry Sync
            if (data.amp_telemetry) {
                try {
                    updateAmpTelemetryUI(data.amp_telemetry);
                } catch (eAmp) {
                    console.warn("updateAmpTelemetryUI error:", eAmp);
                }
            }

            // 11. Audio Channel Mode Sync
            if (data.channel_mode !== undefined) {
                updateChannelModeUI(data.channel_mode);
            }
        }

        function updateChannelModeUI(ch) {
            const chVal = parseInt(ch);
            if (isNaN(chVal) || chVal < 0 || chVal > 3) return;
            const isId = currentLang === 'id';
            const chNames = ['STEREO (L/R)', 'KIRI (L-CH)', 'KANAN (R-CH)', 'MONO (L+R)'];
            const chNamesEn = ['STEREO (L/R)', 'LEFT (L-CH)', 'RIGHT (R-CH)', 'MONO (L+R)'];
            const nameStr = (isId ? chNames[chVal] : chNamesEn[chVal]) || 'STEREO';

            const ampBadge = document.getElementById('amp-ch-badge');
            if (ampBadge) {
                ampBadge.innerText = nameStr;
                ampBadge.className = chVal === 0 ? 'badge' : 'badge active-cyan';
            }
            const panelBadge = document.getElementById('panel-ch-badge');
            if (panelBadge) {
                panelBadge.innerText = nameStr;
                panelBadge.className = chVal === 0 ? 'badge' : 'badge active-cyan';
            }

            for (let i = 0; i < 4; i++) {
                const b1 = document.getElementById(`btn-ch-${i}`);
                if (b1) b1.classList.toggle('active', chVal === i);
                const b2 = document.getElementById(`btn-panel-ch-${i}`);
                if (b2) b2.classList.toggle('active', chVal === i);
            }
        }
        window.updateChannelModeUI = updateChannelModeUI;

        async function setChannelMode(mode) {
            try {
                updateChannelModeUI(mode);
                const res = await apiFetch(`/cgi-bin/jacktor-api?action=channel_mode&mode=${mode}`);
                const data = await res.json();
                if (data && data.success && data.channel_mode !== undefined) {
                    updateChannelModeUI(data.channel_mode);
                }
            } catch (e) {
                console.error("Failed to set channel mode:", e);
            }
        }
        window.setChannelMode = setChannelMode;

        let currKeyUser = 'root';
        let currSshTab = 'keys';

        function updateSshTelemetry(ssh, hostIp) {
            if (!ssh) return;
            const badge = document.getElementById('badge-ssh-status');
            const isRun = ssh.running === 1 || ssh.running === true;
            if (badge) {
                if (isRun) {
                    badge.innerHTML = `<i class="fa-solid fa-circle-check"></i> RUNNING (Port ${ssh.port})`;
                    badge.className = 'badge active-green';
                    badge.style.background = '';
                    badge.style.color = '';
                    badge.style.borderColor = '';
                } else {
                    badge.innerHTML = `<i class="fa-solid fa-circle-xmark"></i> STOPPED`;
                    badge.className = 'badge';
                    badge.style.background = 'rgba(239, 68, 68, 0.2)';
                    badge.style.color = '#ef4444';
                    badge.style.borderColor = 'rgba(239, 68, 68, 0.4)';
                }
            }

            document.getElementById('btn-ssh-start')?.classList.toggle('active', isRun);
            document.getElementById('btn-ssh-stop')?.classList.toggle('active', !isRun);

            const isAuto = ssh.autostart === 1 || ssh.autostart === true;
            document.getElementById('btn-ssh-auto-1')?.classList.toggle('active', isAuto);
            document.getElementById('btn-ssh-auto-0')?.classList.toggle('active', !isAuto);

            const portInput = document.getElementById('ssh-port-input');
            if (portInput && document.activeElement !== portInput) {
                portInput.value = ssh.port || 22;
            }

            const ip = ssh.ip || hostIp || window.location.hostname || '127.0.0.1';
            const port = ssh.port || 22;

            // Update Header SSH Badge with live IP and port
            const badgeHeaderSsh = document.getElementById('badge-ssh');
            if (badgeHeaderSsh) {
                if (isRun && ip && ip !== '127.0.0.1' && ip !== '0.0.0.0') {
                    badgeHeaderSsh.innerHTML = `<i class="fa-solid fa-terminal"></i> SSH: ${ip}:${port}`;
                    badgeHeaderSsh.className = 'badge active';
                    badgeHeaderSsh.title = `SSH Server running at ${ip}:${port} (Click to scroll)`;
                } else if (isRun) {
                    badgeHeaderSsh.innerHTML = `<i class="fa-solid fa-terminal"></i> SSH: Port ${port}`;
                    badgeHeaderSsh.className = 'badge active';
                    badgeHeaderSsh.title = `SSH Server running on Port ${port}`;
                } else {
                    badgeHeaderSsh.innerHTML = `<i class="fa-solid fa-terminal"></i> SSH: Offline`;
                    badgeHeaderSsh.className = 'badge';
                    badgeHeaderSsh.title = `SSH Server Stopped`;
                }
            }

            const epLabel = document.getElementById('ssh-endpoint-label');
            if (epLabel) epLabel.innerText = `Host: ${ip}:${port}`;
            const cmdRoot = document.getElementById('ssh-cmd-root');
            if (cmdRoot) cmdRoot.innerText = `ssh -p ${port} root@${ip}`;
            const cmdShell = document.getElementById('ssh-cmd-shell');
            if (cmdShell) cmdShell.innerText = `ssh -p ${port} shell@${ip}`;
        }

        async function sshAction(act) {
            const isId = (currentLang === 'id');
            showToast(isId ? `Menjalankan perintah SSH ${act}...` : `Executing SSH ${act}...`, 'loading');
            try {
                const res = await getApiData(`ssh_${act}`);
                showToast(res && res.message ? res.message : (isId ? `SSH ${act} berhasil` : `SSH ${act} successful`), 'success', 2500);
                setTimeout(fetchStatus, 1000);
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
            }
        }

        async function sshSetAutostart(val) {
            const isId = (currentLang === 'id');
            showToast(isId ? `Mengubah autostart SSH...` : `Configuring SSH autostart...`, 'loading');
            try {
                const res = await getApiData(`ssh_set_autostart&val=${val}`);
                showToast(res && res.message ? res.message : (isId ? `Autostart SSH diubah` : `SSH autostart updated`), 'success', 2500);
                setTimeout(fetchStatus, 500);
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
            }
        }

        async function sshSetPort() {
            const p = document.getElementById('ssh-port-input')?.value;
            if (!p) return;
            const isId = (currentLang === 'id');
            showToast(isId ? `Mengubah port SSH ke ${p}...` : `Changing SSH port to ${p}...`, 'loading');
            try {
                const res = await getApiData(`ssh_set_port&port=${p}`);
                showToast(res && res.message ? res.message : (isId ? `Port SSH berhasil diubah ke ${p}` : `SSH port changed to ${p}`), 'success', 2500);
                setTimeout(fetchStatus, 1000);
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
            }
        }

        function copySshCmd(u) {
            const el = document.getElementById(`ssh-cmd-${u}`);
            if (el && el.innerText) {
                navigator.clipboard.writeText(el.innerText).then(() => {
                    showToast(currentLang === 'id' ? `Perintah SSH ${u} disalin ke clipboard!` : `SSH command for ${u} copied to clipboard!`);
                });
            }
        }

        function switchSshTab(tab) {
            currSshTab = tab;
            ['keys', 'config', 'logs', 'password'].forEach(t => {
                const btn = document.getElementById(`tab-btn-${t}`);
                const panel = document.getElementById(`ssh-subpanel-${t}`);
                if (btn) btn.classList.toggle('active', t === tab);
                if (panel) panel.style.display = (t === tab) ? 'block' : 'none';
            });
            if (tab === 'keys') loadSshKeys();
            else if (tab === 'config') loadSshConfig();
            else if (tab === 'logs') loadSshLog();
        }

        function setKeyUser(u) {
            currKeyUser = u;
            document.getElementById('btn-user-root')?.classList.toggle('active', u === 'root');
            document.getElementById('btn-user-shell')?.classList.toggle('active', u === 'shell');
            loadSshKeys();
        }

        async function loadSshKeys() {
            const container = document.getElementById('ssh-keys-container');
            if (container) container.innerHTML = `<div style="font-size: 0.78rem; color: var(--text-dim); padding: 8px;"><i class="fa-solid fa-spinner fa-spin"></i> ${currentLang === 'id' ? 'Memuat daftar kunci...' : 'Loading keys...'}</div>`;
            try {
                const res = await getApiData(`ssh_get_keys&user=${currKeyUser}`);
                if (container) {
                    if (!res || !res.keys || res.keys.length === 0) {
                        const dict = i18n[currentLang] || i18n.en;
                        container.innerHTML = `<div style="font-size: 0.78rem; color: var(--text-dim); padding: 12px; background: rgba(0,0,0,0.2); border-radius: 8px; text-align: center;"><i class="fa-solid fa-key" style="opacity: 0.5;"></i> ${dict.no_keys_found}</div>`;
                    } else {
                        let html = '';
                        res.keys.forEach(k => {
                            html += `
                                <div style="display: flex; align-items: center; justify-content: space-between; padding: 8px 12px; background: rgba(0,0,0,0.3); border-radius: 8px; margin-bottom: 6px; border: 1px solid rgba(255,255,255,0.05); gap: 10px;">
                                    <div style="flex: 1; min-width: 0;">
                                        <div style="display: flex; align-items: center; gap: 8px; margin-bottom: 2px;">
                                            <span class="badge" style="font-size: 0.68rem; padding: 1px 6px;">${k.type}</span>
                                            <span style="font-size: 0.8rem; font-weight: 600; color: #fff; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">${k.comment}</span>
                                        </div>
                                        <div style="font-size: 0.7rem; color: var(--text-dim); font-family: monospace;">${k.fingerprint}</div>
                                    </div>
                                    <button class="btn" style="padding: 4px 8px; color: #ef4444; border-color: rgba(239,68,68,0.3);" onclick="deleteSshKey(${k.line})"><i class="fa-solid fa-trash"></i></button>
                                </div>
                            `;
                        });
                        container.innerHTML = html;
                    }
                }
            } catch (err) {
                if (container) container.innerHTML = `<div style="color: #ef4444; font-size: 0.78rem;">Error: ${err.message}</div>`;
            }
        }

        async function addSshKey() {
            const input = document.getElementById('ssh-new-key-input');
            const key = input ? input.value.trim() : '';
            if (!key) {
                showToast(currentLang === 'id' ? "Silakan tempel public key terlebih dahulu" : "Please paste a public key first", 'warning');
                return;
            }
            showToast(currentLang === 'id' ? "Menambahkan public key..." : "Adding public key...", 'loading');
            try {
                const b64 = btoa(key);
                const res = await getApiData(`ssh_add_key&user=${currKeyUser}&b64=${encodeURIComponent(b64)}`);
                if (res && res.success) {
                    showToast(res.message || (currentLang === 'id' ? "Kunci berhasil ditambahkan" : "Key added successfully"), 'success', 2500);
                    if (input) input.value = '';
                    loadSshKeys();
                    fetchStatus();
                } else {
                    showToast(res && res.error ? res.error : "Failed to add key", 'error', 3500);
                }
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
            }
        }

        async function deleteSshKey(line) {
            if (!confirm(currentLang === 'id' ? `Hapus kunci pada baris ${line}?` : `Delete key at line ${line}?`)) return;
            showToast(currentLang === 'id' ? "Menghapus kunci..." : "Deleting key...", 'loading');
            try {
                const res = await getApiData(`ssh_delete_key&user=${currKeyUser}&line=${line}`);
                showToast(res && res.message ? res.message : (currentLang === 'id' ? "Kunci berhasil dihapus" : "Key deleted successfully"), 'success', 2500);
                loadSshKeys();
                fetchStatus();
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
            }
        }

        async function loadSshConfig() {
            const editor = document.getElementById('ssh-config-editor');
            if (editor) editor.value = "Loading sshd_config...";
            try {
                const res = await getApiData('ssh_get_config');
                if (editor) {
                    if (res && res.config_b64) {
                        editor.value = atob(res.config_b64);
                    } else {
                        editor.value = "Failed to load config";
                    }
                }
            } catch (err) {
                if (editor) editor.value = "Error: " + err.message;
            }
        }

        async function saveSshConfig() {
            const editor = document.getElementById('ssh-config-editor');
            if (!editor) return;
            showToast(currentLang === 'id' ? "Menyimpan sshd_config..." : "Saving sshd_config...", 'loading');
            try {
                const b64 = btoa(editor.value);
                const res = await getApiData(`ssh_save_config&b64=${encodeURIComponent(b64)}`);
                showToast(res && res.message ? res.message : (currentLang === 'id' ? "Konfigurasi SSH berhasil disimpan" : "SSH config saved successfully"), 'success', 2500);
                setTimeout(fetchStatus, 1500);
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
            }
        }

        async function loadSshLog() {
            const viewer = document.getElementById('ssh-log-viewer');
            if (viewer) viewer.innerText = "Loading sshd.log...";
            try {
                const res = await getApiData('ssh_get_log');
                if (viewer) {
                    if (res && res.log_b64) {
                        const txt = atob(res.log_b64);
                        viewer.innerText = txt || "No logs recorded yet.";
                        viewer.scrollTop = viewer.scrollHeight;
                    } else {
                        viewer.innerText = "No logs recorded yet.";
                    }
                }
            } catch (err) {
                if (viewer) viewer.innerText = "Error: " + err.message;
            }
        }

        async function clearSshLog() {
            if (!confirm(currentLang === 'id' ? "Hapus seluruh log server SSH?" : "Clear all SSH server logs?")) return;
            showToast(currentLang === 'id' ? "Membersihkan log SSH..." : "Clearing SSH logs...", 'loading');
            try {
                const res = await getApiData('ssh_clear_log');
                showToast(res && res.message ? res.message : (currentLang === 'id' ? "Log SSH berhasil dibersihkan" : "SSH log cleared"), 'success', 2000);
                loadSshLog();
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
            }
        }

        async function executeCustomCmd() {
            const input = document.getElementById('cmd-input');
            const cmd = input ? input.value.trim() : '';
            if (!cmd) return;
            await runCmd(cmd);
        }

        async function runCmd(cmd) {
            const isId = (currentLang === 'id');
            showToast(isId ? `Menjalankan: ${cmd.slice(0, 28)}...` : `Running: ${cmd.slice(0, 28)}...`, 'loading');
            if (currentView !== 'panel') switchView('panel');
            if (currentPanelTab !== 'network') switchPanelTab('network');
            const termCard = document.getElementById('card-terminal');
            if (termCard) termCard.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
            const term = document.getElementById('terminal-out');
            if (term) term.innerText = `[root@jacktor ~]# ${cmd}
Executing...`;
            
            let output = '';
            if (window.ksu && typeof window.ksu.exec === 'function') {
                try {
                    output = await window.ksu.exec(cmd);
                    showToast(isId ? 'Perintah berhasil dieksekusi' : 'Command executed successfully', 'success', 2000);
                } catch (e) {
                    output = "Error: " + e.message;
                    showToast("Error: " + e.message, 'error', 3500);
                }
            } else {
                try {
                    const res = await getApiData(`exec&cmd=${encodeURIComponent(cmd)}`);
                    output = res && res.output ? res.output : (res && res.error ? res.error : 'Command completed.');
                    showToast(isId ? 'Perintah berhasil dieksekusi' : 'Command executed successfully', 'success', 2000);
                } catch (err) {
                    output = "Error: " + err.message;
                    showToast("Error: " + err.message, 'error', 3500);
                }
            }
            if (term) term.innerText = `[root@jacktor ~]# ${cmd}
${output.trim()}`;
        }

        window.addEventListener('resize', () => {
            const trTitle = document.getElementById('track-title');
            if (trTitle) trTitle.dataset.boxW = '';
            const trArtist = document.getElementById('track-artist');
            if (trArtist) trArtist.dataset.boxW = '';
            updateMarquee(trTitle, document.getElementById('box-track-title'), 'title');
            updateMarquee(trArtist, document.getElementById('box-track-artist'), 'artist');
        });

        // ==========================================================================
        // DRAWER & MULTI-VIEW NAVIGATION CONTROLLER
        // ==========================================================================
        let currentView = 'panel';

        function toggleDrawer() {
            const drawer = document.getElementById('app-drawer');
            const backdrop = document.getElementById('drawer-backdrop');
            if (drawer && backdrop) {
                const isOpen = drawer.classList.contains('open');
                if (isOpen) {
                    closeDrawer();
                } else {
                    drawer.classList.add('open');
                    backdrop.classList.add('open');
                }
            }
        }

        function closeDrawer() {
            const drawer = document.getElementById('app-drawer');
            const backdrop = document.getElementById('drawer-backdrop');
            if (drawer && backdrop) {
                drawer.classList.remove('open');
                backdrop.classList.remove('open');
            }
        }

        function switchView(viewName) {
            currentView = viewName;
            closeDrawer();

            try {
                localStorage.setItem('jacktor_active_view', viewName);
            } catch (e) {}

            const views = ['panel', 'amplifier', 'analyzer'];
            views.forEach(v => {
                const el = document.getElementById('view-' + v);
                const navItem = document.getElementById('nav-item-' + v);

                if (v === viewName) {
                    if (el) el.classList.add('active');
                    if (navItem) navItem.classList.add('active');
                } else {
                    if (el) el.classList.remove('active');
                    if (navItem) navItem.classList.remove('active');
                }
            });

            window.location.hash = '#' + viewName;

            if (viewName === 'analyzer') {
                startAnalyzerFastLoop();
            } else {
                stopAnalyzerFastLoop();
            }
        }

        // Handle browser Back/Forward or direct hash linking
        window.addEventListener('hashchange', () => {
            const hash = window.location.hash.replace('#', '');
            if (['panel', 'amplifier', 'analyzer'].includes(hash) && hash !== currentView) {
                switchView(hash);
            }
        });

        // ==========================================================================
        // PANEL, AMPLIFIER & ANALYZER CATEGORY TABS CONTROLLERS
        // ==========================================================================
        let currentPanelTab = 'media';
        let currentAmpTab = 'power';
        let currentAnalyzerTab = 'spectrum';

        function switchPanelTab(tabName) {
            currentPanelTab = tabName;
            try {
                localStorage.setItem('jacktor_panel_tab', tabName);
            } catch (e) {}

            const tabs = ['media', 'system', 'audio', 'performance', 'usb', 'network'];
            tabs.forEach(t => {
                const btn = document.getElementById('tab-panel-' + t);
                const content = document.getElementById('panel-tab-' + t);

                if (t === tabName) {
                    if (btn) btn.classList.add('active');
                    if (content) content.classList.add('active');
                } else {
                    if (btn) btn.classList.remove('active');
                    if (content) content.classList.remove('active');
                }
            });
        }

        function switchAmpTab(tabName) {
            currentAmpTab = tabName;
            try {
                localStorage.setItem('jacktor_amp_tab', tabName);
            } catch (e) {}

            const tabs = ['power', 'dsp', 'thermal', 'diag'];
            tabs.forEach(t => {
                const btn = document.getElementById('tab-amp-' + t);
                const content = document.getElementById('amp-tab-' + t);

                if (t === tabName) {
                    if (btn) btn.classList.add('active');
                    if (content) content.classList.add('active');
                } else {
                    if (btn) btn.classList.remove('active');
                    if (content) content.classList.remove('active');
                }
            });
        }

        function switchAnalyzerTab(tabName) {
            currentAnalyzerTab = tabName;
            try {
                localStorage.setItem('jacktor_analyzer_tab', tabName);
            } catch (e) {}

            const tabs = ['spectrum', 'engine', 'tuning'];
            tabs.forEach(t => {
                const btn = document.getElementById('tab-analyzer-' + t);
                const content = document.getElementById('analyzer-tab-' + t);

                if (t === tabName) {
                    if (btn) btn.classList.add('active');
                    if (content) content.classList.add('active');
                } else {
                    if (btn) btn.classList.remove('active');
                    if (content) content.classList.remove('active');
                }
            });
        }

        function openSshCard() {
            if (currentView !== 'panel') switchView('panel');
            if (currentPanelTab !== 'network') switchPanelTab('network');
            const sshCard = document.getElementById('card-ssh');
            if (sshCard) sshCard.scrollIntoView({ behavior: 'smooth', block: 'start' });
        }

        // ==========================================================================
        // AMPLIFIER HARDWARE COMMAND & TELEMETRY CONTROLLER
        // ==========================================================================
        let lastAmpBass = false;
        let lastAmp3D = false;
        let isPowerTransitioning = false;
        let targetPowerState = null; // true: turning on, false: turning off
        let powerTransitionTimer = null;

        async function toggleAmpPower() {
            if (isPowerTransitioning) {
                return; // Prevent spam while transitioning
            }

            const currentlyOn = (lastAmpState.rt.pwr !== undefined) ? !!lastAmpState.rt.pwr : !!(lastAmpState.hz.states && lastAmpState.hz.states.on);
            const isId = (currentLang === 'id');
            const btn = document.getElementById('btn-amp-power-toggle');
            const pwrIcon = document.getElementById('amp-power-icon');
            const spinner = document.getElementById('amp-power-loading-spinner');
            const pwrStateLabel = document.getElementById('amp-power-state-label');
            const pwrDescLabel = document.getElementById('amp-power-desc-label');

            if (!currentlyOn) {
                // Standby -> Power ON (Berkedip Biru)
                const isBypass = document.getElementById('switch-amp-pwr-bypass')?.checked;
                const cmd = isBypass ? 'pwr-on --bypass' : 'pwr-on';

                isPowerTransitioning = true;
                targetPowerState = true;

                if (btn) {
                    btn.className = "amp-power-toggle-btn pwr-transitioning";
                    btn.disabled = true;
                }
                if (pwrIcon) {
                    pwrIcon.className = "fa-solid fa-power-off";
                }
                if (spinner) spinner.style.display = "inline-block";
                if (pwrStateLabel) {
                    pwrStateLabel.innerText = isBypass ? "BYPASS BOOTING..." : "SELF-TEST BOOTING...";
                    pwrStateLabel.style.color = "var(--accent-cyan)";
                }
                if (pwrDescLabel) {
                    pwrDescLabel.innerText = isId ? "Menjalankan self-test hardware & softstart SMPS..." : "Running hardware self-test & SMPS softstart...";
                }

                await sendAmpCmd(cmd);

                if (powerTransitionTimer) clearTimeout(powerTransitionTimer);
                powerTransitionTimer = setTimeout(() => {
                    if (isPowerTransitioning) {
                        isPowerTransitioning = false;
                        targetPowerState = null;
                        if (btn) btn.disabled = false;
                        if (spinner) spinner.style.display = "none";
                        updateAmpTelemetryUI({});
                    }
                }, 15000);
            } else {
                // Active -> Power OFF / Standby (Berkedip Biru)
                isPowerTransitioning = true;
                targetPowerState = false;

                if (btn) {
                    btn.className = "amp-power-toggle-btn pwr-transitioning";
                    btn.disabled = true;
                }
                if (pwrIcon) {
                    pwrIcon.className = "fa-solid fa-power-off";
                }
                if (spinner) spinner.style.display = "inline-block";
                if (pwrStateLabel) {
                    pwrStateLabel.innerText = "SHUTTING DOWN...";
                    pwrStateLabel.style.color = "var(--accent-cyan)";
                }
                if (pwrDescLabel) {
                    pwrDescLabel.innerText = isId ? "Menonaktifkan relay SMPS & speaker..." : "Disabling SMPS relay & speaker rails...";
                }

                await sendAmpCmd('pwr-off');

                if (powerTransitionTimer) clearTimeout(powerTransitionTimer);
                powerTransitionTimer = setTimeout(() => {
                    if (isPowerTransitioning) {
                        isPowerTransitioning = false;
                        targetPowerState = null;
                        if (btn) btn.disabled = false;
                        if (spinner) spinner.style.display = "none";
                        updateAmpTelemetryUI({});
                    }
                }, 10000);
            }
        }

        async function sendAmpCmd(cmd) {
            const isId = (currentLang === 'id');
            showToast(isId ? `Mengirim: ${cmd}...` : `Sending: ${cmd}...`, 'loading', 600);
            try {
                const res = await getApiData(`amp_cmd&cmd=${encodeURIComponent(cmd)}`, 2000);
                if (res && res.success) {
                    showToast(isId ? `Perintah terkirim: ${cmd}` : `Command sent: ${cmd}`, 'success', 1500);
                    if (res.response && typeof res.response === 'string') {
                        const lines = res.response.split('\n');
                        for (const line of lines) {
                            const trimmed = line.trim();
                            if (trimmed.startsWith('{') && (trimmed.includes('"telemetry"') || trimmed.includes('"analyzer"') || trimmed.includes('"hz1"') || trimmed.includes('"rt"'))) {
                                try {
                                    updateAmpTelemetryUI(JSON.parse(trimmed));
                                } catch (e) {}
                            }
                        }
                    }
                    if (!isSseConnected) {
                        setTimeout(fetchStatus, 300);
                    }
                } else {
                    showToast("Error: " + (res && res.error ? res.error : "Failed"), 'error', 3500);
                }
            } catch (err) {
                showToast("Error: " + err.message, 'error', 3500);
            }
        }

        async function sendAmpJson(obj) {
            return sendAmpCmd(JSON.stringify(obj));
        }

        function setAmpSleepTimer(min) {
            const val = (min !== undefined) ? min : (parseInt(document.getElementById('amp-sleep-min')?.value) || 0);
            sendAmpCmd(`sleep ${val}`);
            document.querySelectorAll('.btn-sleep-chip').forEach(btn => {
                btn.classList.toggle('active', parseInt(btn.dataset.min) === parseInt(val));
            });
            const badge = document.getElementById('amp-sleep-countdown-badge');
            if (badge) {
                badge.innerText = val > 0 ? `${val}m Active` : 'OFF';
                badge.className = val > 0 ? 'badge active-green' : 'badge';
            }
            showToast(val > 0 ? `Sleep timer set to ${val} minutes` : 'Sleep timer disabled (OFF)', 'success', 2000);
        }

        function setAmpBtAutoOff(min) {
            const ms = min * 60000;
            sendAmpCmd('{"type":"cmd","cmd":{"bt_autooff":' + ms + '}}');
            document.querySelectorAll('.btn-bt-chip').forEach(btn => {
                btn.classList.toggle('active', parseInt(btn.dataset.min) === parseInt(min));
            });
            const badge = document.getElementById('amp-bt-autooff-badge');
            if (badge) {
                badge.innerText = min > 0 ? `${min}m` : 'NEVER';
                badge.className = min > 0 ? 'badge active-cyan' : 'badge';
            }
            showToast(min > 0 ? `Bluetooth auto-off timer set to ${min}m` : 'Bluetooth auto-off disabled', 'success', 2000);
        }

        function setAmpSmpsCutoff() {
            const val = document.getElementById('amp-smps-cutoff-val')?.value || 24.0;
            sendAmpCmd(`smps-cutoff ${val}`);
        }

        function setAmpSmpsRecovery() {
            const val = parseFloat(document.getElementById('amp-smps-rec-val')?.value || 11.5);
            sendAmpCmd('{"type":"cmd","cmd":{"smps_rec":' + val + '}}');
            showToast(`SMPS Recovery Cutoff set to ${val}V`, 'success');
        }

        function setAmpAutoSense(en) {
            sendAmpCmd('{"type":"cmd","cmd":{"auto_sense_en":' + (en ? 'true' : 'false') + '}}');
            const btnOn = document.getElementById('btn-auto-sense-on');
            const btnOff = document.getElementById('btn-auto-sense-off');
            if (btnOn) btnOn.classList.toggle('active', en);
            if (btnOff) btnOff.classList.toggle('active', !en);
            showToast(`Signal Auto-Sense ${en ? 'Enabled' : 'Disabled'}`, 'info');
        }

        function setAmpPidSetpoint(val) {
            sendAmpCmd('{"type":"cmd","cmd":{"tune":{"pid_sp":' + parseFloat(val) + '}}}');
        }

        function setAmpOtpThreshold(val) {
            sendAmpCmd('{"type":"cmd","cmd":{"tune":{"otp_thr":' + parseFloat(val) + '}}}');
        }

        function setAmpBuzzer(en) {
            sendAmpCmd('{"type":"cmd","cmd":{"buzz_enabled":' + (en ? 'true' : 'false') + '}}');
            const btnOn = document.getElementById('btn-buzz-on');
            const btnOff = document.getElementById('btn-buzz-off');
            if (btnOn) btnOn.classList.toggle('active', en);
            if (btnOff) btnOff.classList.toggle('active', !en);
            showToast(`Piezo Buzzer ${en ? 'Enabled' : 'Muted'}`, 'info');
        }

        function setAmpBuzzerVol(val) {
            sendAmpCmd('{"type":"cmd","cmd":{"buzz_volume":' + parseInt(val) + '}}');
        }

        function setAmpBtnLock(lock) {
            sendAmpCmd('{"type":"cmd","cmd":{"btn_lock":' + (lock ? 'true' : 'false') + '}}');
            const btnOn = document.getElementById('btn-lock-on');
            const btnOff = document.getElementById('btn-lock-off');
            if (btnOn) btnOn.classList.toggle('active', lock);
            if (btnOff) btnOff.classList.toggle('active', !lock);
            showToast(`Front Panel Buttons ${lock ? 'Locked' : 'Unlocked'}`, 'info');
        }

        function syncRtcClock() {
            const now = new Date();
            // Ambil local wall-clock seconds agar modular time di ESP32 tepat sama dengan jam pengguna
            const localEpochSec = Math.floor((now.getTime() - (now.getTimezoneOffset() * 60 * 1000)) / 1000);
            sendAmpCmd('{"type":"cmd","cmd":{"rtc_set_epoch":' + localEpochSec + '}}');
            showToast(`DS3231 RTC Synchronized to ${now.toLocaleTimeString()}`, 'success');
        }

        function toggleAmpBass() {
            const btn = document.getElementById('btn-amp-bass');
            const isCurrentlyActive = btn ? btn.classList.contains('active') : false;
            const targetState = !isCurrentlyActive;
            sendAmpCmd(targetState ? 'setbass 1' : 'setbass 0');
            showToast(targetState ? 'Requesting Bass Boost ON...' : 'Requesting Bass Boost OFF...', 'info');
        }

        function toggleAmp3D() {
            const btn = document.getElementById('btn-amp-3d');
            const isCurrentlyActive = btn ? btn.classList.contains('active') : false;
            const targetState = !isCurrentlyActive;
            sendAmpCmd(targetState ? 'set3d 1' : 'set3d 0');
            showToast(targetState ? 'Requesting 3D Audio ON...' : 'Requesting 3D Audio OFF...', 'info');
        }

        // ==========================================================================
        // 1-WIRE MULTI-SENSOR TEMPERATURE MANAGER (STEREO / SUBWOOFER / SMPS)
        // ==========================================================================
        let lastTempScanSensors = [];

        function getTempRoleLabel(role) {
            const isId = (currentLang === 'id');
            const r = (role || '').toLowerCase();
            if (r === 'stereo' || r === 'class_ab' || r === 'ab') return isId ? 'Amplifier Stereo' : 'Stereo Amplifier';
            if (r === 'subwoofer' || r === 'sub' || r === 'class_d' || r === 'd') return isId ? 'Amplifier Subwoofer' : 'Subwoofer Amplifier';
            if (r === 'smps') return isId ? 'Catu Daya SMPS' : 'SMPS Power Supply';
            return isId ? 'Belum Ditugaskan' : 'Unassigned';
        }

        async function scanTempSensors() {
            const isId = (currentLang === 'id');
            const box = document.getElementById('temp-scan-results-box');
            if (box) {
                box.innerHTML = `<div style="padding: 10px; color: var(--accent-cyan); font-size: 0.78rem;"><i class="fa-solid fa-spinner fa-spin"></i> ${isId ? 'Memindai jalur 1-Wire (GPIO 27)...' : 'Scanning 1-Wire bus (GPIO 27)...'}</div>`;
            }
            showToast(isId ? 'Memindai alamat sensor suhu DS18B20...' : 'Scanning DS18B20 temperature sensors...', 'loading');
            try {
                const res = await getApiData('amp_cmd&cmd=temp-scan');
                if (res && res.response) {
                    try {
                        const lines = String(res.response).split('\n');
                        for (const line of lines) {
                            if (line.includes('"temp_scan_result"')) {
                                const parsed = JSON.parse(line.trim());
                                renderTempScanResult(parsed);
                                break;
                            }
                        }
                    } catch (e) {}
                }
            } catch (err) {
                showToast('Error: ' + err.message, 'error');
            }
        }

        function renderTempScanResult(packet) {
            const isId = (currentLang === 'id');
            const box = document.getElementById('temp-scan-results-box');
            const sensors = (packet && Array.isArray(packet.devices)) ? packet.devices : ((packet && Array.isArray(packet.sensors)) ? packet.sensors : []);
            lastTempScanSensors = sensors;

            if (packet && packet.assigned) {
                const elRomAb = document.getElementById('temp-rom-stereo');
                if (elRomAb && packet.assigned.class_ab) {
                    elRomAb.innerText = packet.assigned.class_ab !== "0000000000000000" ? `0x${packet.assigned.class_ab}` : (isId ? "Belum Diatur" : "Unassigned");
                }
                const elRomD = document.getElementById('temp-rom-sub');
                if (elRomD && packet.assigned.class_d) {
                    elRomD.innerText = packet.assigned.class_d !== "0000000000000000" ? `0x${packet.assigned.class_d}` : (isId ? "Belum Diatur" : "Unassigned");
                }
                const elRomSmps = document.getElementById('temp-rom-smps');
                if (elRomSmps && packet.assigned.smps) {
                    elRomSmps.innerText = packet.assigned.smps !== "0000000000000000" ? `0x${packet.assigned.smps}` : (isId ? "Belum Diatur" : "Unassigned");
                }
            }

            if (!box) return;
            if (sensors.length === 0) {
                box.innerHTML = `<div style="padding: 10px; color: #f59e0b; font-size: 0.78rem;"><i class="fa-solid fa-triangle-exclamation"></i> ${isId ? 'Tidak ada sensor DS18B20 yang terdeteksi pada GPIO 27.' : 'No DS18B20 sensors detected on GPIO 27.'}</div>`;
                showToast(isId ? 'Tidak ada sensor suhu terdeteksi' : 'No temperature sensors found', 'warning');
                return;
            }

            let html = `<div style="display: flex; flex-direction: column; gap: 8px; margin-top: 6px;">`;
            sensors.forEach((s, idx) => {
                const romHex = s.rom || '0000000000000000';
                const tempStr = (s.temp !== null && s.temp !== undefined) ? `${parseFloat(s.temp).toFixed(1)} °C` : '-- °C';
                const roleLabel = getTempRoleLabel(s.role);
                const isStereo = (s.role === 'class_ab' || s.role === 'stereo');
                const isSub = (s.role === 'class_d' || s.role === 'subwoofer');
                const isSmps = (s.role === 'smps');

                html += `
                <div style="background: rgba(0, 0, 0, 0.32); border: 1px solid rgba(56, 189, 248, 0.2); border-radius: 10px; padding: 10px 12px; display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 10px;">
                    <div style="display: flex; flex-direction: column; gap: 2px;">
                        <div style="display: flex; align-items: center; gap: 8px;">
                            <span class="badge active" style="font-size: 0.68rem;">#${idx + 1}</span>
                            <code style="color: var(--accent-cyan); font-weight: 700; font-size: 0.8rem;">0x${romHex}</code>
                            <span style="color: var(--accent-green); font-weight: 700; font-family: 'JetBrains Mono', monospace; font-size: 0.8rem;">${tempStr}</span>
                        </div>
                        <div style="font-size: 0.72rem; color: var(--text-muted);">
                            ${isId ? 'Fungsi Saat Ini:' : 'Current Role:'} <strong style="color: #fff;">${roleLabel}</strong>
                        </div>
                    </div>
                    <div style="display: flex; align-items: center; gap: 6px; flex-wrap: wrap;">
                        <button class="btn ${isStereo ? 'active' : ''}" style="padding: 4px 9px; font-size: 0.72rem;" onclick="assignTempSensor('stereo', '${romHex}')">
                            <i class="fa-solid fa-wave-square"></i> Stereo
                        </button>
                        <button class="btn ${isSub ? 'active' : ''}" style="padding: 4px 9px; font-size: 0.72rem;" onclick="assignTempSensor('subwoofer', '${romHex}')">
                            <i class="fa-solid fa-drum"></i> Subwoofer
                        </button>
                        <button class="btn ${isSmps ? 'active' : ''}" style="padding: 4px 9px; font-size: 0.72rem;" onclick="assignTempSensor('smps', '${romHex}')">
                            <i class="fa-solid fa-bolt"></i> SMPS
                        </button>
                    </div>
                </div>`;
            });
            html += `</div>`;
            box.innerHTML = html;
            showToast(isId ? `Ditemukan ${sensors.length} sensor suhu DS18B20` : `Found ${sensors.length} DS18B20 sensor(s)`, 'success', 2000);
        }

        async function assignTempSensor(role, romHex) {
            const isId = (currentLang === 'id');
            const cleanRom = String(romHex || '').replace(/^0x/i, '').trim();
            if (!cleanRom) return;
            const label = getTempRoleLabel(role);
            showToast(isId ? `Menetapkan 0x${cleanRom} sebagai sensor ${label}...` : `Assigning 0x${cleanRom} to ${label}...`, 'loading');
            await sendAmpCmd(`temp-assign ${role} ${cleanRom}`);
            setTimeout(scanTempSensors, 450);
        }

        async function swapTempSensors(role1, role2) {
            const isId = (currentLang === 'id');
            const l1 = getTempRoleLabel(role1);
            const l2 = getTempRoleLabel(role2);
            showToast(isId ? `Menukar fungsi sensor ${l1} ↔ ${l2}...` : `Swapping sensor roles ${l1} ↔ ${l2}...`, 'loading');
            await sendAmpCmd(`temp-swap ${role1} ${role2}`);
            setTimeout(scanTempSensors, 450);
        }

        // ==========================================================================
        // AUDIO ANALYZER & KISSFFT VISUALIZER (Ported from cmd.py Algorithm)
        // ==========================================================================

        // 64-band FFT cutoffs matching cmd.py / firmware analyzer.cpp
        const FREQ_BANDS_64 = [
            30, 50, 100, 150, 250, 400, 650, 1000, 1600, 2500, 4000, 6000, 12000, 14000, 16000, 17000,
            35, 75, 125, 200, 300, 500, 800, 1250, 2000, 3000, 5000, 8000, 10000, 13000, 15000, 18000,
            40, 60, 110, 175, 275, 450, 700, 1100, 1800, 2750, 4500, 7000, 11000, 13500, 15500, 17500,
            45, 55, 115, 160, 225, 350, 550, 850, 1300, 2000, 3200, 5000, 9000, 11500, 14500, 16500
        ];

        // Visual FX Modes (matching cmd.py ana_fx_modes)
        const ANA_FX_MODES = ["GRADIENT", "FIRE", "NEON", "MIRROR", "DOTS"];
        let anaFxIdx = 0; // Current FX mode index

        // Decay Presets (matching cmd.py ana_decay_presets)
        const ANA_DECAY_PRESETS = [
            { name: "FAST", rate: 0.62 },
            { name: "MED", rate: 0.82 },
            { name: "SLOW", rate: 0.92 },
            { name: "LIQUID", rate: 0.96 }
        ];
        let anaDecayIdx = 1; // Default: MED (0.82)
        let anaDecayRate = 0.82;
        let anaTrailRate = 0.91; // min(0.975, decay_rate + 0.09)
        let anaPeakHoldEn = true;

        // Physics State Arrays (64 bands) - matching cmd.py
        const anaDecayBandsL = new Float32Array(64).fill(0);
        const anaDecayBandsR = new Float32Array(64).fill(0);
        const anaTrailBandsL = new Float32Array(64).fill(0);
        const anaTrailBandsR = new Float32Array(64).fill(0);
        const anaPeakHoldL = new Float32Array(64).fill(0);
        const anaPeakHoldR = new Float32Array(64).fill(0);
        const anaPeakVelL = new Float32Array(64).fill(0);
        const anaPeakVelR = new Float32Array(64).fill(0);
        const anaPeakTimerL = new Int32Array(64).fill(0);
        const anaPeakTimerR = new Int32Array(64).fill(0);

        // VU Meter Physics
        let anaDecayVuL = 0, anaDecayVuR = 0;
        let anaVuPeakL = 0, anaVuPeakR = 0;
        let anaVuPeakTimerL = 0, anaVuPeakTimerR = 0;

        // Band Peak Tracking (for Last Peak Frequency)
        const bandPeaksL = new Int32Array(64).fill(0);
        const bandPeaksR = new Int32Array(64).fill(0);
        let maxPeakValL = 0, maxPeakValR = 0;
        let lastPeakFreqL = "N/A", lastPeakFreqR = "N/A";

        // VU Meter Elements (for smooth rendering)
        let vuBarL = null, vuBarR = null, vuValL = null, vuValR = null;
        let balanceDot = null;
        let statsL = null, statsR = null;
        let clipBadge = null;
        let runtimeBadge = null;
        let fpsBadge = null;
        let leftStats = null, rightStats = null;

        function initSpectrumBars() {
            const leftBox = document.getElementById('spectrum-left-box');
            const rightBox = document.getElementById('spectrum-right-box');
            if (!leftBox || !rightBox) return;

            // Cache VU elements
            vuBarL = document.getElementById('vu-meter-bar-l');
            vuBarR = document.getElementById('vu-meter-bar-r');
            vuValL = document.getElementById('vu-val-l');
            vuValR = document.getElementById('vu-val-r');
            balanceDot = document.getElementById('analyzer-balance-dot');
            statsL = document.getElementById('analyzer-l-stats');
            statsR = document.getElementById('analyzer-r-stats');
            clipBadge = document.getElementById('analyzer-clip-badge');
            runtimeBadge = document.getElementById('analyzer-runtime-badge');
            fpsBadge = document.getElementById('analyzer-fps-badge');
            leftStats = document.getElementById('analyzer-l-stats');
            rightStats = document.getElementById('analyzer-r-stats');

            if (leftBox.children.length === 0) {
                let leftHtml = '';
                for (let i = 0; i < 64; i++) {
                    const freqLabel = formatFreqLabel(FREQ_BANDS_64[i]);
                    leftHtml += `
                    <div class="spectrum-band-col" id="spec-col-l-${i}" style="flex: 1;">
                        <div class="spectrum-bar-stage">
                            <div class="spectrum-peak-dot" id="spec-peak-l-${i}" style="bottom: 0%;"></div>
                            <div class="spectrum-bar-fill left" id="spec-bar-l-${i}" style="height: 0%;"></div>
                        </div>
                        <div class="spectrum-label-slot">
                            <span class="spectrum-label">${freqLabel}</span>
                        </div>
                    </div>`;
                }
                leftBox.innerHTML = leftHtml;
            }

            if (rightBox.children.length === 0) {
                let rightHtml = '';
                for (let i = 0; i < 64; i++) {
                    const freqLabel = formatFreqLabel(FREQ_BANDS_64[i]);
                    rightHtml += `
                    <div class="spectrum-band-col" id="spec-col-r-${i}" style="flex: 1;">
                        <div class="spectrum-bar-stage">
                            <div class="spectrum-peak-dot" id="spec-peak-r-${i}" style="bottom: 0%;"></div>
                            <div class="spectrum-bar-fill right" id="spec-bar-r-${i}" style="height: 0%;"></div>
                        </div>
                        <div class="spectrum-label-slot">
                            <span class="spectrum-label">${freqLabel}</span>
                        </div>
                    </div>`;
                }
                rightBox.innerHTML = rightHtml;
                updateVisibleSpectrumBands(activeBandsCount);
            }
        }

        function formatFreqLabel(hz) {
            if (hz >= 1000) {
                const khz = hz / 1000;
                return khz === Math.floor(khz) ? `${khz}k` : `${khz.toFixed(1)}k`;
            }
            return `${hz}`;
        }

        function decodeBase64Bands(b64) {
            if (!b64) return [];
            try {
                const bin = window.atob(b64);
                const arr = new Uint8Array(bin.length);
                for (let i = 0; i < bin.length; i++) {
                    arr[i] = bin.charCodeAt(i);
                }
                return arr;
            } catch (e) {
                return [];
            }
        }

        // Active FFT Band Count (8, 16, 32, 64)
        let activeBandsCount = 16;

        function setAnalyzerBandsCount(val) {
            const count = parseInt(val, 10) || 16;
            activeBandsCount = count;
            sendAmpCmd('ana-bands ' + count);
            updateVisibleSpectrumBands(count);
        }

        function updateVisibleSpectrumBands(count) {
            const c = parseInt(count, 10) || 16;
            activeBandsCount = c;
            for (let i = 0; i < 64; i++) {
                const colL = document.getElementById(`spec-col-l-${i}`);
                const colR = document.getElementById(`spec-col-r-${i}`);
                const disp = i < c ? '' : 'none';
                if (colL) colL.style.display = disp;
                if (colR) colR.style.display = disp;
            }
            const engBadge = document.getElementById('analyzer-engine-badge');
            if (engBadge) engBadge.innerText = `KissFFT ${c}`;
            const sel = document.getElementById('analyzer-bands-select');
            if (sel && sel.value !== String(c)) sel.value = String(c);
        }

        function setDecayPreset(idx) {
            if (idx < 0 || idx >= ANA_DECAY_PRESETS.length) return;
            anaDecayIdx = idx;
            anaDecayRate = ANA_DECAY_PRESETS[idx].rate;
            anaTrailRate = Math.min(0.975, anaDecayRate + 0.09);
            for (let i = 0; i < ANA_DECAY_PRESETS.length; i++) {
                const btn = document.getElementById(`btn-decay-${i}`);
                if (btn) btn.classList.toggle('active', i === idx);
            }
        }

        function cycleFxMode() {
            anaFxIdx = (anaFxIdx + 1) % ANA_FX_MODES.length;
            const lbl = document.getElementById('ana-fx-label');
            if (lbl) lbl.innerText = ANA_FX_MODES[anaFxIdx];
        }

        function togglePeakHold() {
            anaPeakHoldEn = !anaPeakHoldEn;
            const btn = document.getElementById('btn-ana-peak-hold');
            const lbl = document.getElementById('ana-peak-label');
            if (btn) btn.classList.toggle('active', anaPeakHoldEn);
            if (lbl) lbl.innerText = anaPeakHoldEn ? 'ON' : 'OFF';
        }

        function resetPeakTracking() {
            for (let i = 0; i < 64; i++) {
                bandPeaksL[i] = 0;
                bandPeaksR[i] = 0;
                anaPeakHoldL[i] = 0;
                anaPeakHoldR[i] = 0;
                anaPeakVelL[i] = 0;
                anaPeakVelR[i] = 0;
                anaPeakTimerL[i] = 0;
                anaPeakTimerR[i] = 0;
            }
            maxPeakValL = 0;
            maxPeakValR = 0;
            lastPeakFreqL = "N/A";
            lastPeakFreqR = "N/A";
        }

        function updateAnalyzerPhysics(rt) {
            if (!rt) return;

            const isPowerOn = !!rt.pwr;
            const gateL = !!rt.gate_l;
            const gateR = !!rt.gate_r;

            const decRateL = gateL ? anaDecayRate : 0.40;
            const trailRateL = gateL ? anaTrailRate : 0.40;
            const decRateR = gateR ? anaDecayRate : 0.40;
            const trailRateR = gateR ? anaTrailRate : 0.40;

            // --- VU Meter Physics ---
            const rawVuL = rt.vu_l !== undefined ? rt.vu_l : 0;
            const rawVuR = rt.vu_r !== undefined ? rt.vu_r : 0;

            if (!gateL && rawVuL === 0) {
                anaDecayVuL = 0;
                anaVuPeakL = 0;
                anaVuPeakTimerL = 0;
            } else {
                anaDecayVuL = Math.max(rawVuL, anaDecayVuL * decRateL);
                if (anaDecayVuL < 5) anaDecayVuL = 0;
            }

            if (!gateR && rawVuR === 0) {
                anaDecayVuR = 0;
                anaVuPeakR = 0;
                anaVuPeakTimerR = 0;
            } else {
                anaDecayVuR = Math.max(rawVuR, anaDecayVuR * decRateR);
                if (anaDecayVuR < 5) anaDecayVuR = 0;
            }

            // VU Gravity Peak-Hold
            if (anaDecayVuL >= anaVuPeakL) {
                anaVuPeakL = anaDecayVuL;
                anaVuPeakTimerL = 12;
            } else if (anaVuPeakTimerL > 0) {
                anaVuPeakTimerL--;
            } else {
                anaVuPeakL = Math.max(0, anaVuPeakL - 6.5);
            }

            if (anaDecayVuR >= anaVuPeakR) {
                anaVuPeakR = anaDecayVuR;
                anaVuPeakTimerR = 12;
            } else if (anaVuPeakTimerR > 0) {
                anaVuPeakTimerR--;
            } else {
                anaVuPeakR = Math.max(0, anaVuPeakR - 6.5);
            }

            // --- Spectrum Bands Physics (64 bands) ---
            const bandsL = rt.bands_l_b64 ? decodeBase64Bands(rt.bands_l_b64) : [];
            const bandsR = rt.bands_r_b64 ? decodeBase64Bands(rt.bands_r_b64) : [];

            // Extend to 64 bands if firmware only sends 16
            const bandsL64 = new Uint8Array(64);
            const bandsR64 = new Uint8Array(64);
            for (let i = 0; i < 16; i++) {
                if (i < bandsL.length) bandsL64[i] = bandsL[i];
                if (i < bandsR.length) bandsR64[i] = bandsR[i];
            }
            // Mirror/interpolate for higher bands (simplified)
            for (let i = 16; i < 64; i++) {
                const srcIdx = Math.floor(i * 16 / 64);
                bandsL64[i] = bandsL64[srcIdx] * 0.7;
                bandsR64[i] = bandsR64[srcIdx] * 0.7;
            }

            for (let i = 0; i < 64; i++) {
                const rawL = bandsL64[i] || 0;
                const rawR = bandsR64[i] || 0;

                // Left Channel Decay & Trail
                if (!gateL && rawL === 0) {
                    anaDecayBandsL[i] = 0;
                    anaTrailBandsL[i] = 0;
                } else {
                    anaDecayBandsL[i] = Math.max(rawL, anaDecayBandsL[i] * decRateL);
                    if (anaDecayBandsL[i] < 1.5) anaDecayBandsL[i] = 0;
                    anaTrailBandsL[i] = Math.max(anaDecayBandsL[i], anaTrailBandsL[i] * trailRateL);
                    if (anaTrailBandsL[i] < 1.5) anaTrailBandsL[i] = 0;
                }

                // Right Channel Decay & Trail
                if (!gateR && rawR === 0) {
                    anaDecayBandsR[i] = 0;
                    anaTrailBandsR[i] = 0;
                } else {
                    anaDecayBandsR[i] = Math.max(rawR, anaDecayBandsR[i] * decRateR);
                    if (anaDecayBandsR[i] < 1.5) anaDecayBandsR[i] = 0;
                    anaTrailBandsR[i] = Math.max(anaDecayBandsR[i], anaTrailBandsR[i] * trailRateR);
                    if (anaTrailBandsR[i] < 1.5) anaTrailBandsR[i] = 0;
                }

                // Gravity Peak-Hold Physics
                if (anaDecayBandsL[i] >= anaPeakHoldL[i]) {
                    anaPeakHoldL[i] = anaDecayBandsL[i];
                    anaPeakTimerL[i] = 14;
                    anaPeakVelL[i] = 0;
                } else if (anaPeakTimerL[i] > 0) {
                    anaPeakTimerL[i]--;
                } else {
                    anaPeakVelL[i] = Math.min(18, anaPeakVelL[i] + 1.4);
                    anaPeakHoldL[i] = Math.max(0, anaPeakHoldL[i] - anaPeakVelL[i]);
                }

                if (anaDecayBandsR[i] >= anaPeakHoldR[i]) {
                    anaPeakHoldR[i] = anaDecayBandsR[i];
                    anaPeakTimerR[i] = 14;
                    anaPeakVelR[i] = 0;
                } else if (anaPeakTimerR[i] > 0) {
                    anaPeakTimerR[i]--;
                } else {
                    anaPeakVelR[i] = Math.min(18, anaPeakVelR[i] + 1.4);
                    anaPeakHoldR[i] = Math.max(0, anaPeakHoldR[i] - anaPeakVelR[i]);
                }

                // Track band peaks for Last Peak Frequency
                const intRawL = Math.floor(rawL);
                const intRawR = Math.floor(rawR);
                if (intRawL > bandPeaksL[i]) bandPeaksL[i] = intRawL;
                if (intRawR > bandPeaksR[i]) bandPeaksR[i] = intRawR;
            }

            // Update Last Peak Frequency
            if (bandsL64.length > 0) {
                const curMaxL = Math.max(...bandsL64);
                if (curMaxL >= maxPeakValL && curMaxL > 15) {
                    maxPeakValL = curMaxL;
                    const peakIdxL = bandsL64.indexOf(curMaxL);
                    const freqL = peakIdxL < FREQ_BANDS_64.length ? FREQ_BANDS_64[peakIdxL] : 0;
                    lastPeakFreqL = `${freqL} Hz (Band B${peakIdxL + 1})`;
                }
            }
            if (bandsR64.length > 0) {
                const curMaxR = Math.max(...bandsR64);
                if (curMaxR >= maxPeakValR && curMaxR > 15) {
                    maxPeakValR = curMaxR;
                    const peakIdxR = bandsR64.indexOf(curMaxR);
                    const freqR = peakIdxR < FREQ_BANDS_64.length ? FREQ_BANDS_64[peakIdxR] : 0;
                    lastPeakFreqR = `${freqR} Hz (Band B${peakIdxR + 1})`;
                }
            }
        }

        function renderSpectrumBars() {
            const fxMode = ANA_FX_MODES[anaFxIdx];

            for (let i = 0; i < 64; i++) {
                // Convert 0-255 values to 0-100% for display
                const valL = Math.min(100, Math.round((anaDecayBandsL[i] / 255) * 100));
                const valR = Math.min(100, Math.round((anaDecayBandsR[i] / 255) * 100));
                const trailL = Math.min(100, Math.round((anaTrailBandsL[i] / 255) * 100));
                const trailR = Math.min(100, Math.round((anaTrailBandsR[i] / 255) * 100));
                const peakL = Math.min(100, Math.round((anaPeakHoldL[i] / 255) * 100));
                const peakR = Math.min(100, Math.round((anaPeakHoldR[i] / 255) * 100));

                // Render Left Bar
                const barL = document.getElementById(`spec-bar-l-${i}`);
                const dotL = document.getElementById(`spec-peak-l-${i}`);
                if (barL) {
                    barL.style.height = valL + '%';
                    // Apply FX mode styling
                    applyFxStyle(barL, valL, trailL, fxMode, 'left');
                }
                if (dotL && anaPeakHoldEn && peakL > 5) {
                    dotL.style.bottom = peakL + '%';
                } else if (dotL) {
                    dotL.style.bottom = '0%';
                }

                // Render Right Bar
                const barR = document.getElementById(`spec-bar-r-${i}`);
                const dotR = document.getElementById(`spec-peak-r-${i}`);
                if (barR) {
                    barR.style.height = valR + '%';
                    applyFxStyle(barR, valR, trailR, fxMode, 'right');
                }
                if (dotR && anaPeakHoldEn && peakR > 5) {
                    dotR.style.bottom = peakR + '%';
                } else if (dotR) {
                    dotR.style.bottom = '0%';
                }
            }
        }

        function applyFxStyle(bar, val, trail, fxMode, channel) {
            // Color zones: Green (0-60%), Yellow (60-85%), Red (85-100%)
            const zone = val > 85 ? 'peak' : (val > 60 ? 'warn' : 'norm');
            bar.className = `spectrum-bar-fill ${channel} fx-${fxMode.toLowerCase()} zone-${zone}`;

            // Trail effect via opacity/transform
            if (trail > val) {
                bar.style.setProperty('--trail-height', trail + '%');
            }
        }

        function renderVuMeters() {
            const vuL = Math.round(anaDecayVuL);
            const vuR = Math.round(anaDecayVuR);
            const vuPeakL = Math.round(anaVuPeakL);
            const vuPeakR = Math.round(anaVuPeakR);

            const pctL = Math.min(100, Math.round((vuL / 255) * 100));
            const pctR = Math.min(100, Math.round((vuR / 255) * 100));
            const peakPctL = Math.min(100, Math.round((vuPeakL / 255) * 100));
            const peakPctR = Math.min(100, Math.round((vuPeakR / 255) * 100));

            if (vuBarL) vuBarL.style.width = pctL + '%';
            if (vuBarR) vuBarR.style.width = pctR + '%';
            if (vuValL) vuValL.innerText = pctL + '%';
            if (vuValR) vuValR.innerText = pctR + '%';

            // Peak markers on VU
            if (vuBarL) vuBarL.style.setProperty('--peak-pos', peakPctL + '%');
            if (vuBarR) vuBarR.style.setProperty('--peak-pos', peakPctR + '%');

            // Stereo Balance Dot
            if (balanceDot) {
                const total = vuL + vuR;
                if (total > 8) {
                    const diff = (vuR - vuL) / total;
                    const pos = 50 + (diff * 45);
                    balanceDot.style.left = Math.max(5, Math.min(95, pos)) + '%';
                } else {
                    balanceDot.style.left = '50%';
                }
            }
        }

        function resetSpectrumBars() {
            for (let i = 0; i < 64; i++) {
                anaDecayBandsL[i] = 0;
                anaDecayBandsR[i] = 0;
                anaTrailBandsL[i] = 0;
                anaTrailBandsR[i] = 0;
                anaPeakHoldL[i] = 0;
                anaPeakHoldR[i] = 0;
                anaPeakVelL[i] = 0;
                anaPeakVelR[i] = 0;
                anaPeakTimerL[i] = 0;
                anaPeakTimerR[i] = 0;
                bandPeaksL[i] = 0;
                bandPeaksR[i] = 0;
                const barL = document.getElementById(`spec-bar-l-${i}`);
                const dotL = document.getElementById(`spec-peak-l-${i}`);
                const barR = document.getElementById(`spec-bar-r-${i}`);
                const dotR = document.getElementById(`spec-peak-r-${i}`);
                if (barL) barL.style.height = '0%';
                if (dotL) dotL.style.bottom = '0%';
                if (barR) barR.style.height = '0%';
                if (dotR) dotR.style.bottom = '0%';
            }
            anaDecayVuL = anaDecayVuR = 0;
            anaVuPeakL = anaVuPeakR = 0;
            anaVuPeakTimerL = anaVuPeakTimerR = 0;
            maxPeakValL = maxPeakValR = 0;
            lastPeakFreqL = lastPeakFreqR = "N/A";

            if (vuBarL) vuBarL.style.width = '0%';
            if (vuBarR) vuBarR.style.width = '0%';
            if (vuValL) vuValL.innerText = '0%';
            if (vuValR) vuValR.innerText = '0%';
            if (balanceDot) balanceDot.style.left = '50%';
            if (clipBadge) clipBadge.style.display = 'none';
        }

        // ==========================================================================
        // DS3231 HARDWARE PHASE-LOCKED REAL-TIME CLOCK TICKER
        // ==========================================================================
        let rtcAnchorSecOfDay = -1;
        let rtcAnchorLocalMs = 0;
        let rtcTickerTimer = null;

        function syncRtcAnchorFromTimeStr(rawTime) {
            if (!rawTime || typeof rawTime !== 'string') return;
            const timePart = rawTime.includes('T') ? rawTime.split('T')[1].replace('Z', '').substring(0, 8) : rawTime;
            const parts = timePart.split(':');
            if (parts.length === 3) {
                const h = parseInt(parts[0], 10);
                const m = parseInt(parts[1], 10);
                const s = parseInt(parts[2], 10);
                if (!isNaN(h) && !isNaN(m) && !isNaN(s)) {
                    rtcAnchorSecOfDay = h * 3600 + m * 60 + s;
                    rtcAnchorLocalMs = performance.now();
                    updateRtcClockDisplay();
                }
            }
        }

        function updateRtcClockDisplay() {
            const el = document.getElementById('amp-rtc-time-val');
            if (!el || rtcAnchorSecOfDay < 0) return;
            const elapsedSec = Math.floor((performance.now() - rtcAnchorLocalMs) / 1000);
            const curSecOfDay = (rtcAnchorSecOfDay + elapsedSec) % 86400;
            const h = String(Math.floor(curSecOfDay / 3600)).padStart(2, '0');
            const m = String(Math.floor((curSecOfDay % 3600) / 60)).padStart(2, '0');
            const s = String(curSecOfDay % 60).padStart(2, '0');
            el.innerText = `${h}:${m}:${s}`;
        }

        if (!rtcTickerTimer) {
            rtcTickerTimer = setInterval(updateRtcClockDisplay, 250);
        }

        let lastAmpState = {
            hz: {},
            rt: {}
        };

        function updateAmpTelemetryUI(tel) {
            if (!tel) return;
            try {
                if (typeof tel === 'string') {
                    tel = JSON.parse(tel);
                }
            } catch (e) {
                console.error("Failed to parse amp_telemetry JSON string:", e);
                return;
            }
            if (!tel || typeof tel !== 'object') return;

            try {
                let incomingHz = tel.hz1 || (tel.telemetry && tel.telemetry.hz1) || (tel.type === 'telemetry' && tel.hz1) || null;
                let incomingRt = tel.rt || (tel.telemetry && tel.telemetry.rt) || (tel.type === 'telemetry' && tel.rt) || null;

                if (tel.type === 'analyzer' && tel.data) {
                    incomingRt = Object.assign({}, incomingRt || {}, tel.data);
                }

                if (!incomingHz && !incomingRt) {
                    if (tel.fw_ver || tel.v_vdd !== undefined || tel.time || tel.sqw || tel.sleep_timer !== undefined) incomingHz = tel;
                    if (tel.vu !== undefined || tel.mag_l !== undefined || tel.dc_l !== undefined) incomingRt = tel;
                }

                if (incomingHz) Object.assign(lastAmpState.hz, incomingHz);
                if (incomingRt) Object.assign(lastAmpState.rt, incomingRt);

                const hz = lastAmpState.hz;
                const rt = lastAmpState.rt;
                const isId = (currentLang === 'id');

                // 1. Power State & Hero Badges
                const isPowerOn = (rt.pwr !== undefined) ? !!rt.pwr : !!(hz.states && hz.states.on);
                const isSpkOn = (rt.spk_pwr !== undefined) ? !!rt.spk_pwr : !!(hz.nvs && hz.nvs.spk_pwr);
                const isBtOn = (rt.bt_en !== undefined) ? !!rt.bt_en : (hz.inputs ? !!hz.inputs.bt : !!(hz.nvs && hz.nvs.bt_en));

                // Check transition resolution from telemetry
                if (isPowerTransitioning) {
                    if (targetPowerState === true && isPowerOn) {
                        // Power ON reached successfully (Self-test PASS)
                        isPowerTransitioning = false;
                        targetPowerState = null;
                        if (powerTransitionTimer) clearTimeout(powerTransitionTimer);
                    } else if (targetPowerState === false && !isPowerOn) {
                        // Power OFF / Standby reached successfully
                        isPowerTransitioning = false;
                        targetPowerState = null;
                        if (powerTransitionTimer) clearTimeout(powerTransitionTimer);
                    }
                }

                const pwrBadge = document.getElementById('amp-hero-status-badge');
                const btnPwrToggle = document.getElementById('btn-amp-power-toggle');
                const pwrIcon = document.getElementById('amp-power-icon');
                const pwrSpinner = document.getElementById('amp-power-loading-spinner');
                const pwrStateLabel = document.getElementById('amp-power-state-label');
                const pwrDescLabel = document.getElementById('amp-power-desc-label');
                const drawerAmpStatus = document.getElementById('drawer-amp-status-text');

                if (isPowerTransitioning) {
                    // Berkedip biru & disabled saat proses transisi berlangsung
                    if (btnPwrToggle) {
                        btnPwrToggle.className = "amp-power-toggle-btn pwr-transitioning";
                        btnPwrToggle.disabled = true;
                    }
                    if (pwrSpinner) pwrSpinner.style.display = "inline-block";
                    if (pwrBadge) {
                        pwrBadge.className = "badge";
                        pwrBadge.innerText = targetPowerState ? "BOOTING" : "STOPPING";
                    }
                } else {
                    if (btnPwrToggle) btnPwrToggle.disabled = false;
                    if (pwrSpinner) pwrSpinner.style.display = "none";

                    if (isPowerOn) {
                        // Status PASS: Tombol Hijau
                        if (btnPwrToggle) btnPwrToggle.className = "amp-power-toggle-btn pwr-active";
                        if (pwrIcon) {
                            pwrIcon.className = "fa-solid fa-power-off";
                            pwrIcon.style.color = "#10b981";
                        }
                        if (pwrStateLabel) {
                            pwrStateLabel.innerText = "ONLINE (PASS)";
                            pwrStateLabel.style.color = "var(--accent-green)";
                        }
                        if (pwrDescLabel) {
                            pwrDescLabel.innerText = isId ? "PASS • Relay Aktif • SMPS Rails Beroperasi" : "PASS • Relays Active • SMPS Rails Operating";
                        }
                        if (pwrBadge) {
                            pwrBadge.className = "badge active-green";
                            pwrBadge.innerText = "PASS";
                        }
                        if (drawerAmpStatus) drawerAmpStatus.innerText = "Amp: Online (PASS)";
                    } else {
                        // Standby: Tombol Merah
                        if (btnPwrToggle) btnPwrToggle.className = "amp-power-toggle-btn pwr-standby";
                        if (pwrIcon) {
                            pwrIcon.className = "fa-solid fa-power-off";
                            pwrIcon.style.color = "#ef4444";
                        }
                        if (pwrStateLabel) {
                            pwrStateLabel.innerText = "STANDBY";
                            pwrStateLabel.style.color = "var(--text-dim)";
                        }
                        if (pwrDescLabel) {
                            pwrDescLabel.innerText = isId ? "Jalur SMPS Mati • Proteksi Siaga" : "SMPS Rail Idle • Protections Active";
                        }
                        if (pwrBadge) {
                            pwrBadge.className = "badge";
                            pwrBadge.innerText = "STANDBY";
                        }
                        if (drawerAmpStatus) drawerAmpStatus.innerText = "Amp: Standby";
                    }
                }

                // Relays Active States & Header Badge
                const btnSpkOn = document.getElementById('btn-spk-on');
                const btnSpkOff = document.getElementById('btn-spk-off');
                if (btnSpkOn) btnSpkOn.classList.toggle('active', isSpkOn);
                if (btnSpkOff) btnSpkOff.classList.toggle('active', !isSpkOn);

                const relaysBadge = document.getElementById('amp-relays-badge');
                if (relaysBadge) {
                    if (!isPowerOn) {
                        relaysBadge.className = "badge";
                        relaysBadge.innerText = "STANDBY";
                    } else if (rt.spk_fault) {
                        relaysBadge.className = "badge active-red";
                        relaysBadge.innerText = isId ? "PROTEKSI TRIP" : "PROTECTION TRIP";
                    } else if (!isSpkOn) {
                        relaysBadge.className = "badge active-warn";
                        relaysBadge.innerText = isId ? "CUT-OFF (MUTED)" : "CUT-OFF (MUTED)";
                    } else {
                        relaysBadge.className = "badge active-green";
                        relaysBadge.innerText = isId ? "PROTECTED (ACTIVE)" : "PROTECTED (ACTIVE)";
                    }
                }

                const btnBtOn = document.getElementById('btn-bt-on');
                const btnBtOff = document.getElementById('btn-bt-off');
                if (btnBtOn) btnBtOn.classList.toggle('active', isBtOn);
                if (btnBtOff) btnBtOff.classList.toggle('active', !isBtOn);

                // Hardware Bluetooth & QCC Telemetry:
                // Evaluated strictly from QCC Power (GPIO 5) and SPP connection state
                const btData = hz.bt || {};
                const qccPwr = (rt.qcc_pwr !== undefined) ? rt.qcc_pwr : (btData.qcc_pwr !== undefined ? btData.qcc_pwr : false);
                const sppConnected = (rt.spp_connected !== undefined) ? rt.spp_connected : (btData.spp_connected !== undefined ? btData.spp_connected : false);
                const isPowerInjected = (!isPowerOn && qccPwr);
                const btRssi = (rt.bt_rssi !== undefined && rt.bt_rssi !== 0) ? rt.bt_rssi : (btData.rssi !== undefined ? btData.rssi : 0);
                const sppState = btData.spp_state || (sppConnected ? "connected" : "idle");
                const activeCodec = btData.codec || "--";
                const espTxPower = (btData.esp_tx_power && btData.esp_tx_power !== "--" && btData.esp_tx_power !== "None") ? btData.esp_tx_power : "--";

                const ampBtBadge = document.getElementById('amp-bt-badge');
                if (ampBtBadge) {
                    if (!qccPwr) {
                        ampBtBadge.innerText = "POWER OFF";
                        ampBtBadge.className = "badge";
                    } else if (sppConnected) {
                        ampBtBadge.innerText = "ACTIVE (SPP CONNECTED)";
                        ampBtBadge.className = "badge active-green";
                    } else {
                        if (isPowerInjected) {
                            ampBtBadge.innerText = isId ? "STANDBY (EXT POWER • NO SPP)" : "STANDBY (EXT POWER • NO SPP)";
                        } else {
                            ampBtBadge.innerText = isId ? "READY (NO SPP)" : "READY (NO SPP)";
                        }
                        ampBtBadge.className = "badge badge-warning";
                    }
                }

                const elQccPwr = document.getElementById('amp-bt-qcc-pwr');
                if (elQccPwr) {
                    if (!qccPwr) {
                        elQccPwr.innerText = "OFF (GPIO 5 LOW)";
                        elQccPwr.style.color = "var(--text-dim)";
                    } else if (isPowerInjected) {
                        elQccPwr.innerText = "EXT INJECTED (5V)";
                        elQccPwr.style.color = "var(--accent-amber)";
                    } else {
                        elQccPwr.innerText = "POWER ON (5V)";
                        elQccPwr.style.color = "var(--accent-green)";
                    }
                }

                const elSppState = document.getElementById('amp-bt-spp-state');
                if (elSppState) {
                    if (sppConnected) {
                        elSppState.innerText = "CONNECTED";
                        elSppState.style.color = "var(--accent-green)";
                    } else if (qccPwr && isPowerOn) {
                        elSppState.innerText = sppState.toUpperCase();
                        elSppState.style.color = "var(--accent-amber)";
                    } else {
                        elSppState.innerText = "DISCONNECTED";
                        elSppState.style.color = "var(--text-dim)";
                    }
                }

                const elRssi = document.getElementById('amp-bt-rssi');
                if (elRssi) {
                    if (isPowerOn && sppConnected && btRssi && btRssi !== 0) {
                        elRssi.innerText = `${btRssi} dBm`;
                        elRssi.style.color = btRssi >= -60 ? "var(--accent-green)" : (btRssi >= -75 ? "var(--accent-cyan)" : "var(--accent-amber)");
                    } else {
                        elRssi.innerText = "-- dBm";
                        elRssi.style.color = "var(--text-dim)";
                    }
                }

                const elEspTx = document.getElementById('amp-bt-esp-tx');
                if (elEspTx) {
                    if (!isPowerOn || espTxPower === "--") {
                        elEspTx.innerText = "--";
                        elEspTx.style.color = "var(--text-dim)";
                    } else {
                        elEspTx.innerText = espTxPower;
                        elEspTx.style.color = sppConnected ? "var(--accent-cyan)" : "var(--accent-amber)";
                    }
                }

                const elCodec = document.getElementById('amp-bt-codec');
                if (elCodec) {
                    if (isPowerOn && sppConnected && activeCodec && activeCodec !== "None" && activeCodec !== "--") {
                        elCodec.innerText = activeCodec;
                        elCodec.style.color = "var(--accent-green)";
                    } else {
                        elCodec.innerText = "--";
                        elCodec.style.color = "var(--text-dim)";
                    }
                }

                // 2. Thermal & Voltages (Multi-Sensor 1-Wire: Stereo, Subwoofer, SMPS)
                const heatC = (rt.heat_c !== undefined) ? rt.heat_c : (hz.heat_c !== undefined ? hz.heat_c : null);
                const heatAb = (rt.heat_ab !== undefined && rt.heat_ab !== null) ? rt.heat_ab : (hz.thermal && hz.thermal.class_ab && hz.thermal.class_ab.temp !== null ? hz.thermal.class_ab.temp : heatC);
                const heatD = (rt.heat_d !== undefined && rt.heat_d !== null) ? rt.heat_d : (hz.thermal && hz.thermal.class_d && hz.thermal.class_d.temp !== null ? hz.thermal.class_d.temp : null);
                const heatSmps = (rt.heat_smps !== undefined && rt.heat_smps !== null) ? rt.heat_smps : (hz.thermal && hz.thermal.smps && hz.thermal.smps.temp !== null ? hz.thermal.smps.temp : null);

                const heatVal = document.getElementById('amp-heat-temp-val');
                if (heatVal) {
                    if (heatAb !== null && heatAb !== undefined) {
                        const hNum = parseFloat(heatAb);
                        heatVal.innerText = `${hNum.toFixed(1)} °C`;
                        heatVal.style.color = hNum > 65 ? "#ef4444" : (hNum > 50 ? "#f59e0b" : "var(--accent-green)");
                    } else {
                        heatVal.innerText = isId ? "Belum Terpasang" : "Not Connected";
                        heatVal.style.color = "var(--text-dim)";
                    }
                }

                const subTempVal = document.getElementById('amp-sub-temp-val');
                if (subTempVal) {
                    if (heatD !== null && heatD !== undefined) {
                        const dNum = parseFloat(heatD);
                        subTempVal.innerText = `${dNum.toFixed(1)} °C`;
                        subTempVal.style.color = dNum > 65 ? "#ef4444" : (dNum > 50 ? "#f59e0b" : "var(--accent-green)");
                    } else {
                        subTempVal.innerText = isId ? "Belum Terpasang" : "Not Connected";
                        subTempVal.style.color = "var(--text-dim)";
                    }
                }

                const smpsTempVal = document.getElementById('amp-smps-temp-val');
                if (smpsTempVal) {
                    if (heatSmps !== null && heatSmps !== undefined) {
                        const sNum = parseFloat(heatSmps);
                        smpsTempVal.innerText = `${sNum.toFixed(1)} °C`;
                        smpsTempVal.style.color = sNum > 65 ? "#ef4444" : (sNum > 50 ? "#f59e0b" : "var(--accent-green)");
                    } else {
                        smpsTempVal.innerText = isId ? "Belum Terpasang" : "Not Connected";
                        smpsTempVal.style.color = "var(--text-dim)";
                    }
                }

                // Update ROM Badges in Thermal Sensor Card
                if (hz.thermal || hz.nvs) {
                    const romAb = (hz.thermal && hz.thermal.class_ab && hz.thermal.class_ab.rom) || (hz.nvs && hz.nvs.temp_ab_rom) || "0000000000000000";
                    const romD = (hz.thermal && hz.thermal.class_d && hz.thermal.class_d.rom) || (hz.nvs && hz.nvs.temp_d_rom) || "0000000000000000";
                    const romSmps = (hz.thermal && hz.thermal.smps && hz.thermal.smps.rom) || (hz.nvs && hz.nvs.temp_smps_rom) || "0000000000000000";

                    const elRomAb = document.getElementById('temp-rom-stereo');
                    if (elRomAb) elRomAb.innerText = (romAb && romAb !== "0000000000000000") ? `0x${romAb}` : (isId ? "Belum Diatur" : "Unassigned");
                    const elRomD = document.getElementById('temp-rom-sub');
                    if (elRomD) elRomD.innerText = (romD && romD !== "0000000000000000") ? `0x${romD}` : (isId ? "Belum Diatur" : "Unassigned");
                    const elRomSmps = document.getElementById('temp-rom-smps');
                    if (elRomSmps) elRomSmps.innerText = (romSmps && romSmps !== "0000000000000000") ? `0x${romSmps}` : (isId ? "Belum Diatur" : "Unassigned");
                }

                const stbyV = (rt.v_stby !== undefined) ? rt.v_stby : (hz.v_stby !== undefined ? hz.v_stby : null);
                const stbyVal = document.getElementById('amp-stby-volt-val');
                if (stbyVal && stbyV !== null && stbyV !== undefined) {
                    stbyVal.innerText = `${parseFloat(stbyV).toFixed(2)} V`;
                }

                const smpsV = (rt.v_smps !== undefined && rt.v_smps > 0) ? rt.v_smps : ((hz.smps && hz.smps.v !== undefined) ? hz.smps.v : (rt.v_smps !== undefined ? rt.v_smps : 0));
                const smpsVal = document.getElementById('amp-smps-volt-val');
                if (smpsVal && smpsV !== null && smpsV !== undefined) {
                    const sNum = parseFloat(smpsV);
                    smpsVal.innerText = `${sNum.toFixed(2)} V`;
                    smpsVal.style.color = sNum > 18 ? "var(--accent-green)" : "var(--text-dim)";
                }

                // ADS1115 4-Channel Precision Hardware Voltmeter (SMPS, Standby, VDD 3.3V, ESP 5V)
                const vVdd = (hz.v_vdd !== undefined) ? hz.v_vdd : (tel.v_vdd !== undefined ? tel.v_vdd : null);
                const vEsp = (hz.v_esp !== undefined) ? hz.v_esp : (tel.v_esp !== undefined ? tel.v_esp : null);

                const vddVal = document.getElementById('amp-vdd-volt-val');
                if (vddVal && vVdd !== null && vVdd !== undefined) {
                    vddVal.innerText = `${parseFloat(vVdd).toFixed(2)} V`;
                }
                const espVal = document.getElementById('amp-esp-volt-val');
                if (espVal && vEsp !== null && vEsp !== undefined) {
                    espVal.innerText = `${parseFloat(vEsp).toFixed(2)} V`;
                }

                const diagAdsSmps = document.getElementById('diag-ads-smps-val');
                if (diagAdsSmps && smpsV !== null && smpsV !== undefined) {
                    diagAdsSmps.innerText = `${parseFloat(smpsV).toFixed(2)} V`;
                }
                const diagAdsStby = document.getElementById('diag-ads-stby-val');
                if (diagAdsStby && stbyV !== null && stbyV !== undefined) {
                    diagAdsStby.innerText = `${parseFloat(stbyV).toFixed(2)} V`;
                }
                const diagAdsVdd = document.getElementById('diag-ads-vdd-val');
                if (diagAdsVdd && vVdd !== null && vVdd !== undefined) {
                    diagAdsVdd.innerText = `${parseFloat(vVdd).toFixed(2)} V`;
                }
                const diagAdsEsp = document.getElementById('diag-ads-esp-val');
                if (diagAdsEsp && vEsp !== null && vEsp !== undefined) {
                    diagAdsEsp.innerText = `${parseFloat(vEsp).toFixed(2)} V`;
                }

                // Audio Analyzer Stereo ADC Resistor Divider & DC Bias (GPIO 34 Left / GPIO 39 Right)
                const anaCfg = (hz.analyzer && typeof hz.analyzer === 'object') ? hz.analyzer : {};
                const dcL = (rt.dc_l !== undefined) ? rt.dc_l : (anaCfg.dc_l !== undefined ? anaCfg.dc_l : (tel.dc_l !== undefined ? tel.dc_l : null));
                const dcR = (rt.dc_r !== undefined) ? rt.dc_r : (anaCfg.dc_r !== undefined ? anaCfg.dc_r : (tel.dc_r !== undefined ? tel.dc_r : null));
                const p2pL = (rt.p2p_l !== undefined) ? rt.p2p_l : (anaCfg.p2p_l !== undefined ? anaCfg.p2p_l : 0);
                const p2pR = (rt.p2p_r !== undefined) ? rt.p2p_r : (anaCfg.p2p_r !== undefined ? anaCfg.p2p_r : 0);

                let biasVoltL = 0.0;
                let biasVoltR = 0.0;
                if (dcL !== null) {
                    biasVoltL = (dcL * 3.3) / 4095.0;
                    const biasElemL = document.getElementById('amp-adc-bias-l-val');
                    if (biasElemL) biasElemL.innerText = `${biasVoltL.toFixed(3)} V (Raw: ${dcL} / 4095)`;
                    const anaBiasL = document.getElementById('ana-dc-bias-l-val');
                    if (anaBiasL) anaBiasL.innerText = `${biasVoltL.toFixed(3)} V (Offset: ${dcL})`;
                }
                if (dcR !== null) {
                    biasVoltR = (dcR * 3.3) / 4095.0;
                    const biasElemR = document.getElementById('amp-adc-bias-r-val');
                    if (biasElemR) biasElemR.innerText = `${biasVoltR.toFixed(3)} V (Raw: ${dcR} / 4095)`;
                    const anaBiasR = document.getElementById('ana-dc-bias-r-val');
                    if (anaBiasR) anaBiasR.innerText = `${biasVoltR.toFixed(3)} V (Offset: ${dcR})`;
                }

                const p2pElem = document.getElementById('amp-adc-p2p-val');
                if (p2pElem) {
                    p2pElem.innerText = `L: ${p2pL} | R: ${p2pR}`;
                }

                const biasStatus = document.getElementById('amp-adc-bias-status');
                if (biasStatus) {
                    if (biasVoltL >= 1.2 && biasVoltL <= 2.1 && biasVoltR >= 1.2 && biasVoltR <= 2.1) {
                        biasStatus.innerText = isId ? "VIRTUAL GROUND SEIMBANG (~1.65V)" : "VIRTUAL GROUND BALANCED (~1.65V)";
                        biasStatus.style.color = "var(--accent-green)";
                    } else if (biasVoltL < 0.2 || biasVoltR < 0.2) {
                        biasStatus.innerText = isId ? "BIAS DROOP / GROUND SHORT" : "BIAS DROOP / GND SHORT";
                        biasStatus.style.color = "#ef4444";
                    } else {
                        biasStatus.innerText = isId ? "ASYMMETRIC BIAS (DIVIDER OFFSET)" : "ASYMMETRIC BIAS (DIVIDER OFFSET)";
                        biasStatus.style.color = "#f59e0b";
                    }
                }

                // 3. DC Fault
                const dcVal = document.getElementById('amp-dc-fault-val');
                if (dcVal) {
                    if (!isPowerOn) {
                        dcVal.innerText = isId ? "STANDBY (OFF)" : "STANDBY (OFF)";
                        dcVal.style.color = "var(--text-dim)";
                    } else if (rt.spk_fault) {
                        dcVal.innerText = isId ? "PROTEKSI TRIP! (DC Offset)" : "FAULT TRIP! (DC Offset)";
                        dcVal.style.color = "#ef4444";
                    } else if (!isSpkOn) {
                        dcVal.innerText = isId ? "CUT-OFF (MUTED)" : "CUT-OFF (MUTED)";
                        dcVal.style.color = "#f59e0b";
                    } else {
                        dcVal.innerText = isId ? "NORMAL (Aman)" : "NORMAL (Protected)";
                        dcVal.style.color = "var(--accent-green)";
                    }
                }

                // Diagnostic & RTC Card Telemetry Updates
                const rawTime = hz.time || rt.time || hz.current_time || rt.current_time || tel.time || tel.current_time;
                if (rawTime) {
                    syncRtcAnchorFromTimeStr(rawTime);
                }
                const rtcTempVal = document.getElementById('amp-rtc-temp-val');
                const rtcTemp = (hz.rtc_c !== undefined) ? hz.rtc_c : rt.rtc_c;
                if (rtcTempVal && rtcTemp !== undefined && rtcTemp !== null) {
                    rtcTempVal.innerText = `${parseFloat(rtcTemp).toFixed(1)} °C`;
                }

                // SQW Pulse Hardware Status & Frequency Validation
                const sqwStatusVal = document.getElementById('amp-sqw-status-val');
                if (sqwStatusVal) {
                    const sqw = hz.sqw || rt.sqw;
                    if (sqw && sqw.hw_connected) {
                        const freqNum = (typeof sqw.freq === 'number') ? sqw.freq : parseFloat(sqw.freq || 0);
                        sqwStatusVal.innerText = `GPIO 36 (Connected • ${freqNum.toFixed(2)} Hz)`;
                        sqwStatusVal.className = "stat-val val-green";
                    } else if (sqw && sqw.status === "INVALID_FREQ") {
                        sqwStatusVal.innerText = `GPIO 36 (Signal Error / Noise)`;
                        sqwStatusVal.className = "stat-val val-warn";
                    } else {
                        sqwStatusVal.innerText = `GPIO 36 (Belum Terhubung / Fallback Timer)`;
                        sqwStatusVal.className = "stat-val";
                        sqwStatusVal.style.color = "var(--text-dim)";
                    }
                }

                // Sleep Timer Live Countdown
                const sleepTimerMin = (hz.sleep_timer !== undefined) ? hz.sleep_timer : null;
                const sleepBadge = document.getElementById('amp-sleep-countdown-badge');
                if (sleepBadge && sleepTimerMin !== null) {
                    if (sleepTimerMin > 0) {
                        sleepBadge.innerText = `${sleepTimerMin}m Active`;
                        sleepBadge.className = 'badge active-green';
                    } else {
                        sleepBadge.innerText = 'OFF';
                        sleepBadge.className = 'badge';
                    }
                    document.querySelectorAll('.btn-sleep-chip').forEach(btn => {
                        btn.classList.toggle('active', parseInt(btn.dataset.min) === parseInt(sleepTimerMin));
                    });
                }

                // Complete NVS Settings Synchronization (Matching ESP32 Flash State)
                if (hz.nvs) {
                    const nvs = hz.nvs;

                    // 1. Fan Governor & Duty
                    if (nvs.fan_mode_str) {
                        const isAuto = nvs.fan_mode_str.toLowerCase() === 'auto';
                        document.getElementById('btn-fan-auto')?.classList.toggle('active', isAuto);
                        document.getElementById('btn-fan-custom')?.classList.toggle('active', !isAuto);
                        const fanBadge = document.getElementById('amp-fan-mode-badge');
                        if (fanBadge) fanBadge.innerText = nvs.fan_mode_str.toUpperCase();
                    }
                    if (nvs.fan_duty !== undefined) {
                        const fanSlider = document.getElementById('amp-fan-duty-slider');
                        const fanNum = document.getElementById('amp-fan-duty-num');
                        if (fanSlider && (typeof fanSlider.matches !== 'function' || !fanSlider.matches(':active'))) {
                            fanSlider.value = nvs.fan_duty;
                            if (fanNum) fanNum.innerText = Math.round(nvs.fan_duty / 10.23) + '%';
                        }
                    }

                    // 2. SMPS Protection & Thresholds
                    if (nvs.smps_bypass !== undefined) {
                        document.getElementById('btn-smps-on')?.classList.toggle('active', !nvs.smps_bypass);
                        document.getElementById('btn-smps-off')?.classList.toggle('active', !!nvs.smps_bypass);
                        const smpsBadge = document.getElementById('amp-smps-mode-badge');
                        if (smpsBadge) {
                            smpsBadge.innerText = nvs.smps_bypass ? 'BYPASS' : 'PROTECTED';
                            smpsBadge.className = nvs.smps_bypass ? 'badge active-red' : 'badge active-green';
                        }
                    }
                    if (nvs.smps_cut !== undefined) {
                        const cutInput = document.getElementById('amp-smps-cutoff-val');
                        if (cutInput && document.activeElement !== cutInput) {
                            cutInput.value = parseFloat(nvs.smps_cut).toFixed(1);
                        }
                    }
                    if (nvs.smps_rec !== undefined) {
                        const recInput = document.getElementById('amp-smps-rec-val');
                        if (recInput && document.activeElement !== recInput) {
                            recInput.value = parseFloat(nvs.smps_rec).toFixed(1);
                        }
                    }

                    // 3. Thermal PID & OTP Thresholds
                    if (nvs.pid_sp !== undefined) {
                        const pidSlider = document.getElementById('amp-pid-sp-slider');
                        const pidNum = document.getElementById('amp-pid-sp-num');
                        if (pidSlider && (typeof pidSlider.matches !== 'function' || !pidSlider.matches(':active'))) {
                            pidSlider.value = nvs.pid_sp;
                            if (pidNum) pidNum.innerText = parseFloat(nvs.pid_sp).toFixed(1) + '°C';
                        }
                    }
                    if (nvs.otp_thr !== undefined) {
                        const otpSlider = document.getElementById('amp-otp-thr-slider');
                        const otpNum = document.getElementById('amp-otp-thr-num');
                        if (otpSlider && (typeof otpSlider.matches !== 'function' || !otpSlider.matches(':active'))) {
                            otpSlider.value = nvs.otp_thr;
                            if (otpNum) otpNum.innerText = parseFloat(nvs.otp_thr).toFixed(1) + '°C';
                        }
                    }

                    // 4. Output Relays & Controls
                    if (nvs.spk_pwr !== undefined) {
                        document.getElementById('btn-spk-on')?.classList.toggle('active', !!nvs.spk_pwr);
                        document.getElementById('btn-spk-off')?.classList.toggle('active', !nvs.spk_pwr);
                    }
                    if (nvs.bt_en !== undefined) {
                        document.getElementById('btn-bt-on')?.classList.toggle('active', !!nvs.bt_en);
                        document.getElementById('btn-bt-off')?.classList.toggle('active', !nvs.bt_en);
                    }
                    if (nvs.btn_lock !== undefined) {
                        document.getElementById('btn-lock-on')?.classList.toggle('active', !!nvs.btn_lock);
                        document.getElementById('btn-lock-off')?.classList.toggle('active', !nvs.btn_lock);
                    }

                    // 5. Signal Auto-Sense
                    if (nvs.auto_sense_en !== undefined) {
                        document.getElementById('btn-auto-sense-on')?.classList.toggle('active', !!nvs.auto_sense_en);
                        document.getElementById('btn-auto-sense-off')?.classList.toggle('active', !nvs.auto_sense_en);
                    }

                    // 6. Piezo Buzzer Settings
                    if (nvs.buzz_en !== undefined) {
                        document.getElementById('btn-buzz-on')?.classList.toggle('active', !!nvs.buzz_en);
                        document.getElementById('btn-buzz-off')?.classList.toggle('active', !nvs.buzz_en);
                    }
                    if (nvs.buzz_vol !== undefined) {
                        const buzzSlider = document.getElementById('amp-buzz-vol-slider');
                        const buzzNum = document.getElementById('amp-buzz-vol-num');
                        if (buzzSlider && (typeof buzzSlider.matches !== 'function' || !buzzSlider.matches(':active'))) {
                            buzzSlider.value = nvs.buzz_vol;
                            if (buzzNum) buzzNum.innerText = nvs.buzz_vol + '%';
                        }
                    }

                    // 7. Bluetooth Auto-Off
                    if (nvs.bt_off_min !== undefined) {
                        const btBadge = document.getElementById('amp-bt-autooff-badge');
                        if (btBadge) {
                            btBadge.innerText = nvs.bt_off_min > 0 ? `${nvs.bt_off_min}m` : 'NEVER';
                            btBadge.className = nvs.bt_off_min > 0 ? 'badge active-cyan' : 'badge';
                        }
                        document.querySelectorAll('.btn-bt-chip').forEach(btn => {
                            btn.classList.toggle('active', parseInt(btn.dataset.min) === parseInt(nvs.bt_off_min));
                        });
                    }

                    // 8. Sleep Timer
                    if (nvs.sleep_timer !== undefined) {
                        const sleepBadge = document.getElementById('amp-sleep-countdown-badge');
                        if (sleepBadge) {
                            sleepBadge.innerText = nvs.sleep_timer > 0 ? `${nvs.sleep_timer}m Active` : 'OFF';
                            sleepBadge.className = nvs.sleep_timer > 0 ? 'badge active-green' : 'badge';
                        }
                        document.querySelectorAll('.btn-sleep-chip').forEach(btn => {
                            btn.classList.toggle('active', parseInt(btn.dataset.min) === parseInt(nvs.sleep_timer));
                        });
                    }

                    // 9. Hardware DSP Enhancements & EQ Preset (Persisted from ESP32 NVS)
                    if (nvs.bass_boost !== undefined) {
                        const isBass = !!nvs.bass_boost;
                        const btnBass = document.getElementById('btn-amp-bass');
                        const pillBass = document.getElementById('pill-amp-bass');
                        if (btnBass) btnBass.classList.toggle('active', isBass);
                        if (pillBass) pillBass.innerText = isBass ? 'ON' : 'OFF';
                    }
                    if (nvs.audio_3d !== undefined) {
                        const is3d = !!nvs.audio_3d;
                        const btn3d = document.getElementById('btn-amp-3d');
                        const pill3d = document.getElementById('pill-amp-3d');
                        if (btn3d) btn3d.classList.toggle('active', is3d);
                        if (pill3d) pill3d.innerText = is3d ? 'ON' : 'OFF';
                    }
                    if (nvs.eq_preset !== undefined) {
                        const eqSelect = document.getElementById('amp-eq-preset');
                        if (eqSelect && document.activeElement !== eqSelect) {
                            eqSelect.value = String(nvs.eq_preset);
                        }
                    }
                    if (nvs.ch_mode !== undefined) {
                        updateChannelModeUI(nvs.ch_mode);
                    }
                }

                const diagSmpsVal = document.getElementById('amp-diag-smps-val');
                if (diagSmpsVal && smpsV !== null && smpsV !== undefined) {
                    const sNum = parseFloat(smpsV);
                    diagSmpsVal.innerText = `${sNum.toFixed(2)} V`;
                    diagSmpsVal.style.color = sNum > 18 ? "var(--accent-green)" : "var(--text-dim)";
                }
                const diagTempVal = document.getElementById('amp-diag-temp-val');
                if (diagTempVal && heatC !== null && heatC !== undefined) {
                    const hNum = parseFloat(heatC);
                    diagTempVal.innerText = `${hNum.toFixed(1)} °C`;
                    diagTempVal.style.color = hNum > 65 ? "#ef4444" : (hNum > 50 ? "#f59e0b" : "var(--accent-green)");
                }
                const diagErrVal = document.getElementById('amp-diag-err-code');
                if (diagErrVal) {
                    const errs = (hz.errors && Array.isArray(hz.errors) && hz.errors.length) ? hz.errors.join(', ') : (rt.err || rt.err_code || '');
                    const warns = (hz.warnings && Array.isArray(hz.warnings) && hz.warnings.length) ? hz.warnings.join(', ') : '';
                    if (errs) {
                        diagErrVal.innerText = `ERR: ${errs}`;
                        diagErrVal.style.color = "#ef4444";
                    } else if (warns) {
                        diagErrVal.innerText = `WARN: ${warns}`;
                        diagErrVal.style.color = "#f59e0b";
                    } else {
                        diagErrVal.innerText = isId ? "NORMAL (Sistem Sehat)" : "NONE (Healthy)";
                        diagErrVal.style.color = "var(--accent-green)";
                    }
                }

                // 4. Hardware Protections & Fan NVS Controls Sync
                const fanModeBadge = document.getElementById('amp-fan-mode-badge');
                if (fanModeBadge && hz.nvs && hz.nvs.fan_mode_str) {
                    fanModeBadge.innerText = hz.nvs.fan_mode_str.toUpperCase();
                    fanModeBadge.className = hz.nvs.fan_mode_str === 'auto' ? 'badge active-green' : 'badge';
                }
                const fanSlider = document.getElementById('amp-fan-duty-slider');
                const fanNum = document.getElementById('amp-fan-duty-num');
                const isFanActive = fanSlider && ((typeof fanSlider.matches === 'function' && fanSlider.matches(':active')) || document.activeElement === fanSlider);
                if (fanSlider && !isFanActive && hz.nvs && hz.nvs.fan_duty !== undefined) {
                    fanSlider.value = hz.nvs.fan_duty;
                    if (fanNum) fanNum.innerText = `${Math.round(hz.nvs.fan_duty / 10.23)}%`;
                }

                const smpsCutInput = document.getElementById('amp-smps-cutoff-val');
                const isCutFocus = smpsCutInput && ((typeof smpsCutInput.matches === 'function' && smpsCutInput.matches(':focus')) || document.activeElement === smpsCutInput);
                if (smpsCutInput && !isCutFocus) {
                    if (hz.nvs && hz.nvs.smps_cut !== undefined) smpsCutInput.value = parseFloat(hz.nvs.smps_cut).toFixed(1);
                    else if (hz.smps && hz.smps.cutoff !== undefined) smpsCutInput.value = parseFloat(hz.smps.cutoff).toFixed(1);
                }

                const smpsRecInput = document.getElementById('amp-smps-rec-val');
                const isRecFocus = smpsRecInput && ((typeof smpsRecInput.matches === 'function' && smpsRecInput.matches(':focus')) || document.activeElement === smpsRecInput);
                if (smpsRecInput && !isRecFocus) {
                    if (hz.nvs && hz.nvs.smps_rec !== undefined) smpsRecInput.value = parseFloat(hz.nvs.smps_rec).toFixed(1);
                    else if (hz.smps && hz.smps.recover !== undefined) smpsRecInput.value = parseFloat(hz.smps.recover).toFixed(1);
                }

                const smpsBadge = document.getElementById('amp-smps-mode-badge');
                if (smpsBadge) {
                    const isBypass = !!(hz.nvs && hz.nvs.smps_bypass);
                    smpsBadge.innerText = isBypass ? "BYPASS (UNPROTECTED)" : "PROTECTED";
                    smpsBadge.className = isBypass ? "badge active-warn" : "badge active-green";
                }

                // 5. Audio Analyzer Controls, NVS Engine Config & Spectrum Meters
                const anaMode = (rt.mode || anaCfg.mode || 'fft').toLowerCase();
                document.getElementById('btn-ana-mode-fft')?.classList.toggle('active', anaMode === 'fft');
                document.getElementById('btn-ana-mode-vu')?.classList.toggle('active', anaMode === 'vu');
                document.getElementById('btn-ana-mode-off')?.classList.toggle('active', anaMode === 'off');

                const hwBands = parseInt(rt.bands_len || anaCfg.bands_len || rt.ana_bands || 0, 10);
                if ([8, 16, 32, 64].includes(hwBands)) {
                    const bandsSelect = document.getElementById('analyzer-bands-select');
                    if (bandsSelect && document.activeElement !== bandsSelect) {
                        bandsSelect.value = String(hwBands);
                    }
                    const engBadge = document.getElementById('analyzer-engine-badge');
                    if (engBadge) engBadge.innerText = `KissFFT ${hwBands}`;
                }

                const gainLVal = (rt.gain_l !== undefined) ? rt.gain_l : anaCfg.gain_l;
                if (gainLVal !== undefined) {
                    const glSlider = document.getElementById('analyzer-gain-l-slider');
                    const glNum = document.getElementById('analyzer-gain-l-num');
                    if (glSlider && (typeof glSlider.matches !== 'function' || !glSlider.matches(':active'))) {
                        glSlider.value = parseFloat(gainLVal).toFixed(1);
                        if (glNum) glNum.innerText = parseFloat(gainLVal).toFixed(1) + 'x';
                    }
                }
                const gainRVal = (rt.gain_r !== undefined) ? rt.gain_r : anaCfg.gain_r;
                if (gainRVal !== undefined) {
                    const grSlider = document.getElementById('analyzer-gain-r-slider');
                    const grNum = document.getElementById('analyzer-gain-r-num');
                    if (grSlider && (typeof grSlider.matches !== 'function' || !grSlider.matches(':active'))) {
                        grSlider.value = parseFloat(gainRVal).toFixed(1);
                        if (grNum) grNum.innerText = parseFloat(gainRVal).toFixed(1) + 'x';
                    }
                }
                const noiseLVal = (rt.noise_l !== undefined) ? rt.noise_l : anaCfg.noise_l;
                if (noiseLVal !== undefined) {
                    const nlSlider = document.getElementById('analyzer-noise-l-slider');
                    const nlNum = document.getElementById('analyzer-noise-l-num');
                    if (nlSlider && (typeof nlSlider.matches !== 'function' || !nlSlider.matches(':active'))) {
                        nlSlider.value = Math.round(noiseLVal);
                        if (nlNum) nlNum.innerText = String(Math.round(noiseLVal));
                    }
                }
                const noiseRVal = (rt.noise_r !== undefined) ? rt.noise_r : anaCfg.noise_r;
                if (noiseRVal !== undefined) {
                    const nrSlider = document.getElementById('analyzer-noise-r-slider');
                    const nrNum = document.getElementById('analyzer-noise-r-num');
                    if (nrSlider && (typeof nrSlider.matches !== 'function' || !nrSlider.matches(':active'))) {
                        nrSlider.value = Math.round(noiseRVal);
                        if (nrNum) nrNum.innerText = String(Math.round(noiseRVal));
                    }
                }

                const runtimeBadge = document.getElementById('analyzer-runtime-badge');
                if (runtimeBadge) {
                    if (isPowerOn) {
                        runtimeBadge.innerHTML = `<i class="fa-solid fa-circle-dot" style="font-size: 0.6rem; color: var(--accent-green); margin-right: 4px;"></i> RUNTIME ACTIVE`;
                        runtimeBadge.className = "badge active-green";
                    } else {
                        runtimeBadge.innerHTML = `<i class="fa-solid fa-circle" style="font-size: 0.6rem; color: var(--text-dim); margin-right: 4px;"></i> STANDBY (DORMANT - 0% CPU)`;
                        runtimeBadge.className = "badge";
                    }
                }

                // Ensure spectrum bars DOM is initialized and band count synced
                initSpectrumBars();
                if ([8, 16, 32, 64].includes(hwBands) && hwBands !== activeBandsCount) {
                    updateVisibleSpectrumBands(hwBands);
                } else {
                    updateVisibleSpectrumBands(activeBandsCount);
                }

                if (!isPowerOn) {
                    // When in Standby, zero out bars and meters immediately, but keep DC offset stats live
                    resetSpectrumBars();
                    const statsL = document.getElementById('analyzer-l-stats');
                    if (statsL) statsL.innerText = `Peak: 0 (N/A) | P2P: ${p2pL} | DC: ${dcL !== null ? dcL : 0}`;
                    const statsR = document.getElementById('analyzer-r-stats');
                    if (statsR) statsR.innerText = `Peak: 0 (N/A) | P2P: ${p2pR} | DC: ${dcR !== null ? dcR : 0}`;
                    return;
                }

                // Update Analyzer Physics, Spectrum Bars, and Stereo VU Meters
                updateAnalyzerPhysics(rt);
                renderSpectrumBars();
                renderVuMeters();
                analyzerFrameCount++;

                // Stats Labels
                const vuL = rt.vu_l !== undefined ? rt.vu_l : 0;
                const vuR = rt.vu_r !== undefined ? rt.vu_r : 0;
                const statsL = document.getElementById('analyzer-l-stats');
                if (statsL) statsL.innerText = `Peak: ${vuL} (${lastPeakFreqL}) | Mag: ${rt.mag_l || 0} | DC: ${dcL !== null ? dcL : 0}`;
                const statsR = document.getElementById('analyzer-r-stats');
                if (statsR) statsR.innerText = `Peak: ${vuR} (${lastPeakFreqR}) | Mag: ${rt.mag_r || 0} | DC: ${dcR !== null ? dcR : 0}`;

                // Clipping Warning
                const clipBadge = document.getElementById('analyzer-clip-badge');
                if (clipBadge) {
                    clipBadge.style.display = (rt.clipping || (rt.clip_count && rt.clip_count > 0)) ? 'inline-block' : 'none';
                }
            } catch (uiErr) {
                console.error("updateAmpTelemetryUI error:", uiErr);
            }
        }

        // ==========================================================================
        // FAST ANALYZER POLLING ENGINE
        // ==========================================================================
        let analyzerTimer = null;
        let analyzerFrameCount = 0;
        let analyzerFpsTimer = null;

        function startAnalyzerFastLoop() {
            stopAnalyzerFastLoop();
            initSpectrumBars();
            
            analyzerFrameCount = 0;
            analyzerFpsTimer = setInterval(() => {
                const badge = document.getElementById('analyzer-fps-badge');
                if (badge) badge.innerText = `${analyzerFrameCount * 2} FPS`;
                analyzerFrameCount = 0;
            }, 500);

            const runFrame = async () => {
                if (currentView !== 'analyzer') return;
                // If SSE stream is active and healthy, it streams packets directly at 30 FPS.
                // Only poll HTTP amp_telemetry as fallback if SSE is disconnected.
                if (!isSseConnected || (Date.now() - lastSsePacketTime > 3000)) {
                    try {
                        const data = await getApiData('amp_telemetry', 1500);
                        if (data && data.telemetry) {
                            updateAmpTelemetryUI(data.telemetry);
                        }
                    } catch (e) {}
                }
                if (currentView === 'analyzer') {
                    analyzerTimer = setTimeout(runFrame, 100);
                }
            };
            runFrame();
        }

        function stopAnalyzerFastLoop() {
            if (analyzerTimer) {
                clearTimeout(analyzerTimer);
                analyzerTimer = null;
            }
            if (analyzerFpsTimer) {
                clearInterval(analyzerFpsTimer);
                analyzerFpsTimer = null;
            }
        }

        // Initialize view & sub-tabs from URL hash or localStorage
        const savedHash = window.location.hash.replace('#', '');
        const savedView = (['panel', 'amplifier', 'analyzer'].includes(savedHash))
            ? savedHash
            : (localStorage.getItem('jacktor_active_view') || 'panel');

        const savedPanelTab = localStorage.getItem('jacktor_panel_tab') || 'media';
        const savedAmpTab = localStorage.getItem('jacktor_amp_tab') || 'power';
        const savedAnalyzerTab = localStorage.getItem('jacktor_analyzer_tab') || 'spectrum';

        switchPanelTab(savedPanelTab);
        switchAmpTab(savedAmpTab);
        switchAnalyzerTab(savedAnalyzerTab);
        switchView(savedView);

        // Initialize i18n and authenticate session
        applyLanguage(currentLang);
        initAuth();
