/**
 * Jacktor Audio Framework • Modular i18n & Auto-Discovery Engine
 * Copyright (C) 2026 Ardo Zapp. All rights reserved.
 * 
 * Automatically discovers all language files (*.json) from /i18n/
 * and provides dynamic language switching with zero page reloads.
 */
(function() {
    const DEFAULT_LANG = 'id';
    const FALLBACK_LANG = 'en';

    const loadedTranslations = {};
    let availableLanguages = [
        { code: 'id', name: 'Bahasa Indonesia', flag: '🇮🇩', file: 'id.json' },
        { code: 'en', name: 'English', flag: '🇬🇧', file: 'en.json' }
    ];

    const urlParams = new URLSearchParams(window.location.search);
    let currentLang = urlParams.get('lang') || localStorage.getItem('jacktor_lang') || DEFAULT_LANG;

    async function discoverLanguages() {
        try {
            const res = await fetch('/cgi-bin/jacktor-api?action=languages');
            if (res.ok) {
                const data = await res.json();
                if (data && data.success && Array.isArray(data.languages) && data.languages.length > 0) {
                    availableLanguages = data.languages;
                    renderLanguageSelectorMenu();
                    return availableLanguages;
                }
            }
        } catch (e) {
            // API not reachable, try static manifest
        }

        try {
            const res2 = await fetch('/i18n/languages.json');
            if (res2.ok) {
                const data2 = await res2.json();
                if (Array.isArray(data2) && data2.length > 0) {
                    availableLanguages = data2;
                    renderLanguageSelectorMenu();
                    return availableLanguages;
                }
            }
        } catch (e2) {
            // Use static fallback
        }

        renderLanguageSelectorMenu();
        return availableLanguages;
    }

    async function loadLanguage(langCode) {
        if (loadedTranslations[langCode]) {
            return loadedTranslations[langCode];
        }

        try {
            const res = await fetch(`/i18n/${langCode}.json?v=${Date.now()}`);
            if (res.ok) {
                const dict = await res.json();
                loadedTranslations[langCode] = dict;
                return dict;
            }
        } catch (err) {
            console.warn(`[i18n] Failed to load /i18n/${langCode}.json:`, err);
        }

        if (langCode !== FALLBACK_LANG && !loadedTranslations[FALLBACK_LANG]) {
            await loadLanguage(FALLBACK_LANG);
        }
        return loadedTranslations[langCode] || loadedTranslations[FALLBACK_LANG] || {};
    }

    async function applyLanguage(langCode, showNotification = false) {
        if (!langCode) langCode = currentLang;
        currentLang = langCode;
        localStorage.setItem('jacktor_lang', langCode);

        const dict = await loadLanguage(langCode);
        const fallbackDict = (langCode !== FALLBACK_LANG) ? await loadLanguage(FALLBACK_LANG) : dict;

        function getTranslation(key) {
            if (dict[key] !== undefined) return dict[key];
            if (fallbackDict[key] !== undefined) return fallbackDict[key];
            return null;
        }

        const langLabel = document.getElementById('lang-label');
        if (langLabel) langLabel.innerText = langCode.toUpperCase();
        const loginLangLabel = document.getElementById('login-lang-label');
        if (loginLangLabel) loginLangLabel.innerText = langCode.toUpperCase();

        document.querySelectorAll('[data-i18n]').forEach(el => {
            const key = el.getAttribute('data-i18n');
            const val = getTranslation(key);
            if (val !== null) {
                const icon = el.querySelector('i');
                if (icon) {
                    const span = el.querySelector('span');
                    if (span) {
                        span.innerText = val;
                    } else {
                        el.innerHTML = '';
                        el.appendChild(icon);
                        el.appendChild(document.createTextNode(' ' + val));
                    }
                } else {
                    el.innerText = val;
                }
            }
        });

        document.querySelectorAll('[data-i18n-title]').forEach(el => {
            const key = el.getAttribute('data-i18n-title');
            const val = getTranslation(key);
            if (val !== null) el.setAttribute('title', val);
        });

        document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
            const key = el.getAttribute('data-i18n-placeholder');
            const val = getTranslation(key);
            if (val !== null) el.setAttribute('placeholder', val);
        });

        // Login screen explicit element binding
        const txtLoginTitle = document.getElementById('txt-login-title');
        if (txtLoginTitle) txtLoginTitle.innerText = getTranslation('login_title') || getTranslation('title') || txtLoginTitle.innerText;
        const txtLoginDesc = document.getElementById('txt-login-desc');
        if (txtLoginDesc) txtLoginDesc.innerText = getTranslation('login_desc') || getTranslation('desc') || txtLoginDesc.innerText;
        const lblLoginUser = document.getElementById('lbl-login-user');
        if (lblLoginUser) lblLoginUser.innerText = getTranslation('login_user') || getTranslation('user') || lblLoginUser.innerText;
        const lblLoginPass = document.getElementById('lbl-login-pass');
        if (lblLoginPass) lblLoginPass.innerText = getTranslation('login_pass') || getTranslation('pass') || lblLoginPass.innerText;
        const lblLoginRemember = document.getElementById('lbl-login-remember');
        if (lblLoginRemember) lblLoginRemember.innerText = getTranslation('login_remember') || getTranslation('remember') || lblLoginRemember.innerText;
        const lblLoginBtn = document.getElementById('lbl-login-btn');
        if (lblLoginBtn) lblLoginBtn.innerText = getTranslation('login_btn') || getTranslation('btn') || lblLoginBtn.innerText;
        const loginErrText = document.getElementById('login-error-text');
        if (loginErrText && loginErrText.innerText) {
            loginErrText.innerText = getTranslation('login_err_invalid') || getTranslation('err_invalid') || loginErrText.innerText;
        }

        renderLanguageSelectorMenu();

        if (showNotification && typeof window.showToast === 'function') {
            const langObj = availableLanguages.find(l => l.code === langCode);
            const name = langObj ? langObj.name : langCode.toUpperCase();
            const flag = langObj ? langObj.flag : '🌐';
            window.showToast(`${flag} Language: ${name}`, 'success', 2000);
        }

        window.dispatchEvent(new CustomEvent('jacktor-language-changed', { detail: { lang: langCode } }));
    }

    function renderLanguageSelectorMenu() {
        let menu = document.getElementById('jacktor-lang-dropdown');
        if (!menu) {
            menu = document.createElement('div');
            menu.id = 'jacktor-lang-dropdown';
            menu.className = 'jacktor-lang-dropdown';
            document.body.appendChild(menu);

            document.addEventListener('click', function(e) {
                const btn1 = document.getElementById('btn-lang');
                const btn2 = document.getElementById('login-btn-lang');
                if (menu && menu.classList.contains('active')) {
                    if (!menu.contains(e.target) && (!btn1 || !btn1.contains(e.target)) && (!btn2 || !btn2.contains(e.target))) {
                        closeLanguageSelector();
                    }
                }
            });
        }

        menu.innerHTML = `
            <div class="lang-dropdown-header">
                <i class="fa-solid fa-language"></i>
                <span>SELECT LANGUAGE</span>
            </div>
            <div class="lang-dropdown-list">
                ${availableLanguages.map(item => `
                    <div class="lang-dropdown-item ${item.code === currentLang ? 'active' : ''}" onclick="window.jacktorI18n.selectLanguage('${item.code}')">
                        <span class="lang-flag">${item.flag || '🌐'}</span>
                        <div class="lang-info">
                            <span class="lang-name">${item.name}</span>
                            <span class="lang-code">${item.code.toUpperCase()}</span>
                        </div>
                        ${item.code === currentLang ? '<i class="fa-solid fa-check lang-check"></i>' : ''}
                    </div>
                `).join('')}
            </div>
        `;
    }

    function toggleLanguageSelector(btnElement) {
        const menu = document.getElementById('jacktor-lang-dropdown');
        if (!menu) {
            renderLanguageSelectorMenu();
        }
        const m = document.getElementById('jacktor-lang-dropdown');
        if (!m) return;

        if (m.classList.contains('active')) {
            closeLanguageSelector();
        } else {
            openLanguageSelector(btnElement);
        }
    }

    function openLanguageSelector(btnElement) {
        const menu = document.getElementById('jacktor-lang-dropdown');
        if (!menu) return;

        renderLanguageSelectorMenu();

        const btn = btnElement || document.getElementById('btn-lang') || document.getElementById('login-btn-lang');
        if (btn) {
            const rect = btn.getBoundingClientRect();
            menu.style.top = (rect.bottom + 8) + 'px';
            menu.style.left = Math.max(10, Math.min(window.innerWidth - 250, rect.right - 230)) + 'px';
        }

        menu.classList.add('active');
    }

    function closeLanguageSelector() {
        const menu = document.getElementById('jacktor-lang-dropdown');
        if (menu) menu.classList.remove('active');
    }

    async function selectLanguage(code) {
        closeLanguageSelector();
        if (code !== currentLang) {
            await applyLanguage(code, true);
        }
    }

    window.jacktorI18n = {
        getCurrentLang: () => currentLang,
        getAvailableLanguages: () => availableLanguages,
        discoverLanguages: discoverLanguages,
        loadLanguage: loadLanguage,
        applyLanguage: applyLanguage,
        toggleSelector: toggleLanguageSelector,
        openSelector: openLanguageSelector,
        closeSelector: closeLanguageSelector,
        selectLanguage: selectLanguage,
        t: (key, fallback) => {
            const dict = loadedTranslations[currentLang] || {};
            const fbDict = loadedTranslations[FALLBACK_LANG] || {};
            return dict[key] !== undefined ? dict[key] : (fbDict[key] !== undefined ? fbDict[key] : (fallback !== undefined ? fallback : key));
        }
    };

    // Auto-discover and apply immediately when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', async () => {
            await discoverLanguages();
            await applyLanguage(currentLang, false);
        });
    } else {
        discoverLanguages().then(() => applyLanguage(currentLang, false));
    }
})();
