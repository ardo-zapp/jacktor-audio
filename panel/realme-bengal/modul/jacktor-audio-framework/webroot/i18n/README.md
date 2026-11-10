# Jacktor Audio Framework • Internationalization (i18n)

This directory contains modular translation files for the Jacktor Audio Framework Webroot and Android Panel.

## Adding a New Language / Menambahkan Bahasa Baru

To add a new language, simply create a new `.json` file in this folder (e.g., `ja.json`, `de.json`, `es.json`, `ru.json`).
**No coding or HTML modifications required!** The system automatically detects the new file and immediately lists it in the language selector dropdown on both the Dashboard and the Login screen.

### File Format:
```json
{
  "lang_code": "ja",
  "lang_name": "日本語 (Japanese)",
  "lang_flag": "🇯🇵",
  "title_app": "JACKTOR AUDIO FRAMEWORK",
  "subtitle_app": "...",
  "badge_live": "ONLINE",
  ...
}
```

- `lang_code`: Two-letter ISO language code (e.g. `ja`, `de`, `fr`).
- `lang_name`: Display name of the language shown in the selector.
- `lang_flag`: Optional emoji flag (e.g. `🇯🇵`, `🇩🇪`, `🇫🇷`).
- Other keys: Translation key-value pairs matching `en.json` / `id.json`. If any key is missing, the system gracefully falls back to English.
