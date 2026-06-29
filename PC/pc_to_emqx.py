#!/usr/bin/env python
"""
Cross-platform PC resource monitor → MQTT / debug output
Supports Windows, Linux, macOS
Auto-runs in .venv virtual environment if available
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

# ---------- Virtual environment auto-switch ----------
def ensure_venv(venv_dir=".venv"):
    """Re-run this script with the venv Python if not already inside a venv."""
    if sys.prefix != sys.base_prefix:
        return

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

    print(f"→ Re-running via venv: {python_exe}")
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
PUBLISH_INTERVAL = 3
TLS_VERIFY = False
DEBUG_MODE = "--debug" in sys.argv or "-d" in sys.argv

running = True

# Signal handler must avoid print() (may deadlock)
def set_exit_flag(sig, frame):
    global running
    running = False

signal.signal(signal.SIGINT, set_exit_flag)
if hasattr(signal, 'SIGTERM'):
    signal.signal(signal.SIGTERM, set_exit_flag)

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
def get_cpu_temp():
    # Method 1: psutil native sensors (typically available on Linux)
    if hasattr(psutil, "sensors_temperatures"):
        temps = psutil.sensors_temperatures()
        if temps:
            for name, entries in temps.items():
                if any(key in name.lower() for key in ('cpu', 'core', 'k10temp')):
                    for entry in entries:
                        if entry.current:
                            return round(entry.current, 1)

    # Method 2: Windows WMI
    if platform.system() == 'Windows':
        try:
            import wmi
            w = wmi.WMI()
            zone_temps = w.MSAcpi_ThermalZoneTemperature()
            if zone_temps:
                kelvin = zone_temps[0].CurrentTemperature / 10.0
                return round(kelvin - 273.15, 1)
        except Exception:
            pass

        for namespace in ["root/LibreHardwareMonitor", "root/OpenHardwareMonitor"]:
            try:
                import wmi
                w = wmi.WMI(namespace=namespace)
                sensors = w.Sensor()
                for sensor in sensors:
                    if sensor.SensorType == 'Temperature' and 'cpu' in sensor.Name.lower():
                        return round(sensor.Value, 1)
            except Exception:
                pass

    return None

# ---------- GPU info (cross-platform, cached) ----------
_gpu_info_cache = None

def get_gpu_info():
    """Fetch GPU info once and cache the result (GPU hardware is immutable at runtime)."""
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

    # Strategy 3: wmic CLI fallback
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

# ---------- Disk I/O utilization (Linux only) ----------
def get_disk_io_percent():
    """
    Returns disk I/O utilization percentage.
    Linux: approximate busy time from /proc/diskstats
    Windows: requires admin privileges, returns None

    Note: Currently always returns None due to lack of reliable cross-platform
    busy_time measurement. A third-party library (e.g. sio) or parsing
    /proc/diskstats directly would be needed.
    """
    return None

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
def get_system_stats():
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

    cpu_freq = psutil.cpu_freq()
    cpu_freq_current = round(cpu_freq.current, 0) if cpu_freq else None
    cpu_freq_min = round(cpu_freq.min, 0) if cpu_freq and cpu_freq.min > 0 else None
    cpu_freq_max = round(cpu_freq.max, 0) if cpu_freq and cpu_freq.max > 0 else None

    hostname = socket.gethostname()
    os_platform = platform.platform()

    swap = psutil.swap_memory()
    swap_total = swap.total if swap else 0
    swap_used = swap.used if swap else 0
    swap_percent = round(swap.percent, 1) if swap else 0.0

    gpu = get_gpu_info()
    disk_io_percent = get_disk_io_percent()

    return {
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
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("[MQTT] Connected successfully")
    else:
        print(f"[MQTT] Connection failed, return code: {rc}")

def on_disconnect(client, userdata, rc):
    if rc != 0:
        print("[MQTT] Unexpected disconnect, reconnecting...")
    else:
        print("[MQTT] Disconnected normally")

def mqtt_loop():
    client = mqtt.Client(client_id=MQTT_CLIENT_ID)
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
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

    client.loop_start()
    print(f"[INFO] Publishing every {PUBLISH_INTERVAL}s to {MQTT_TOPIC}")
    while running:
        try:
            stats = get_system_stats()
            payload = json.dumps(stats, ensure_ascii=False)
            ret = client.publish(MQTT_TOPIC, payload, qos=1)
            if ret.rc == mqtt.MQTT_ERR_SUCCESS:
                print(f"[PUB] {len(payload)} bytes")
            else:
                print(f"[WARN] Publish failed: {ret.rc}")
        except Exception as e:
            print(f"[ERROR] Collection or publish error: {e}")

        remaining = PUBLISH_INTERVAL - 1
        if remaining > 0:
            for _ in range(int(remaining)):
                if not running:
                    break
                time.sleep(1)

    client.loop_stop()
    client.disconnect()
    print("[INFO] Exited")

# ---------- Entry point ----------
if __name__ == "__main__":
    if DEBUG_MODE:
        debug_loop()
    else:
        mqtt_loop()
