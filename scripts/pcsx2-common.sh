# Shared PCSX2 setup, sourced by run-pcsx2.sh and run-vvvvvv-pcsx2.sh.
# Expects REPO and PCSX2_BIOS; defines APPRUN, DATA, INI, TIMEOUT and applies
# the settings evidence collection relies on (isolated data dir, host fs, logs).
PCSX2_VER=v2.8.2
PCSX2_SHA256=0c46bb6a88aa2782b10853a7b07cf3387ba99cbef2b966372cd2315b8571abea
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/vvvvvv-ps2/pcsx2-$PCSX2_VER"
DATA="$CACHE/data"
TIMEOUT="${PCSX2_TIMEOUT:-180}"

if [[ -z "${PCSX2_BIOS:-}" || ! -f "${PCSX2_BIOS}" ]]; then
    echo "PCSX2_BIOS must point to your own PS2 BIOS dump (e.g. SCPH-7500x)." >&2
    echo "PCSX2 cannot boot anything without it; no BIOS is shipped or downloaded." >&2
    exit 3
fi

if [[ ! -x "$CACHE/squashfs-root/AppRun" ]]; then
    mkdir -p "$CACHE"
    curl -fL -o "$CACHE/pcsx2.AppImage" \
        "https://github.com/PCSX2/pcsx2/releases/download/$PCSX2_VER/pcsx2-$PCSX2_VER-linux-appimage-x64-Qt.AppImage"
    echo "$PCSX2_SHA256  $CACHE/pcsx2.AppImage" | sha256sum -c -
    chmod +x "$CACHE/pcsx2.AppImage"
    (cd "$CACHE" && ./pcsx2.AppImage --appimage-extract >/dev/null)
fi
APPRUN="$CACHE/squashfs-root/AppRun"

INI="$DATA/PCSX2/inis/PCSX2.ini"
if [[ ! -f "$INI" ]]; then
    mkdir -p "$DATA"
    "$APPRUN" -datapath "$DATA" -testconfig >/dev/null 2>&1 || true
fi

# Apply the settings the evidence collection depends on.
python3 -I - "$INI" "$(dirname "$(realpath "$PCSX2_BIOS")")" "$(basename "$PCSX2_BIOS")" "${PCSX2_VIRTUAL_PAD:-0}" "${PCSX2_OSD_FPS:-0}" <<'EOF'
import configparser, sys
ini, bios_dir, bios_file, virtual_pad, osd_fps = sys.argv[1:6]
cp = configparser.ConfigParser(interpolation=None, strict=False)
cp.optionxform = str
cp.read(ini)
def put(sec, key, val):
    if not cp.has_section(sec):
        cp.add_section(sec)
    cp.set(sec, key, val)
put("UI", "SetupWizardIncomplete", "false")
put("UI", "ConfirmShutdown", "false")
put("UI", "StartFullscreen", "false")
put("Folders", "Bios", bios_dir)
put("Filenames", "BIOS", bios_file)
put("EmuCore", "HostFs", "true")
put("EmuCore/GS", "Renderer", "13")          # software GS renderer (deterministic 2D)
# PCSX2_OSD_FPS=1: on-screen FPS (frames the game presents), VPS (emulated
# vsyncs) and emulation speed. Off by default so screenshots stay clean.
for key in ("OsdShowFPS", "OsdShowVPS", "OsdShowSpeed"):
    put("EmuCore/GS", key, "true" if osd_fps == "1" else "false")
put("Logging", "EnableSystemConsole", "true")
put("Logging", "EnableFileLogging", "true")
put("Logging", "EnableEEConsole", "true")
put("Logging", "EnableIOPConsole", "true")
put("Logging", "EnableTimestamps", "true")
# Pad 1: keyboard for people, SDL-0 for scripts/virtual-pad.py
if virtual_pad == "1":
    pad = {"Up": "SDL-0/DPadUp", "Down": "SDL-0/DPadDown", "Left": "SDL-0/DPadLeft", "Right": "SDL-0/DPadRight",
           "Cross": "SDL-0/FaceSouth", "Circle": "SDL-0/FaceEast", "Square": "SDL-0/FaceWest",
           "Triangle": "SDL-0/FaceNorth", "Start": "SDL-0/Start", "Select": "SDL-0/Back",
           "L1": "SDL-0/LeftShoulder", "R1": "SDL-0/RightShoulder",
           "L3": "SDL-0/LeftStick", "R3": "SDL-0/RightStick",
           "LLeft": "SDL-0/-LeftX", "LRight": "SDL-0/+LeftX", "LUp": "SDL-0/-LeftY", "LDown": "SDL-0/+LeftY"}
else:
    pad = {"Up": "Keyboard/Up", "Down": "Keyboard/Down", "Left": "Keyboard/Left", "Right": "Keyboard/Right",
           "Cross": "Keyboard/K", "Circle": "Keyboard/L", "Square": "Keyboard/J", "Triangle": "Keyboard/I",
           "Start": "Keyboard/Return", "Select": "Keyboard/Backspace", "L1": "Keyboard/Q", "R1": "Keyboard/E",
           "L3": "Keyboard/Z", "R3": "Keyboard/X",
           "LLeft": "Keyboard/A", "LRight": "Keyboard/D", "LUp": "Keyboard/W", "LDown": "Keyboard/S"}
put("Pad1", "Type", "DualShock2")
for k, v in pad.items():
    put("Pad1", k, v)
put("InputSources", "SDL", "true")
with open(ini, "w") as f:
    cp.write(f, space_around_delimiters=True)
# PCSX2 reads repeated keys as additional bindings. Bind the virtual pad's
# buttons on SDL-1..3 too: other joystick-like devices (e.g. a wireless
# keyboard dongle) can take SDL-0 first.
if virtual_pad == "1":
    out, section = [], None
    for line in open(ini).read().splitlines():
        out.append(line)
        if line.startswith("["):
            section = line.strip()
        elif section == "[Pad1]" and " = SDL-0/" in line:
            key, val = line.split(" = ", 1)
            out += [f"{key} = SDL-{n}/{val.split('/', 1)[1]}" for n in (1, 2, 3)]
    open(ini, "w").write("\n".join(out) + "\n")
EOF
