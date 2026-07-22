#!/usr/bin/env python
"""
Cross-platform PC resource monitor -> MQTT / debug output
Supports Windows / Linux / macOS
Auto-runs in .venv virtual environment if available

Additional fields:
  cpu_freq_current / cpu_freq_min / cpu_freq_max
  hostname / os_platform
  swap_total / swap_used / swap_percent
  gpu_name / gpu_usage / gpu_mem_used / gpu_mem_total / gpu_temp
  disk_io_percent (Linux only, Windows = null)
"""

import sys
import os
import subprocess
import platform
import getpass
import json
import time
import signal
import ssl
import socket
import datetime as dt  # LHM timing diagnostics

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
        print("  pip install -r requirements.txt")
        sys.exit(1)

    print(f"-> Re-running via venv: {python_exe}")
    result = subprocess.run([python_exe] + sys.argv)
    sys.exit(result.returncode)

ensure_venv()

# ---------- Third-party imports ----------
import psutil
import paho.mqtt.client as mqtt

# ---------- Configuration ----------
MQTT_BROKER = "your.emqxsl.cn"
MQTT_PORT = 8883
MQTT_TOPIC = "pc/stats"
MQTT_CLIENT_ID = "pc"
MQTT_USERNAME = "your_user_id"
MQTT_PASSWORD = "your_password"
PUBLISH_INTERVAL = 3          # Seconds between publishes
TLS_VERIFY = False            # Skip cert validation during testing
DEBUG_MODE = "--debug" in sys.argv or "-d" in sys.argv

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

# Full LHM sensor output (default off)
# True  -> Push complete LHM sensor data (~100+ fields), for debugging or USB full report
# False -> Only backfill fields that nvidia-smi/WMI couldn't provide; lhm fields excluded
LHM_FULL_DATA = False

# ---------- Libre Hardware Monitor path (Windows only) ----------
# Auto-detect from running LHM process, fall back to default path
LHM_DIR = None  # Resolved by _find_lhm_dir() at runtime

def _find_lhm_dir():
    """Dynamically locate Libre Hardware Monitor installation directory."""
    # Try from running LHM process
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

    # Default path (user Downloads directory)
    default = os.path.expandvars(r"%USERPROFILE%\Downloads\LibreHardwareMonitor")
    dll_path = os.path.join(default, "LibreHardwareMonitorLib.dll")
    if os.path.isfile(dll_path):
        return default

    return None

running = True

# Signal handler: only set flag, never call print() (may deadlock)
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
        """Check for LogonUI.exe process (Windows lock screen)."""
        for proc in psutil.process_iter(['name']):
            try:
                if proc.info['name'] == 'LogonUI.exe':
                    return True
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
        return False

    def _check_linux(self):
        """Check if current user session is locked via logind LockedHint."""
        try:
            import dbus
            bus = dbus.SystemBus()
            login = bus.get_object('org.freedesktop.login1',
                                   '/org/freedesktop/login1')
            manager = dbus.Interface(login,
                                     'org.freedesktop.login1.Manager')
            sessions = manager.ListSessions()
            my_uid = os.getuid()
            for sid, uid, uname, seat, vtn in sessions:
                if uid != my_uid:
                    continue
                spath = f'/org/freedesktop.login1/session/{sid}'
                sobj = bus.get_object('org.freedesktop.login1', spath)
                props = dbus.Interface(sobj,
                                       'org.freedesktop.DBus.Properties')
                locked = props.Get('org.freedesktop.login1.Session',
                                   'LockedHint')
                if locked:
                    return True
            return False
        except Exception:
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
# Linux: psutil native sensors
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


# ---------- Libre Hardware Monitor comprehensive collection (pythonnet DLL) ----------
_LHM_data_cache = None
_LHM_cache_time = 0
_LHM_CACHE_TTL = 30
_LHM_computer = None  # Persistent Computer object, Open() once, avoid repeated HW enumeration


def get_libre_hardware_monitor_data():
    """
    Load LibreHardwareMonitorLib.dll via pythonnet to collect hardware sensors.
    No LHM process, WMI registration, or HTTP Server required.

    Computer object is Open()ed once and kept resident; subsequent calls only
    Update() to refresh values, avoiding full HW re-enumeration (which triggers
    Windows device manager refresh).

    Returns a grouped dictionary, or None (unavailable):

      temps:    Temperature (C)
        CPU Package, P-Core #1~4, E-Core #1~8, GPU Core, Hard drives...
      loads:    Load (%)
        CPU Total, CPU Core #x Thread #x, GPU D3D 3D/Video/Decode...
      clocks:   Frequency (MHz)
        P-Core #1~4, E-Core #1~8, Bus Speed, GPU Core...
      fans:     Fan speed (RPM)
      voltages: Voltage (V)
        CPU Core, P-Core #x, E-Core #x, GPU Core...
      powers:   Power consumption (W)
        CPU Package, CPU Cores, CPU Platform, GPU Power...

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
        # Cache expired but update failed: keep stale cache, retry next cycle
        _LHM_cache_time = now
        return _LHM_data_cache  # Return stale cache as fallback


# ---------- GPU backfill: fill unavailable fields from LHM data ----------
def _backfill_gpu_from_lhm(gpu, lhm):
    """
    Use LHM DLL sensor data to backfill GPU fields that nvidia-smi couldn't provide.
    Applicable for Intel/AMD integrated GPU scenarios.
    """
    if gpu is None or lhm is None:
        return gpu

    gpu_name = gpu.get("name") or ""
    changed = False

    # Only backfill non-NVIDIA GPUs (NVIDIA has complete data from nvidia-smi)
    if "NVIDIA" not in gpu_name.upper():
        # --- GPU usage: from D3D 3D Load ---
        if gpu.get("usage") is None and lhm["loads"]:
            for key, val in lhm["loads"].items():
                if "D3D 3D" in key:
                    gpu["usage"] = round(val, 1)
                    changed = True
                    break

        # --- GPU memory used: from D3D Shared Memory Used ---
        if gpu.get("mem_used_mb") is None and lhm["others"]:
            for key, val in lhm["others"].items():
                if "D3D Shared Memory Used" in key:
                    gpu["mem_used_mb"] = round(val, 0)
                    changed = True
                    break

        # --- GPU temperature: from LHM temperature data ---
        if gpu.get("temp_c") is None and lhm["temps"]:
            for key, val in lhm["temps"].items():
                key_lower = key.lower()
                if "gpu" in key_lower:
                    gpu["temp_c"] = val
                    changed = True
                    break

    if changed:
        print(f"[GPU-BF] LHM backfill: usage={gpu.get('usage')}, mem={gpu.get('mem_used_mb')}, temp={gpu.get('temp_c')}")

    return gpu


# ---------- GPU info collection (cross-platform, cached) ----------
_gpu_info_cache = None          # None=unqueried, dict=cached (GPU info never changes at runtime)

def get_gpu_info():
    """Fetch GPU info, cached for the entire process lifetime (GPU hardware is immutable)."""
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

    # Strategy 2: Windows WMI (Intel / AMD integrated GPU)
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
                    total_mb = round(float(gpu.AdapterRAM) / (1048576), 0)
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

    # Cache result (even None, to avoid retrying every cycle)
    _gpu_info_cache = result
    return result

# ---------- Disk I/O utilization ----------
# Calculated from psutil.disk_io_counters() read_time/write_time deltas,
# avoiding wmic WMI performance counter provider deadlock (which can hang 10+ s).
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

    # Read and write may overlap; take max as conservative busy time estimate
    delta_busy = max(delta_read, delta_write)
    percent = min(100.0, round(delta_busy / delta_wall_ms * 100.0, 1))
    _disk_io_cache = percent
    return percent

# ---------- Root partition usage (cross-platform) ----------
def get_disk_usage_percent():
    try:
        if platform.system() == 'Windows':
            system_drive = os.environ.get('SystemDrive', 'C:')
            path = system_drive + '\\'
        else:
            path = '/'
        return psutil.disk_usage(path).percent
    except Exception:
        return psutil.disk_usage('/').percent

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

# ---------- Collect all system stats ----------
_STATS_DIAG_LAST_LOG = 0.0
def get_system_stats():
    global _STATS_DIAG_LAST_LOG
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

    # ---------- CPU frequency ----------
    cpu_freq = psutil.cpu_freq()
    cpu_freq_current = round(cpu_freq.current, 0) if cpu_freq else None
    cpu_freq_min = round(cpu_freq.min, 0) if cpu_freq and cpu_freq.min > 0 else None
    cpu_freq_max = round(cpu_freq.max, 0) if cpu_freq and cpu_freq.max > 0 else None

    # ---------- Hostname / OS ----------
    hostname = socket.gethostname()
    os_platform = platform.platform()

    # ---------- Swap ----------
    swap = psutil.swap_memory()
    swap_total = swap.total if swap else 0
    swap_used = swap.used if swap else 0
    swap_percent = round(swap.percent, 1) if swap else 0.0

    # ---------- GPU ----------
    gpu = get_gpu_info()
    disk_io_percent = get_disk_io_percent()

    # ---------- Libre Hardware Monitor sensors (backfill + full output) ----------
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

    _elapsed = time.time() - _t0
    if _elapsed > 3.0:
        diag_log(f"[DIAG] get_system_stats() took {_elapsed:.1f}s")
        _STATS_DIAG_LAST_LOG = time.time()
    elif _elapsed > 0.5 and (time.time() - _STATS_DIAG_LAST_LOG) > 60:
        diag_log(f"[DIAG] get_system_stats() took {_elapsed:.1f}s")
        _STATS_DIAG_LAST_LOG = time.time()

    return {
        # Legacy fields (backward compatible)
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

        # Additional fields
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

        # Libre Hardware Monitor full sensor data (only when LHM_FULL_DATA=True)
        **({"lhm": lhm} if LHM_FULL_DATA else {}),
    }

# ---------- Debug mode ----------
def debug_loop():
    print("[DEBUG] Local debug mode, no MQTT connection. Press Ctrl+C to exit.")
    while running:
        stats = get_system_stats()
        print(json.dumps(stats, indent=2, ensure_ascii=False))
        remaining = PUBLISH_INTERVAL - 1
        if remaining > 0:
            for _ in range(int(remaining)):
                if not running:
                    break
                time.sleep(1)

# ---------- MQTT mode ----------
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("[MQTT] Connected successfully")
        # Publish current lock state on (re)connect so MCU gets retained state
        try:
            currently_locked = userdata.is_locked()
            event = {"event": "lock" if currently_locked else "unlock"}
            payload = json.dumps(event, ensure_ascii=False)
            client.publish("pc/event", payload, qos=2, retain=True)
            print(f"[EVENT] Published initial state on connect: {'LOCK' if currently_locked else 'UNLOCK'}")
        except Exception as e:
            print(f"[WARN] Failed to publish initial state: {e}")
    else:
        print(f"[MQTT] Connection failed, return code: {reason_code}")

def on_disconnect(client, userdata, flags, reason_code, properties):
    if reason_code != 0:
        print("[MQTT] Unexpected disconnect, reconnecting...")
    else:
        print("[MQTT] Disconnected normally")

def mqtt_loop():
    # Create detector early so on_connect / main loop can use it
    detector = ScreenLockDetector()

    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2, client_id=MQTT_CLIENT_ID)
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.user_data_set(detector)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect

    if MQTT_PORT == 8883:
        if TLS_VERIFY:
            client.tls_set()
        else:
            client.tls_set(cert_reqs=ssl.CERT_NONE)
            client.tls_insecure_set(True)

    print(f"[INFO] Connecting to {MQTT_BROKER}:{MQTT_PORT} ...")
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
    except Exception as e:
        print(f"[ERROR] Cannot connect to MQTT broker: {e}")
        sys.exit(1)

    g_stats_count = 0
    g_last_complete = time.time()
    client.loop_start()
    print(f"[INFO] Publishing every {PUBLISH_INTERVAL}s to {MQTT_TOPIC}")

    # Lock screen check interval (seconds) -- checked every 1s
    LOCK_CHECK_INTERVAL = 1.0

    # Lock transition tracking: suppress interval anomaly detection on first publish after lock
    was_locked_before = False

    while running:
        try:
            cycle_start = time.time()
            currently_locked = detector.is_locked()

            # State transition -> publish event immediately
            if detector.has_state_changed(currently_locked):
                if currently_locked:
                    was_locked_before = True   # Entering lock -- suppress interval warning on unlock
                event = {"event": "lock" if currently_locked else "unlock"}
                payload = json.dumps(event, ensure_ascii=False)
                ret = client.publish("pc/event", payload, qos=1, retain=True)
                print(f"[EVENT] Published {'LOCK' if currently_locked else 'UNLOCK'} event")
                if ret.rc != mqtt.MQTT_ERR_SUCCESS:
                    print(f"[WARN] Event publish failed: {ret.rc}")

            # Skip HW collection during lock (no log output)
            if not currently_locked:
                stats = get_system_stats()
                payload = json.dumps(stats, ensure_ascii=False)
                ret = client.publish(MQTT_TOPIC, payload, qos=0)
                if ret.rc == mqtt.MQTT_ERR_SUCCESS:
                    g_stats_count += 1
                    now = time.time()
                    cycle_elapsed = now - cycle_start
                    idle_since_last = now - g_last_complete
                    if cycle_elapsed > 3.0:
                        diag_log(f"[DIAG] Publish cycle took {cycle_elapsed:.1f}s (interval {idle_since_last:.0f}s, #{g_stats_count})")
                    # Skip interval anomaly check for first post-lock publish (gap is expected)
                    if not was_locked_before and idle_since_last > 6.0:
                        diag_log(f"[DIAG] Publish interval anomaly: {idle_since_last:.0f}s (possible stall)")
                    was_locked_before = False
                    g_last_complete = now
                    diag_log(f"[PUB] {len(payload)} bytes")
                else:
                    print(f"[WARN] Publish failed: {ret.rc}")
        except Exception as e:
            diag_log(f"[ERROR] Collection or publish exception: {e}")

        # Fine-grained sleep: check lock state every LOCK_CHECK_INTERVAL to avoid unlock delay
        for _ in range(int(PUBLISH_INTERVAL / LOCK_CHECK_INTERVAL)):
            if not running:
                break
            time.sleep(LOCK_CHECK_INTERVAL)
            new_locked = detector.is_locked()
            if detector.has_state_changed(new_locked):
                event = {"event": "lock" if new_locked else "unlock"}
                payload = json.dumps(event, ensure_ascii=False)
                ret = client.publish("pc/event", payload, qos=1, retain=True)
                print(f"[EVENT] Fast-detect state change -> {'LOCK' if new_locked else 'UNLOCK'}")
                if ret.rc != mqtt.MQTT_ERR_SUCCESS:
                    print(f"[WARN] Event publish failed: {ret.rc}")

    client.loop_stop()
    client.disconnect()
    print("[INFO] Exited")

# ---------- Entry point ----------
if __name__ == "__main__":
    if DEBUG_MODE:
        debug_loop()
    else:
        mqtt_loop()
