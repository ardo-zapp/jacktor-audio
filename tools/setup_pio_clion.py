#!/usr/bin/env python3
# Jacktor Audio - Setup PlatformIO + CLion (auto-scan firmware/*)
# - Idempoten: tiap proyek hanya di-init sekali (stamp file)
# - Tampilkan instruksi CLion: biasanya otomatis, manual hanya jika perlu
# - Opsi --reinit untuk paksa inisialisasi ulang

import argparse
import subprocess
import sys
from pathlib import Path
from shutil import which
from datetime import datetime

WRAPPER_REL = Path("tools/piow")
STAMP_NAME = ".pio-clion.init.stamp"


def run(cmd, cwd=None, ignore_error=False):
    print(f"$ {' '.join(map(str, cmd))}")
    try:
        return subprocess.run(cmd, cwd=cwd, check=True)
    except subprocess.CalledProcessError as e:
        if ignore_error:
            print(f"(info) Command gagal diabaikan: {e}")
            return e
        raise


def has_cmd(name) -> bool:
    return which(name) is not None


def has_pio() -> bool:
    try:
        subprocess.run(["pio", "--version"], check=True, capture_output=True)
        return True
    except Exception:
        return False


def ensure_pipx():
    if has_cmd("pipx"):
        return
    run([sys.executable, "-m", "pip", "install", "--user", "pipx"])


def pipx_venv_pio() -> str | None:
    home = Path.home()
    candidates = [
        home / ".local" / "pipx" / "venvs" / "platformio" / "bin" / "pio",                     # Linux
        home / "AppData" / "Local" / "pipx" / "venvs" / "platformio" / "Scripts" / "pio.exe",  # Windows
    ]
    for c in candidates:
        if c.exists():
            return str(c)
    return None


def ensure_pio() -> str:
    """
    Pastikan PlatformIO tersedia dan berikan instruksi setting di CLion.
    Catatan: CLion biasanya mendeteksi otomatis. Langkah manual hanya jika auto-detect gagal.
    """
    if has_pio():
        path = which("pio") or "pio"
        print(f"PlatformIO CLI terdeteksi: {path}")
        print("\nLangkah di CLion:")
        print("1. Pastikan plugin 'PlatformIO for CLion' sudah terpasang (File → Settings → Plugins).")
        print("2. Buka File → Settings → Languages & Frameworks → PlatformIO")
        print(f"3. Isi 'PlatformIO Core Executable' dengan: {path}")
        print("\n⚙️ Biasanya CLion mendeteksi otomatis. "
              "Langkah manual hanya diperlukan bila CLion belum mengenali PlatformIO.\n")
        # Contoh jalur spesifik (opsional, hanya ilustrasi):
        print("Contoh jalur spesifik (jika sesuai sistem Anda):")
        print("3*. Isi 'PlatformIO Core Executable' dengan: /home/ardo/.local/bin/pio\n")
        return path

    ensure_pipx()
    try:
        run(["pipx", "install", "platformio"])
    except Exception:
        run([sys.executable, "-m", "pipx", "install", "platformio"])

    pio = pipx_venv_pio()
    if pio:
        print(f"PlatformIO terpasang via pipx: {pio}")
        print("\nLangkah di CLion:")
        print("1. Pastikan plugin 'PlatformIO for CLion' sudah terpasang (File → Settings → Plugins).")
        print("2. Buka File → Settings → Languages & Frameworks → PlatformIO")
        print(f"3. Isi 'PlatformIO Core Executable' dengan: {pio}")
        print("\n⚙️ Biasanya CLion mendeteksi otomatis. "
              "Langkah manual hanya diperlukan bila CLion belum mengenali PlatformIO.\n")
        # Contoh jalur spesifik (opsional, hanya ilustrasi):
        print("Contoh jalur spesifik (jika sesuai sistem Anda):")
        print("3*. Isi 'PlatformIO Core Executable' dengan: /home/ardo/.local/bin/pio\n")
        return pio

    raise SystemExit("Gagal menemukan atau memasang PlatformIO.")


def ensure_wrapper(repo_root: Path, pio_cmd: str) -> Path:
    wrapper = repo_root / WRAPPER_REL
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    content = f"""#!/usr/bin/env bash
exec "{pio_cmd}" "$@"
"""
    if not wrapper.exists() or wrapper.read_text() != content:
        wrapper.write_text(content)
        wrapper.chmod(0o755)
        print(f"Wrapper dibuat/diperbarui: {wrapper}")
    else:
        print(f"Wrapper OK: {wrapper}")
    return wrapper


def discover_projects(firmware_root: Path) -> list[Path]:
    """Cari semua subfolder langsung di firmware/ yang mengandung platformio.ini."""
    if not firmware_root.exists():
        return []
    projects = []
    for child in sorted(firmware_root.iterdir()):
        if child.is_dir() and (child / "platformio.ini").exists():
            projects.append(child.resolve())
    return projects


def pio_init_compat(pio_cmd: str, proj: Path):
    """Jalankan 'pio init' (alias lama). Abaikan error jika tidak didukung."""
    run([pio_cmd, "init"], cwd=proj, ignore_error=True)


def pio_project_init_clion(pio_cmd: str, proj: Path):
    """Jalankan 'pio project init --ide clion' (tanpa --force untuk PIO 6+)."""
    run([pio_cmd, "project", "init", "--ide", "clion"], cwd=proj)


def is_already_initialized(proj: Path) -> bool:
    """
    Idempoten per proyek:
    - Jika stamp file ada, anggap sudah di-init.
    - Heuristik tambahan: ada CMakeLists.txt atau folder .pio/ di root proyek.
    """
    stamp = proj / STAMP_NAME
    if stamp.exists():
        return True
    cmake = proj / "CMakeLists.txt"
    pio_dir = proj / ".pio"
    return cmake.exists() or pio_dir.exists()


def write_stamp(proj: Path):
    stamp = proj / STAMP_NAME
    stamp.write_text(f"Initialized at {datetime.now().isoformat()}\n")


def process_project(pio_cmd: str, proj: Path, reinit: bool):
    print(f"\nProyek: {proj}")
    if not (proj / "platformio.ini").exists():
        print("- Skip: tidak ada platformio.ini")
        return

    if not reinit and is_already_initialized(proj):
        print("- Sudah di-init sebelumnya, skip (gunakan --reinit untuk paksa ulang).")
        return

    # Urutan: kompat lama lalu init CLion
    pio_init_compat(pio_cmd, proj)
    pio_project_init_clion(pio_cmd, proj)
    write_stamp(proj)
    print(f"- Init selesai, stamp ditulis -> {proj / STAMP_NAME}")


def repo_root_from_script() -> Path:
    # file ini ada di tools/, repo root = parent dari parent file
    return Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(
        description="Setup otomatis PlatformIO + CLion untuk semua proyek di firmware/* (idempoten)."
    )
    parser.add_argument("--firmware-root", default=None, help="Path ke folder firmware (default: <repo>/firmware)")
    parser.add_argument("--reinit", action="store_true", help="Paksa inisialisasi ulang meski sudah pernah.")
    args = parser.parse_args()

    repo_root = repo_root_from_script()
    firmware_root = Path(args.firmware_root).resolve() if args.firmware_root else (repo_root / "firmware")

    pio_real = ensure_pio()
    wrapper = ensure_wrapper(repo_root, pio_real)
    pio_exec = str(wrapper.resolve())

    projects = discover_projects(firmware_root)
    if not projects:
        print(f"Tidak ditemukan proyek PlatformIO di: {firmware_root}")
        print("Pastikan tiap subfolder memiliki 'platformio.ini'.")
    else:
        for proj in projects:
            process_project(pio_exec, proj, reinit=args.reinit)

    print("\nSetup selesai.")
    print("Di CLion:")
    print("- Pastikan plugin 'PlatformIO for CLion' terpasang dan aktif.")
    print("- CLion biasanya mendeteksi otomatis lokasi PlatformIO.")
    print("- Jika perlu set manual: Buka File → Settings → Languages & Frameworks → PlatformIO")
    print(f"  lalu isi 'PlatformIO Core Executable' dengan: {pio_real}")
    print("  (contoh spesifik: /home/ardo/.local/bin/pio bila itu jalur pio Anda)")
    print("- Buka proyek dari folder yang berisi platformio.ini (mis. firmware/amplifier atau firmware/bridge).")


if __name__ == "__main__":
    main()
