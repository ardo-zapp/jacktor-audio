        function toggleLanguage() {
            if (window.jacktorI18n && typeof window.jacktorI18n.toggleSelector === 'function') {
                window.jacktorI18n.toggleSelector(document.getElementById('login-btn-lang'));
            }
        }

        function applyLanguage(lang) {
            if (window.jacktorI18n && typeof window.jacktorI18n.applyLanguage === 'function') {
                window.jacktorI18n.applyLanguage(lang);
            }
        }

        function togglePasswordVisibility(inputId, btn) {
            const el = document.getElementById(inputId);
            if (!el) return;
            if (el.type === 'password') {
                el.type = 'text';
                btn.innerHTML = '<i class="fa-solid fa-eye-slash"></i>';
            } else {
                el.type = 'password';
                btn.innerHTML = '<i class="fa-solid fa-eye"></i>';
            }
        }

        async function handleLoginSubmit(event) {
            if (event) event.preventDefault();
            const userInput = document.getElementById('login-username');
            const passInput = document.getElementById('login-password');
            const rememberInput = document.getElementById('login-remember');
            const errorBox = document.getElementById('login-error-box');
            const submitBtn = document.getElementById('btn-login-submit');

            const u = userInput ? userInput.value.trim() : '';
            const p = passInput ? passInput.value : '';
            const remember = rememberInput ? rememberInput.checked : true;
            const t = (k, fb) => (window.jacktorI18n ? window.jacktorI18n.t(k, fb) : fb);
            const dict = {
                logging_in: t('login_logging_in', 'Logging in...'),
                btn: t('login_btn', 'Login to System'),
                err_invalid: t('login_err_invalid', 'Invalid credentials')
            };

            if (errorBox) errorBox.style.display = 'none';
            if (submitBtn) {
                submitBtn.disabled = true;
                submitBtn.innerHTML = `<i class="fa-solid fa-spinner fa-spin"></i> <span>${dict.logging_in}</span>`;
            }

            try {
                const bodyData = `action=auth_login&user=${encodeURIComponent(u)}&pass=${encodeURIComponent(p)}`;
                let res = null;
                try {
                    const resp = await fetch('/cgi-bin/jacktor-api', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                        body: bodyData
                    });
                    if (resp.ok) res = await resp.json();
                } catch (e1) {
                    const resp = await fetch(`/cgi-bin/jacktor-api?action=auth_login&user=${encodeURIComponent(u)}&pass=${encodeURIComponent(p)}`);
                    if (resp.ok) res = await resp.json();
                }

                if (res && res.success && res.token) {
                    sessionStorage.setItem('jacktor_token', res.token);
                    if (remember) {
                        localStorage.setItem('jacktor_token', res.token);
                    } else {
                        localStorage.removeItem('jacktor_token');
                    }
                    // Redirect directly to dashboard
                    window.location.replace('/');
                } else {
                    if (errorBox) {
                        const errText = document.getElementById('login-error-text');
                        if (errText && res && res.error) {
                            errText.innerText = res.error;
                        } else if (errText) {
                            errText.innerText = dict.err_invalid;
                        }
                        errorBox.style.display = 'flex';
                    }
                }
            } catch (err) {
                if (errorBox) {
                    const errText = document.getElementById('login-error-text');
                    if (errText) errText.innerText = "Error: " + err.message;
                    errorBox.style.display = 'flex';
                }
            } finally {
                if (submitBtn) {
                    submitBtn.disabled = false;
                    submitBtn.innerHTML = `<i class="fa-solid fa-arrow-right-to-bracket"></i> <span id="lbl-login-btn">${dict.btn}</span>`;
                }
            }
        }

        // Initialized automatically by i18n.js
