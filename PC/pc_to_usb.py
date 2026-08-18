#!/usr/bin/env python
"""
PC resource monitor --> USB serial output
Feature-aligned with pc_to_emqx.py (identical capabilities):

  - ensure_venv(): auto-restart inside project .venv
  - diag_log() + --log-file: diagnostic log to file
  - LHM timing diagnostics: slow Update detection + stale-cache fallback
  - get_disk_io_percent(): psutil read_time/write_time delta (no more wmic deadlock)
  - GPU triple detection: nvidia-smi -> WMI COM -> wmic CLI
  - get_weather(): OpenWeatherMap fetch, embedded in USB stats JSON

Auto-detect serial port:
  * No --port given -> scan for Ameba CDC ACM devices; retry every 2s if none found.
  * One MCU        -> auto-connect.
  * Multiple MCUs  -> list them and ask user to specify --port COMx.
  * --port COMx    -> wait until that port appears, then connect.
  * If the MCU is unplugged -> auto-reconnect when it reappears.
  * Zero CPU waste: time.sleep() between scans.
"""

import sys
import os
import subprocess
import platform
import getpass
import json
import time
import signal
import socket
import datetime as dt  # LHM timing diagnostics
import threading

# ---------- Virtual environment auto-switch ----------
def ensure_venv(venv_dir=".venv"):
    """Re-run this script with the venv Python if not already inside a venv."""
    if sys.prefix != sys.base_prefix:
        return  # Already inside a venv

    if os.name == 'nt':
        python_exe = os.path.join(venv_dir, "Scripts", "python.exe")
    else:
        python_exe = os.path.join(venv_dir, "bin", "python")

    if not os.path.isfile(python_exe):
        print(f"Error: virtual environment not found at {os.path.abspath(venv_dir)}")
        print("Please create a venv and install dependencies:")
        print("  python -m venv .venv")
        if os.name == 'nt':
            print("  .venv\\Scripts\\activate")
        else:
            print("  source .venv/bin/activate")
        print("  pip install pyserial psutil pythonnet")
        sys.exit(1)

    print(f"-> Re-running via venv: {python_exe}")
    result = subprocess.run([python_exe] + sys.argv)
    sys.exit(result.returncode)

ensure_venv()

# ---------- Third-party imports ----------
import psutil
import serial
import serial.tools.list_ports

# ================== Config ==================
# Realtek USB VID + custom CDC ACM PID for auto-detect
# PID 0xF852 distinguishes our firmware from ROM download mode (PID 0xF851).
AMEBA_CDC_VID = {0x0BDA, 0x1D5C}          # Realtek Semiconductor
AMEBA_CDC_PID = 0xF852                     # Our custom CDC ACM PID
DEFAULT_PORT = None                        # None = auto-detect; or "COM3" etc.
BAUDRATE = 115200
PUBLISH_INTERVAL = 1                       # seconds (was 3s; 1s matches UI_UPDATE_INTERVAL_MS)

# Full LHM sensor output (USB version: enabled by default)
LHM_FULL_DATA = True

# ---------- Weather (OpenWeatherMap) ----------
# Coordinate-based query (lat/lon) for accurate district-level weather.
# City display name auto-detected from API response's "name" field.
# Set WEATHER_CITY_OVERRIDE to override auto-detected name.
# Weather is embedded in the main stats JSON sent over USB.
# Set WEATHER_ENABLED = False when MCU has WEATHER_FETCH_MCU=1 (MCU HTTP fetches).
_WEATHER_CACHE = None
_WEATHER_CACHE_TIME = 0
_WEATHER_CACHE_TTL = 600          # 10 minutes
WEATHER_API_KEY = "YOUR_OPENWEATHERMAP_API_KEY"  # Get free key at https://openweathermap.org/api
WEATHER_LAT = 31.34               # Latitude  (e.g., Gusu District, Suzhou)
WEATHER_LON = 120.61              # Longitude
WEATHER_CITY_OVERRIDE = ""        # Optional: empty = use API-returned "name"
WEATHER_ENABLED = True           # True = enable OpenWeatherMap API fetch (requires valid WEATHER_API_KEY)

# ---------- MQTT SHT3X subscriber (background thread) ----------
# In USB CDC mode (CONFIG_USB_CDC_MODE), the MCU has no WiFi.
# The PC subscribes to MQTT topic "humiture/measurement" on behalf of the
# MCU and forwards the latest SHT3X reading through USB JSON so the MCU
# can display it without a network connection.
try:
    import paho.mqtt.client as mqtt
    _HAVE_SHT3X_MQTT = True
except ImportError:
    _HAVE_SHT3X_MQTT = False
    print("[INFO] paho-mqtt not installed. SHT3X data will not be available via USB.")
    print("       Install: pip install paho-mqtt")

_SHT3X_CACHE = {}
_SHT3X_LOCK = threading.Lock()

# ---------- Weather send suppression (align with pc_to_emqx.py pattern) ----------
# Track the last weather snapshot sent over USB.  Weather data changes slowly
# (API fetch interval is 10 minutes); embedding it in every 1s frame is wasteful.
# Only include weather fields in the stats JSON when the snapshot actually changes.
_USB_WEATHER_SENT = None
# =========================================

def _sht3x_on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode('utf-8'))
        with _SHT3X_LOCK:
            _SHT3X_CACHE.clear()
            _SHT3X_CACHE.update(data)
    except Exception:
        pass

def _sht3x_mqtt_loop():
    """Connect to EMQX broker, subscribe to humiture/measurement, loop forever."""
    if not _HAVE_SHT3X_MQTT:
        return
    client = mqtt.Client()
    client.username_pw_set("your-username", "your-passwd")
    client.tls_set()
    client.on_message = _sht3x_on_message
    try:
        client.connect("your.emqxsl.cn", 8883, 60)
        client.subscribe("humiture/measurement", qos=0)
        print("[SHT3X-MQTT] Subscribed to humiture/measurement (background)")
        client.loop_forever()
    except Exception as e:
        print(f"[SHT3X-MQTT] Connection failed: {e}")

def start_sht3x_subscriber():
    """Launch the MQTT subscriber in a daemon thread."""
    t = threading.Thread(target=_sht3x_mqtt_loop, daemon=True)
    t.start()

def get_sht3x_data():
    """Return a copy of the latest SHT3X reading, or None."""
    if not _HAVE_SHT3X_MQTT:
        return None
    with _SHT3X_LOCK:
        return dict(_SHT3X_CACHE) if _SHT3X_CACHE else None
# =========================================

DEBUG_MODE = "--debug" in sys.argv or "-d" in sys.argv

# Verbose per-cycle logging (default OFF). Controls [SENT] byte-count prints
# and similar high-frequency messages that add little value at 1s intervals.
# Set to True only when debugging data transmission volume.
VERBOSE_LOG = False

# ---------- Diagnostic log file (optional, --log-file <path>) ----------
# Lightweight: append-only diag messages, does not replace normal print
_LOG_FILE_PATH = None
for _i, _arg in enumerate(sys.argv):
    if _arg == "--log-file" and _i + 1 < len(sys.argv):
        _LOG_FILE_PATH = sys.argv[_i + 1]
        break


def diag_log(msg):
    """Print diagnostic message and optionally append to log file."""
    print(msg)
    if _LOG_FILE_PATH:
        try:
            with open(_LOG_FILE_PATH, "a", encoding="utf-8") as _f:
                _f.write(f"[{dt.datetime.now().strftime('%H:%M:%S.%f')[:12]}] {msg}\n")
        except Exception:
            pass


# ---------- Libre Hardware Monitor path (Windows only) ----------
LHM_DIR = None

def _find_lhm_dir():
    """Dynamically locate Libre Hardware Monitor installation directory."""
    try:
        proc = subprocess.run(
            ["wmic", "process", "where", "name='LibreHardwareMonitor.exe",
             "get", "ExecutablePath", "/format:csv"],
            capture_output=True, text=True, timeout=3
        )
        for line in proc.stdout.strip().split('\n')[1:]:
            if 'LibreHardwareMonitor.exe' in line and ',,' not in line:
                parts = line.split(',')
                for p in parts:
                    if p.strip().endswith('LibreHardwareMonitor.exe'):
                        return os.path.dirname(p.strip())
    except Exception:
        pass

    default = os.path.expandvars(r"%USERPROFILE%\Downloads\LibreHardwareMonitor")
    if os.path.isfile(os.path.join(default, "LibreHardwareMonitorLib.dll")):
        return default

    return None

running = True

def set_exit_flag(sig, frame):
    global running
    running = False

signal.signal(signal.SIGINT, set_exit_flag)
if hasattr(signal, 'SIGTERM'):
    signal.signal(signal.SIGTERM, set_exit_flag)


# ---------- Lock screen detection (cross-platform) ----------
class ScreenLockDetector:
    """Cross-platform lock screen detection (Windows + Linux)."""
    def __init__(self):
        self._prev_locked = False

    def is_locked(self):
        """Return True if screen is locked, False otherwise."""
        if sys.platform == 'win32':
            return self._check_windows()
        elif sys.platform.startswith('linux'):
            return self._check_linux()
        return False

    def _check_windows(self):
        """Check for LogonUI.exe process."""
        for proc in psutil.process_iter(['name']):
            try:
                if proc.info['name'] == 'LogonUI.exe':
                    return True
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
        return False

    def _check_linux(self):
        """Check if current user session is locked via loginctl CLI (zero extra deps)."""
        try:
            my_uid = os.getuid()
            result = subprocess.run(
                ["loginctl", "list-sessions", "--no-legend"],
                capture_output=True, text=True, timeout=3
            )
            if result.returncode != 0:
                return False
            my_session_id = None
            for line in result.stdout.strip().split('\n'):
                parts = line.split()
                if len(parts) >= 3:
                    try:
                        if int(parts[1]) == my_uid:
                            my_session_id = parts[0]
                            break
                    except ValueError:
                        continue
            if my_session_id is None:
                return False
            result = subprocess.run(
                ["loginctl", "show-session", my_session_id, "-p", "LockedHint"],
                capture_output=True, text=True, timeout=3
            )
            return 'LockedHint=yes' in result.stdout
        except Exception as e:
            diag_log(f"[LOCK-DIAG] _check_linux failed: {e}")
            return False

    def has_state_changed(self, currently_locked):
        changed = currently_locked != self._prev_locked
        self._prev_locked = currently_locked
        return changed

# ---------- Network rate calculation ----------
net_io_prev = None
net_time_prev = None

def get_network_rate():
    global net_io_prev, net_time_prev
    net = psutil.net_io_counters()
    now = time.time()
    if net_io_prev is None or net_time_prev is None:
        net_io_prev = (net.bytes_sent, net.bytes_recv)
        net_time_prev = now
        return 0.0, 0.0
    delta_sent = net.bytes_sent - net_io_prev[0]
    delta_recv = net.bytes_recv - net_io_prev[1]
    delta_t = now - net_time_prev
    if delta_t == 0:
        return 0.0, 0.0
    net_io_prev = (net.bytes_sent, net.bytes_recv)
    net_time_prev = now
    upload = delta_sent / delta_t / 1024.0
    download = delta_recv / delta_t / 1024.0
    return round(upload, 1), round(download, 1)

# ---------- CPU temperature (cross-platform) ----------
# Linux: psutil native sensors_temperatures()
# Windows: LHM DLL provides more accurate CPU Package temperature
def get_cpu_temp():
    if hasattr(psutil, "sensors_temperatures"):
        temps = psutil.sensors_temperatures()
        if temps:
            for name, entries in temps.items():
                if any(key in name.lower() for key in ('cpu', 'core', 'k10temp')):
                    for entry in entries:
                        if entry.current:
                            return round(entry.current, 1)
    return None


# ---------- Libre Hardware Monitor via pythonnet DLL ----------
_LHM_data_cache = None
_LHM_cache_time = 0
_LHM_CACHE_TTL = 30
_LHM_computer = None  # Persistent Computer object: Open() once, avoid repeated HW enumeration


def get_libre_hardware_monitor_data():
    """
    Load LibreHardwareMonitorLib.dll via pythonnet to collect hardware sensors.
    No LHM process, WMI registration, or HTTP Server required.

    Computer object is Open()ed once and kept resident; subsequent calls only
    call Update() to refresh values, avoiding full HW re-enumeration (which
    triggers Windows device manager refresh).

    Result cached for _LHM_CACHE_TTL seconds.
    """
    global _LHM_data_cache, _LHM_cache_time, LHM_DIR, _LHM_computer

    now = time.time()
    if _LHM_data_cache is not None and (now - _LHM_cache_time) < _LHM_CACHE_TTL:
        return _LHM_data_cache

    if platform.system() != 'Windows':
        return None

    # First time: locate LHM dir, load DLL, create and Open Computer
    if _LHM_computer is None:
        if LHM_DIR is None:
            LHM_DIR = _find_lhm_dir()
            if LHM_DIR is None:
                return None

        try:
            import clr
        except ImportError:
            return None

        dll_path = os.path.join(LHM_DIR, "LibreHardwareMonitorLib.dll")
        if not os.path.isfile(dll_path):
            return None

        try:
            sys.path.insert(0, LHM_DIR)
            clr.AddReference(os.path.join(LHM_DIR, "LibreHardwareMonitorLib.dll"))
            from LibreHardwareMonitor.Hardware import Computer

            computer = Computer()
            computer.IsCpuEnabled = True
            computer.IsGpuEnabled = True
            computer.IsMotherboardEnabled = True
            computer.IsStorageEnabled = True
            computer.IsBatteryEnabled = True
            computer.IsPsuEnabled = True
            computer.IsNetworkEnabled = True
            computer.Open()

            _LHM_computer = computer
            print("[LHM] Computer initialised (persistent, single Open)")
        except Exception as e:
            print(f"[LHM] Computer init failed: {e}")
            return None

    # Subsequent calls: only Update() to refresh values, no re-Open
    try:
        _lhm_update_start = time.time()

        result = {
            "temps": {},
            "loads": {},
            "clocks": {},
            "fans": {},
            "voltages": {},
            "powers": {},
            "others": {},
        }

        for hardware in _LHM_computer.Hardware:
            hardware.Update()
            _lhm_update_elapsed = time.time() - _lhm_update_start
            if _lhm_update_elapsed > 3.0:
                diag_log(f"[LHM-DIAG] hardware.Update() slow: {_lhm_update_elapsed:.1f}s (hardware={hardware.Name})")
            hw_name = str(hardware.Name)

            for sensor in hardware.Sensors:
                value = sensor.Value
                if value is None:
                    continue

                sensor_type = str(sensor.SensorType)
                sensor_name = str(sensor.Name)
                label = f"{hw_name}: {sensor_name}"

                try:
                    val = float(value)
                except (ValueError, TypeError):
                    continue

                if sensor_type == 'Temperature':
                    result["temps"][label] = round(val, 1)
                elif sensor_type == 'Load':
                    result["loads"][label] = round(val, 1)
                elif sensor_type == 'Clock':
                    result["clocks"][label] = int(round(val))
                elif sensor_type == 'Fan':
                    result["fans"][label] = int(round(val))
                elif sensor_type == 'Voltage':
                    result["voltages"][label] = round(val, 3)
                elif sensor_type == 'Power':
                    result["powers"][label] = round(val, 2)
                else:
                    result["others"][label] = round(val, 2)

            for sub in hardware.SubHardware:
                sub.Update()
                sub_name = str(sub.Name)
                for sensor in sub.Sensors:
                    value = sensor.Value
                    if value is None:
                        continue

                    sensor_type = str(sensor.SensorType)
                    sensor_name = str(sensor.Name)
                    label = f"{sub_name}: {sensor_name}"

                    try:
                        val = float(value)
                    except (ValueError, TypeError):
                        continue

                    if sensor_type == 'Temperature':
                        result["temps"][label] = round(val, 1)
                    elif sensor_type == 'Load':
                        result["loads"][label] = round(val, 1)
                    elif sensor_type == 'Clock':
                        result["clocks"][label] = int(round(val))
                    elif sensor_type == 'Fan':
                        result["fans"][label] = int(round(val))
                    elif sensor_type == 'Voltage':
                        result["voltages"][label] = round(val, 3)
                    elif sensor_type == 'Power':
                        result["powers"][label] = round(val, 2)
                    else:
                        result["others"][label] = round(val, 2)

        total = sum(len(v) for v in result.values())
        if total == 0:
            _lhm_total_elapsed = time.time() - _lhm_update_start
            diag_log(f"[LHM-DIAG] Update returned 0 sensors after {_lhm_total_elapsed:.1f}s -- returning None")
            return None

        _lhm_total_elapsed = time.time() - _lhm_update_start
        _LHM_data_cache = result
        _LHM_cache_time = now
        if _lhm_total_elapsed > 3.0:
            diag_log(f"[LHM-DIAG] Update completed in {_lhm_total_elapsed:.1f}s, {total} sensors")
        print(f"[LHM] Update refreshed {total} sensor values")
        return result

    except Exception as e:
        _lhm_fail_elapsed = time.time() - _lhm_update_start
        print(f"[LHM] Update exception ({_lhm_fail_elapsed:.1f}s): {e}")
        # Cache expired but update failed: keep stale cache, retry on next cycle
        _LHM_cache_time = now
        return _LHM_data_cache  # Return stale cache as fallback


# ---------- GPU backfill: fill unavailable fields from LHM data ----------
def _backfill_gpu_from_lhm(gpu, lhm):
    if gpu is None or lhm is None:
        return gpu

    gpu_name = gpu.get("name") or ""
    changed = False

    if "NVIDIA" not in gpu_name.upper():
        if gpu.get("usage") is None and lhm["loads"]:
            for key, val in lhm["loads"].items():
                if "D3D 3D" in key:
                    gpu["usage"] = round(val, 1)
                    changed = True
                    break

        if gpu.get("mem_used_mb") is None and lhm["others"]:
            for key, val in lhm["others"].items():
                if "D3D Shared Memory Used" in key:
                    gpu["mem_used_mb"] = round(val, 0)
                    changed = True
                    break

        if gpu.get("temp_c") is None and lhm["temps"]:
            for key, val in lhm["temps"].items():
                if "gpu" in key.lower():
                    gpu["temp_c"] = val
                    changed = True
                    break

    if changed:
        print(f"[GPU-BF] LHM backfill: usage={gpu.get('usage')}, mem={gpu.get('mem_used_mb')}, temp={gpu.get('temp_c')}")

    return gpu


# ---------- GPU info collection (cross-platform, cached) ----------
_gpu_info_cache = None

def get_gpu_info():
    global _gpu_info_cache
    if _gpu_info_cache is not None:
        return _gpu_info_cache

    result = None

    # Strategy 1: nvidia-smi (NVIDIA GPU, Windows/Linux)
    try:
        proc = subprocess.run(
            ["nvidia-smi",
             "--query-gpu=name,utilization.gpu,memory.used,memory.total,temperature.gpu",
             "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=3
        )
        if proc.returncode == 0 and proc.stdout.strip():
            line = proc.stdout.strip().split('\n')[0]
            parts = [p.strip() for p in line.split(',')]
            if len(parts) >= 5:
                gpu_name = parts[0]
                try:
                    gpu_usage = float(parts[1])
                except ValueError:
                    gpu_usage = None
                try:
                    gpu_mem_used = float(parts[2])
                except ValueError:
                    gpu_mem_used = None
                try:
                    gpu_mem_total = float(parts[3])
                except ValueError:
                    gpu_mem_total = None
                try:
                    gpu_temp = float(parts[4])
                except ValueError:
                    gpu_temp = None
                result = {
                    "name": gpu_name,
                    "usage": gpu_usage,
                    "mem_used_mb": gpu_mem_used,
                    "mem_total_mb": gpu_mem_total,
                    "temp_c": gpu_temp,
                }
    except Exception:
        pass

    # Strategy 2: Windows WMI (Intel/AMD integrated GPU)
    if result is None and platform.system() == 'Windows':
        try:
            import wmi
            w = wmi.WMI()
            gpus = w.Win32_VideoController()
            if gpus and len(gpus) > 0:
                gpu = gpus[0]
                name = str(gpu.Name) if gpu.Name else None
                total_mb = None
                if hasattr(gpu, 'AdapterRAM') and gpu.AdapterRAM:
                    total_mb = round(float(gpu.AdapterRAM) / 1048576, 0)
                if name:
                    result = {
                        "name": name,
                        "usage": None,
                        "mem_used_mb": None,
                        "mem_total_mb": total_mb,
                        "temp_c": None,
                    }
                    print(f"[GPU-DBG] WMI detected: {name}")
        except Exception as e:
            print(f"[GPU-DBG] WMI module failed: {e}")

    # Strategy 3: wmic CLI (fallback for Strategy 2)
    if result is None and platform.system() == 'Windows':
        try:
            proc = subprocess.run(
                ["wmic", "path", "Win32_VideoController",
                 "get", "Name,AdapterRAM", "/format:csv"],
                capture_output=True, text=True, timeout=5
            )
            if proc.returncode == 0 and proc.stdout.strip():
                lines = [l.strip() for l in proc.stdout.strip().split('\n') if l.strip()]
                if len(lines) >= 2:
                    parts = lines[1].split(',')
                    if len(parts) >= 2 and parts[1]:
                        name = parts[1]
                        total_mb = None
                        if len(parts) >= 3 and parts[2].isdigit():
                            total_mb = round(int(parts[2]) / 1048576, 0)
                        result = {
                            "name": name,
                            "usage": None,
                            "mem_used_mb": None,
                            "mem_total_mb": total_mb,
                            "temp_c": None,
                        }
                        print(f"[GPU-DBG] wmic detected: {name}")
        except Exception as e:
            print(f"[GPU-DBG] wmic failed: {e}")

    _gpu_info_cache = result
    return result

# ---------- Disk I/O utilization (psutil delta, no wmic deadlock) ----------
_disk_io_cache = None
_disk_io_prev_time = 0.0
_disk_io_prev_read_ms = 0
_disk_io_prev_write_ms = 0


def get_disk_io_percent():
    """
    Return disk I/O utilization percentage.
    Windows: calculated from psutil.disk_io_counters() read_time/write_time deltas.
             Pure Python local call, no external process, no timeout deadlock risk.
    Linux:   Returns None (not yet implemented).
    """
    if platform.system() != 'Windows':
        return None

    global _disk_io_cache, _disk_io_prev_time
    global _disk_io_prev_read_ms, _disk_io_prev_write_ms

    now = time.time()
    try:
        io = psutil.disk_io_counters()
        read_ms = io.read_time
        write_ms = io.write_time
    except Exception:
        return _disk_io_cache

    # First call: initialise sampling baseline, return cached value (may be None)
    if _disk_io_prev_time == 0.0:
        _disk_io_prev_time = now
        _disk_io_prev_read_ms = read_ms
        _disk_io_prev_write_ms = write_ms
        return _disk_io_cache

    delta_wall_ms = (now - _disk_io_prev_time) * 1000.0
    if delta_wall_ms <= 0:
        return _disk_io_cache

    delta_read = read_ms - _disk_io_prev_read_ms
    delta_write = write_ms - _disk_io_prev_write_ms

    _disk_io_prev_time = now
    _disk_io_prev_read_ms = read_ms
    _disk_io_prev_write_ms = write_ms

    # Read and write may overlap; take max as conservative busy-time estimate
    delta_busy = max(delta_read, delta_write)
    percent = min(100.0, round(delta_busy / delta_wall_ms * 100.0, 1))
    _disk_io_cache = percent
    return percent


# ---------- Root partition usage (cross-platform) ----------
def get_disk_usage_percent():
    try:
        if platform.system() == 'Windows':
            return psutil.disk_usage(os.environ.get('SystemDrive', 'C:') + '\\').percent
        return psutil.disk_usage('/').percent
    except Exception:
        return psutil.disk_usage('/').percent


# ---------- Weather (OpenWeatherMap API, 10-min cache) ----------
def get_weather():
    """
    Fetch current weather from OpenWeatherMap API via coordinate query (lat/lon).
    Cached for _WEATHER_CACHE_TTL seconds (600s = 10 min).
    City name auto-detected from API response; override via WEATHER_CITY_OVERRIDE.
    Returns a dict or None on failure.
    """
    global _WEATHER_CACHE, _WEATHER_CACHE_TIME
    now = time.time()

    if _WEATHER_CACHE is not None and (now - _WEATHER_CACHE_TIME) < _WEATHER_CACHE_TTL:
        return _WEATHER_CACHE

    if not WEATHER_ENABLED:
        return None

    try:
        import json as _json
        import urllib.request
        url = (
            f"https://api.openweathermap.org/data/2.5/weather"
            f"?lat={WEATHER_LAT}&lon={WEATHER_LON}"
            f"&appid={WEATHER_API_KEY}&units=metric"
        )
        resp = urllib.request.urlopen(url, timeout=10)
        data = _json.loads(resp.read().decode('utf-8'))

        city_name = data["name"]
        if WEATHER_CITY_OVERRIDE:
            city_name = WEATHER_CITY_OVERRIDE

        _WEATHER_CACHE = {
            "temp_c": data["main"]["temp"],
            "humidity": data["main"]["humidity"],
            "wind_speed": data["wind"]["speed"],
            "condition_code": data["weather"][0]["id"],
            "main": data["weather"][0]["main"],
            "description": data["weather"][0]["description"],
            "city": city_name,
            "country": data["sys"]["country"],
        }
        _WEATHER_CACHE_TIME = now
        print(f"[WEATHER] Updated: {_WEATHER_CACHE['city']}, "
              f"{_WEATHER_CACHE['description']}, "
              f"{_WEATHER_CACHE['temp_c']:.1f}°C, "
              f"{_WEATHER_CACHE['humidity']}%")
        return _WEATHER_CACHE
    except Exception as e:
        print(f"[WEATHER] Fetch failed: {e}")
        return _WEATHER_CACHE


# ---------- Current user (multi-level fallback) ----------
def get_current_user():
    try:
        users = psutil.users()
        if users:
            return users[0].name
    except Exception:
        pass
    for var in ('USER', 'LOGNAME', 'USERNAME'):
        user = os.environ.get(var)
        if user:
            return user
    try:
        return os.getlogin()
    except Exception:
        pass
    try:
        return getpass.getuser()
    except Exception:
        pass
    return "unknown"


# ---------- System stats collection ----------
_STATS_DIAG_LAST_LOG = 0.0
def get_system_stats():
    global _STATS_DIAG_LAST_LOG, _USB_WEATHER_SENT
    _t0 = time.time()
    cpu = psutil.cpu_percent(interval=0)
    mem = psutil.virtual_memory()
    mem_percent = mem.percent
    mem_total_bytes = mem.total
    mem_used_bytes = mem.used
    disk = get_disk_usage_percent()
    upload, download = get_network_rate()
    boot_time = int(psutil.boot_time())
    process_count = len(psutil.pids())

    cpu_cores_logical = psutil.cpu_count(logical=True) or 0
    cpu_cores_physical = psutil.cpu_count(logical=False) or 0
    current_user = get_current_user()

    battery = psutil.sensors_battery()
    battery_percent = round(battery.percent, 2) if battery else None
    battery_plugged = battery.power_plugged if battery else None

    cpu_temp = get_cpu_temp()

    disk_io = psutil.disk_io_counters()
    disk_read_bytes = disk_io.read_bytes if disk_io else 0
    disk_write_bytes = disk_io.write_bytes if disk_io else 0

    # CPU frequency
    cpu_freq = psutil.cpu_freq()
    cpu_freq_current = round(cpu_freq.current, 0) if cpu_freq else None
    cpu_freq_min = round(cpu_freq.min, 0) if cpu_freq and cpu_freq.min > 0 else None
    cpu_freq_max = round(cpu_freq.max, 0) if cpu_freq and cpu_freq.max > 0 else None

    # Hostname / OS
    hostname = socket.gethostname()
    os_platform = platform.platform()

    # Swap
    swap = psutil.swap_memory()
    swap_total = swap.total if swap else 0
    swap_used = swap.used if swap else 0
    swap_percent = round(swap.percent, 1) if swap else 0.0

    # GPU
    gpu = get_gpu_info()
    disk_io_percent = get_disk_io_percent()

    # LHM sensors
    lhm = get_libre_hardware_monitor_data()

    # CPU temperature: match LHM sensor name by priority
    # Core Average -> CPU Package -> Core Max -> CPU Die -> CPU Cores -> CPU Total
    # Core Average is the most sensible metric: average across all cores,
    # avoiding Core Max being skewed by single-core spikes.
    if lhm is not None and lhm["temps"]:
        cpu_keywords = ("Core Average", "CPU Package", "Core Max",
                        "CPU Die", "CPU Cores", "CPU Total")
        for kw in cpu_keywords:
            matched = [k for k in lhm["temps"] if kw in k]
            if matched:
                cpu_temp = lhm["temps"][matched[0]]
                break

    gpu = _backfill_gpu_from_lhm(gpu, lhm)

    # Weather (OpenWeatherMap, 10-min cache) -- only embed in USB JSON when changed,
    # aligning with pc_to_emqx.py's publish_weather_if_changed() pattern.
    weather = get_weather()
    if WEATHER_ENABLED and weather is not None:
        _weather_snap = {
            "weather_temp_c":         weather["temp_c"],
            "weather_humidity":       weather["humidity"],
            "weather_wind_speed":     weather["wind_speed"],
            "weather_condition_code": weather["condition_code"],
            "weather_main":           weather["main"],
            "weather_description":    weather["description"],
            "weather_city":           weather["city"],
        }
    else:
        _weather_snap = None

    _elapsed = time.time() - _t0
    if _elapsed > 3.0:
        diag_log(f"[DIAG] get_system_stats() took {_elapsed:.1f}s")
        _STATS_DIAG_LAST_LOG = time.time()
    elif _elapsed > 0.5 and (time.time() - _STATS_DIAG_LAST_LOG) > 60:
        diag_log(f"[DIAG] get_system_stats() took {_elapsed:.1f}s")
        _STATS_DIAG_LAST_LOG = time.time()

    stats = {
        "cpu": cpu,
        "mem": mem_percent,
        "mem_total": mem_total_bytes,
        "mem_used": mem_used_bytes,
        "disk": disk,
        "net_upload_kbps": upload,
        "net_download_kbps": download,
        "cpu_temp": cpu_temp if cpu_temp is not None else None,
        "boot_time": boot_time,
        "process_count": process_count,
        "cpu_cores_logical": cpu_cores_logical,
        "cpu_cores_physical": cpu_cores_physical,
        "current_user": current_user,
        "battery_percent": battery_percent,
        "battery_plugged": battery_plugged,
        "disk_read_bytes": disk_read_bytes,
        "disk_write_bytes": disk_write_bytes,
        "timestamp": int(time.time()),

        "cpu_freq_current": cpu_freq_current,
        "cpu_freq_min": cpu_freq_min,
        "cpu_freq_max": cpu_freq_max,
        "hostname": hostname,
        "os_platform": os_platform,
        "swap_total": swap_total,
        "swap_used": swap_used,
        "swap_percent": swap_percent,
        "gpu_name": gpu["name"] if gpu else None,
        "gpu_usage": gpu["usage"] if gpu else None,
        "gpu_mem_used_mb": gpu["mem_used_mb"] if gpu else None,
        "gpu_mem_total_mb": gpu["mem_total_mb"] if gpu else None,
        "gpu_temp_c": gpu["temp_c"] if gpu else None,
        "disk_io_percent": disk_io_percent,

        # Weather fields ¡ª only include when the snapshot changed, matching
        # pc_to_emqx.py's publish_weather_if_changed() pattern.
        **(_weather_snap if (_weather_snap is not None
                             and _weather_snap != _USB_WEATHER_SENT) else {}),

        **({"lhm": lhm} if LHM_FULL_DATA else {}),
    }

    # Update weather sentinel when weather data was emitted
    if _weather_snap is not None and _weather_snap != _USB_WEATHER_SENT:
        _USB_WEATHER_SENT = _weather_snap

    # SHT3X sensor data (PC forwards from MQTT humiture/measurement topic)
    sht3x_data = get_sht3x_data()
    if sht3x_data:
        stats["sht3x_temperature"]   = sht3x_data.get("temperature_C")
        stats["sht3x_temperature_f"] = sht3x_data.get("temperature_F")
        stats["sht3x_humidity"]      = sht3x_data.get("humidity")

    return stats


# ---------- Debug mode ----------
def debug_loop():
    print("[DEBUG] Local debug mode, print data only, no serial output.")
    while running:
        stats = get_system_stats()
        print(json.dumps(stats, indent=2, ensure_ascii=False))
        remaining = PUBLISH_INTERVAL - 1
        if remaining > 0:
            for _ in range(int(remaining)):
                if not running:
                    break
                time.sleep(1)


# ---------- USB serial send ----------
def list_serial_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("[WARN] No serial ports found.")
        return
    print("Available ports:")
    for p in ports:
        tag = " <-- Ameba CDC" if _is_ameba_cdc_port(p) else ""
        print(f"  {p.device} - {p.description}{tag}")


def _is_ameba_cdc_port(port):
    """Return True if `port` (ListPortInfo) is our custom CDC ACM device.

    Requires both VID (Realtek 0x0BDA / 0x1D5C) AND our private PID
    (AMEBA_CDC_PID = 0xF852) to avoid matching the ROM download mode
    port (which has the same VID but PID 0xF851).
    """
    if port.vid is not None and port.pid is not None:
        if port.vid in AMEBA_CDC_VID and port.pid == AMEBA_CDC_PID:
            return True
    return False


def _find_ameba_ports():
    """Return a list of ListPortInfo for detected Ameba CDC ACM ports."""
    return [p for p in serial.tools.list_ports.comports() if _is_ameba_cdc_port(p)]


def wait_for_serial(port=None, baudrate=115200, retry_interval=2):
    """
    Wait for and open an Ameba CDC ACM serial port.

    Behaviour:
      * If a specific ``port`` (e.g. ``"COM3"``) is given — wait until that
        COM port appears, then open it.
      * If ``port`` is ``None`` — auto-detect:
          -  0 Ameba ports found → retry every ``retry_interval`` seconds.
          -  1 found             → auto-connect, no user action needed.
          -  ≥2 found            → list them and remind user to pass ``--port``.
      * Loop exits when a port is successfully opened or ``running`` becomes False.
      * Returns a ``serial.Serial`` instance, or ``None`` on cancellation.
    """
    last_scan_msg = 0.0
    list_shown = False

    while running:
        if port:
            # Specific port requested — wait for it to appear
            available = [p for p in serial.tools.list_ports.comports()
                         if p.device == port]
            if available:
                break
            if not list_shown:
                print(f"[WAIT] Port {port} not found. Waiting...")
                list_serial_ports()
                list_shown = True
        else:
            # Auto-detect mode
            ameba_ports = _find_ameba_ports()

            if len(ameba_ports) == 1:
                port = ameba_ports[0].device
                print(f"[INFO] Auto-detected Ameba CDC on {port}")
                break

            if len(ameba_ports) == 0 and (time.time() - last_scan_msg) > 8:
                print("[WAIT] No Ameba CDC device detected. Plug the MCU via USB.")
                print("       Use --list to see all serial ports, or --port COMx to specify.")
                last_scan_msg = time.time()
                list_shown = False

            if len(ameba_ports) >= 2 and not list_shown:
                print(f"[WARN] Found {len(ameba_ports)} Ameba CDC devices."
                      " Use --port COMx to select one:")
                for p in ameba_ports:
                    print(f"       {p.device} - {p.description}")
                list_shown = True

        # Sleep (check running flag every 1 s)
        for _ in range(retry_interval):
            if not running:
                return None
            time.sleep(1)

    # Open the port
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"[INFO] Serial {port} opened, baudrate {baudrate}")
        return ser
    except PermissionError:
        print(f"[ERROR] Permission denied on {port}.")
        if not sys.platform.startswith("win"):
            print("       On Linux, add your user to the 'dialout' group and re-login:")
            print("         sudo usermod -a -G dialout $USER")
        list_serial_ports()
        return None
    except serial.SerialException as e:
        print(f"[ERROR] Cannot open serial {port}: {e}")
        list_serial_ports()
        return None


def usb_loop(ser):
    """Send stats and events over an already-open Serial object."""
    lock_detector = ScreenLockDetector()
    LOCK_CHECK_INTERVAL = 1.0

    g_stats_count = 0
    g_last_complete = time.time()
    was_locked_before = False
    fresh_connect = True

    # Reset weather sentinel so cached weather is re-sent after reconnect
    global _USB_WEATHER_SENT
    _USB_WEATHER_SENT = None

    while running:
        try:
            cycle_start = time.time()
            currently_locked = lock_detector.is_locked()

            # State transition -> send event via serial immediately
            if lock_detector.has_state_changed(currently_locked):
                event = json.dumps({"event": "lock" if currently_locked else "unlock",
                                    "timestamp": int(time.time())},
                                    ensure_ascii=False) + '\n'
                ser.write(event.encode('utf-8'))
                print(f"[EVENT] {'LOCK' if currently_locked else 'UNLOCK'} -> serial")

            # Skip HW collection while locked (except one reconnect burst)
            if not currently_locked or fresh_connect:
                fresh_connect = False
                stats = get_system_stats()
                payload = json.dumps(stats, ensure_ascii=False) + '\n'
                ser.write(payload.encode('utf-8'))
                now = time.time()
                cycle_elapsed = now - cycle_start
                idle_since_last = now - g_last_complete
                g_stats_count += 1
                if cycle_elapsed > 3.0:
                    diag_log(f"[DIAG] Send cycle took {cycle_elapsed:.1f}s (interval {idle_since_last:.0f}s, #{g_stats_count})")
                if not was_locked_before and idle_since_last > 6.0:
                    diag_log(f"[DIAG] Send interval anomaly: {idle_since_last:.0f}s (possible stall)")
                was_locked_before = False
                g_last_complete = now
                if VERBOSE_LOG:
                    print(f"[SENT] {len(payload)} bytes")
            else:
                if not was_locked_before:
                    print("[LOCK] Screen locked, skip HW collection")
                was_locked_before = True

            # Fine-grained sleep: check lock state every LOCK_CHECK_INTERVAL
            # NOTE: inside try block so any SerialException (e.g. MCU unplugged)
            # is caught, triggering reconnection.
            for _ in range(int(PUBLISH_INTERVAL / LOCK_CHECK_INTERVAL)):
                if not running:
                    break
                time.sleep(LOCK_CHECK_INTERVAL)
                new_locked = lock_detector.is_locked()
                if lock_detector.has_state_changed(new_locked):
                    event = json.dumps({"event": "lock" if new_locked else "unlock",
                                        "timestamp": int(time.time())},
                                        ensure_ascii=False) + '\n'
                    ser.write(event.encode('utf-8'))
                    print(f"[EVENT] Fast-detect state change -> {'LOCK' if new_locked else 'UNLOCK'} -> serial")
        except serial.SerialException as e:
            print(f"[ERROR] Serial write exception: {e}")
            break
        except Exception as e:
            print(f"[ERROR] Collection exception: {e}")

    # Send disconnect event on graceful exit so MCU detects it immediately
    event = json.dumps({"event": "disconnect", "timestamp": int(time.time())},
                       ensure_ascii=False) + '\n'
    try:
        ser.write(event.encode('utf-8'))
    except Exception as e:
        print(f"[WARN] Disconnect serial write failed: {e}")

    ser.close()
    print("[INFO] Serial closed")


# ---------- Entry point ----------
if __name__ == "__main__":
    if DEBUG_MODE:
        debug_loop()
        sys.exit(0)

    port = DEFAULT_PORT
    baud = BAUDRATE
    args = sys.argv[1:]
    if "--port" in args:
        idx = args.index("--port")
        if idx + 1 < len(args):
            port = args[idx + 1]
    elif "-p" in args:
        idx = args.index("-p")
        if idx + 1 < len(args):
            port = args[idx + 1]

    if "--baud" in args:
        idx = args.index("--baud")
        if idx + 1 < len(args):
            baud = int(args[idx + 1])
    elif "-b" in args:
        idx = args.index("-b")
        if idx + 1 < len(args):
            baud = int(args[idx + 1])

    if "--list" in args or "-l" in args:
        list_serial_ports()
        sys.exit(0)

    # Main loop: wait for device → send data → reconnect on disconnect
    print("[INFO] pc_to_usb.py — PC stats sender over USB CDC")
    print(f"       Baudrate: {baud}")
    if port:
        print(f"       Port: {port} (specified)")
    else:
        print("       Port: auto-detect (plug Ameba USB, or use --port COMx)")
    print("       Press Ctrl+C to exit.")

    print(f"[INFO] PC data @ {PUBLISH_INTERVAL}s — MCU UI timer is 1s, data-driven update matches PC send rate")
    print(f"       Lock events carry timestamp for MCU clock sync (no SNTP needed in USB CDC mode)")

    # Start MQTT SHT3X subscriber in background (USB CDC mode: MCU has no WiFi)
    start_sht3x_subscriber()

    # Remember whether user specified a port or auto-detect mode
    auto_detect = (port is None)

    # Standby monitor: when screen is locked after disconnect, wait silently
    # for unlock before reconnecting (Windows lock prevents CDC ACM port
    # re-enumeration from being visible to user-space processes).
    standby_detector = ScreenLockDetector()

    while running:
        ser = wait_for_serial(port, baud)
        if ser is None:
            break
        print(f"[INFO] Starting USB send loop on {ser.port}")
        usb_loop(ser)
        if not running:
            break
        print("[INFO] USB connection lost. Reconnecting...")
        if auto_detect:
            port = None

        # Silently wait for unlock before attempting serial reconnect
        while running and standby_detector.is_locked():
            time.sleep(1)
