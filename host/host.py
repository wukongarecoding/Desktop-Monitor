#!/usr/bin/env python3
import asyncio, json, queue, sys, threading, time, datetime, os, struct
import urllib.request, urllib.parse, base64, hmac, hashlib, socket
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from io import BytesIO
from pathlib import Path
from collections import Counter

import serial, serial.tools.list_ports
import psutil
import winsdk.windows.media.control as wmc
from winsdk.windows.storage.streams import Buffer, InputStreamOptions
from PIL import Image

try:
    import GPUtil; HAS_GPUTIL = True
except ImportError:
    HAS_GPUTIL = False
try:
    import keyboard; HAS_KEYBOARD = True
except ImportError:
    HAS_KEYBOARD = False
try:
    import winreg
    HAS_WINREG = True
except ImportError:
    HAS_WINREG = False
try:
    import pystray
    from pystray import MenuItem as TrayMenuItem
    HAS_TRAY = True
except ImportError:
    HAS_TRAY = False

if getattr(sys, 'frozen', False) or "__compiled__" in globals():
    application_path = Path(sys.argv[0]).parent
else:
    application_path = Path(__file__).parent
CONFIG_FILE = application_path / "hotkeys.json"

DEFAULT_HOTKEYS = {
    "KEY1": "", "KEY1_D": "", "KEY2": "", "KEY2_D": "",
    "KEY3": "", "KEY3_D": "",
    "ENC_L": "", "ENC_R": "", "ENC_P": "",
    "hwd_led": True, "hwd_volt": True, "hwd_max_volt": "4.8",
    "clock_en": True, "clock_x": "80", "clock_y": "20",
    "clock_size": "2", "clock_r": "255", "clock_g": "255", "clock_b": "255",
    "weather_city": "Beijing",
    "weather_public_key": "", "weather_private_key": "",
    "comm_mode": "serial", "wifi_ip": "", "wifi_port": "81",
    "wifi_ssid": "", "wifi_pwd": "",
    "auto_start": False,
    "auto_connect": True,
}

TARGET_APPS = (
    "chrome", "msedge", "firefox",
    "qqmusic", "cloudmusic", "netease",
    "spotify", "kugou", "kuwo",
    "potplayer", "wmplayer", "zunemusic",
)


#autorun
AUTOSTART_KEY  = r"Software\Microsoft\Windows\CurrentVersion\Run"
AUTOSTART_NAME = "SmtcBridge"


def _autostart_command():
    
    if getattr(sys, 'frozen', False) or "__compiled__" in globals():
        return f'"{sys.executable}"'
    return f'"{sys.executable}" "{Path(__file__).resolve()}"'


def is_autostart_enabled():
    if not HAS_WINREG:
        return False
    try:
        k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, AUTOSTART_KEY, 0, winreg.KEY_READ)
        try:
            winreg.QueryValueEx(k, AUTOSTART_NAME)
            return True
        finally:
            winreg.CloseKey(k)
    except (FileNotFoundError, OSError):
        return False


def set_autostart(enable):
    if not HAS_WINREG:
        return False
    try:
        k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, AUTOSTART_KEY, 0, winreg.KEY_SET_VALUE)
        try:
            if enable:
                winreg.SetValueEx(k, AUTOSTART_NAME, 0, winreg.REG_SZ, _autostart_command())
            else:
                try:
                    winreg.DeleteValue(k, AUTOSTART_NAME)
                except FileNotFoundError:
                    pass
            return True
        finally:
            winreg.CloseKey(k)
    except Exception:
        return False



class WsClient:
    def __init__(self, host, port=81, timeout=0.05):
        self.host, self.port = host, port
        self.sock = None
        self._open = False
        self.timeout = timeout
        self._recv_buf = b""
        self._frag_data = bytearray()

    @property
    def is_open(self): return self._open

    def open(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET / HTTP/1.1\r\n"
               f"Host: {self.host}:{self.port}\r\n"
               f"Upgrade: websocket\r\n"
               f"Connection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\n"
               f"Sec-WebSocket-Version: 13\r\n\r\n")
        self.sock.sendall(req.encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = self.sock.recv(1024)
            if not chunk: raise IOError("WebSocket 握手无响应")
            resp += chunk
        head, _, rest = resp.partition(b"\r\n\r\n")
        status = head.split(b"\r\n", 1)[0]
        if b"101" not in status:
            raise IOError(f"WebSocket 握手失败: {status!r}")
        self._recv_buf = rest
        self.sock.settimeout(self.timeout)
        self._open = True

    def close(self):
        if self._open:
            try: self._send_frame(0x8, b"")
            except: pass
        self._open = False
        if self.sock:
            try: self.sock.close()
            except: pass
            self.sock = None

    def _send_frame(self, opcode, payload):
        if not self.sock: raise IOError("ws not open")
        if isinstance(payload, str): payload = payload.encode()
        n = len(payload)
        header = bytearray()
        header.append(0x80 | opcode)          # FIN=1
        if n < 126:
            header.append(0x80 | n)
        elif n < 65536:
            header.append(0x80 | 126)
            header.extend(struct.pack(">H", n))
        else:
            header.append(0x80 | 127)
            header.extend(struct.pack(">Q", n))
        mask = os.urandom(4)
        header.extend(mask)
        masked = bytes(payload[i] ^ mask[i & 3] for i in range(n))
        self.sock.sendall(bytes(header) + masked)

    def write(self, data):
        #send text
        if isinstance(data, bytes):
            try: data = data.decode("utf-8")
            except: return
        self._send_frame(0x1, data)

    def write_bin(self, data):
        #send binary
        if isinstance(data, str): data = data.encode()
        self._send_frame(0x2, data)

    def flush(self): pass

    def read(self, _n=1):
        if not self._open: return b""
        try:
            try:
                chunk = self.sock.recv(4096)
                if not chunk:
                    self._open = False
                    return b""
                self._recv_buf += chunk
            except socket.timeout:
                pass
            except Exception:
                self._open = False
                return b""
            return self._parse_frames()
        except Exception:
            self._open = False
            return b""

    def _parse_frames(self):
        out = bytearray()
        while len(self._recv_buf) >= 2:
            b0, b1 = self._recv_buf[0], self._recv_buf[1]
            fin    = (b0 & 0x80) != 0
            opcode = b0 & 0x0F
            masked = (b1 & 0x80) != 0
            n = b1 & 0x7F
            idx = 2
            if n == 126:
                if len(self._recv_buf) < idx + 2: break
                n = struct.unpack(">H", self._recv_buf[idx:idx+2])[0]; idx += 2
            elif n == 127:
                if len(self._recv_buf) < idx + 8: break
                n = struct.unpack(">Q", self._recv_buf[idx:idx+8])[0]; idx += 8
            if masked:
                if len(self._recv_buf) < idx + 4: break
                mask = self._recv_buf[idx:idx+4]; idx += 4
            else:
                mask = None
            if len(self._recv_buf) < idx + n: break
            payload = self._recv_buf[idx:idx+n]
            self._recv_buf = self._recv_buf[idx+n:]
            if mask:
                payload = bytes(p ^ mask[i & 3] for i, p in enumerate(payload))

            if opcode == 0x1:                    # TEXT
                if fin: out.extend(payload)
                else:   self._frag_data = bytearray(payload)
            elif opcode == 0x2:                  # BINARY
                pass
            elif opcode == 0x0:                  # continue
                self._frag_data.extend(payload)
                if fin:
                    out.extend(self._frag_data)
                    self._frag_data = bytearray()
            elif opcode == 0x8:                  # CLOSE
                self._open = False
                break
            elif opcode == 0x9:                  # PING
                try: self._send_frame(0xA, payload)
                except: pass
            elif opcode == 0xA:                  # PONG
                pass
        return bytes(out)



class Bridge:
    def __init__(self, port=None, baud=256000,
                 backend="serial", wifi_ip="", wifi_port=81):
        self.port = port
        self.baud = baud
        self.backend = backend
        self.wifi_ip = wifi_ip
        self.wifi_port = wifi_port
        self.ser = None
        self.stop_event = threading.Event()
        self.send_lock = threading.RLock()     
        self.bin_lock  = threading.Lock()     

        self.monitor_mode = False
        self.music_mode = False
        self.resource_type = 'cpu'
        self._gpu_cache = 0.0

        self.smtc_loop = None
        self.current_session = None
        self.music_title = ""
        self.music_artist = ""
        self.music_cover_jpeg = None
        self.music_theme = (30, 20, 50)
        self.music_position = 0.0
        self.music_duration = 0.0
        self.music_playing = False
        self.base_wall = 0.0
        self.last_cover_data = None
        self.last_meta = None
        self._last_pushed_meta = None
        self._last_pushed_cover = None

        self.bg_sending = False
        self.cover_sending = False 
        self.last_bg_image = None

        self.weather_city = DEFAULT_HOTKEYS["weather_city"]
        self.weather_data = None
        self.weather_last_fetch = 0
        self.weather_interval = 900
        self._weather_key_warned = False
        self._last_pushed_weather = None

        self.reported_ip = ""
        self.reported_ssid = ""
        self._smtc_manager_logged = False

        self.hotkeys = dict(DEFAULT_HOTKEYS)
        self.load_hotkeys()

        self.status_queue = queue.Queue()
        self.log_queue = queue.Queue()

    def log(self, msg): self.log_queue.put(msg)
    def update_status(self, **kw): self.status_queue.put(kw)

    def load_hotkeys(self):
        try:
            if CONFIG_FILE.exists():
                with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                for k in DEFAULT_HOTKEYS:
                    if k in data: self.hotkeys[k] = data[k]
            self.weather_city = self.hotkeys.get("weather_city", "Beijing")
        except Exception as e:
            self.log(f"加载配置失败: {e}")

    def save_hotkeys(self):
        try:
            with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
                json.dump(self.hotkeys, f, ensure_ascii=False, indent=2)
            return True
        except Exception as e:
            self.log(f"保存配置失败: {e}"); return False

    def find_port(self):
        if self.port: return self.port
        for p in serial.tools.list_ports.comports():
            if any(k in p.description for k in ("CP210", "CH340", "USB", "Silicon", "JTAG")):
                return p.device
        ports = list(serial.tools.list_ports.comports())
        return ports[0].device if ports else None

    def open(self):
        if self.backend == "wifi":
            if not self.wifi_ip:
                return False, "WiFi 未填 IP"
            try:
                t = WsClient(self.wifi_ip, self.wifi_port)
                t.open()
                self.ser = t
                return True, f"WS ws://{self.wifi_ip}:{self.wifi_port}"
            except Exception as e:
                return False, f"WS 连接失败: {e}"
        port = self.find_port()
        if not port: return False, "未找到串口"
        try:
            self.ser = serial.Serial(port, self.baud, timeout=0.05)
            return True, f"{port} @ {self.baud}"
        except Exception as e:
            return False, str(e)

    def close(self):
        self.stop_event.set()
        if self.ser:
            try: self.ser.close()
            except: pass

    def send(self, data, quiet=False):
        if not self.ser: return
        is_bin = not isinstance(data, str)
        if isinstance(data, str): data = data.encode('utf-8')
        with self.send_lock:
            try:
                if isinstance(self.ser, WsClient) and not is_bin:
                    self.ser.write(data.decode('utf-8', errors='ignore'))
                else:
                    self.ser.write(data)
            except Exception as e:
                self.log(f"发送失败: {e}"); return
        if not is_bin and not quiet:
            s = data.decode('utf-8', errors='ignore').strip()
            if s and len(s) < 100:
                self.log(f"[TX] {s}")

    def start(self, port=None):
        self.stop_event.clear()
        if port: self.port = port
        ok, msg = self.open()
        if not ok: return False, msg

        if self.backend == "serial":
            psutil.cpu_percent(interval=None)

        for target in (self._serial_loop, self._metric_loop,
                       self._music_progress_loop, self._gpu_loop,
                       self._time_loop, self._weather_loop):
            threading.Thread(target=target, daemon=True).start()
        threading.Thread(target=self._run_smtc, daemon=True).start()
        return True, msg

    def _run_smtc(self):
        self.log("[SMTC] 线程启动")
        self.smtc_loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.smtc_loop)
        try:
            self.smtc_loop.run_until_complete(self._smtc_loop())
        except Exception as e:
            self.log(f"SMTC loop 退出: {e}")

    def _serial_loop(self):
        buf = b""
        while not self.stop_event.is_set():
            try:
                if not self.ser or not getattr(self.ser, 'is_open', True):
                    if self.backend == "wifi":
                        self.log("WS连接已断开")
                    break
                data = self.ser.read(256)
                if not data: continue
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    self._handle(line.decode('utf-8', errors='ignore').strip())
            except Exception as e:
                if not self.stop_event.is_set():
                    self.log(f"通信错误: {e}")
                time.sleep(0.2)

    def _handle(self, line):
        if not line: return
        if line.startswith("[RX] MON:") or line.startswith("MON:"): return
        if line.startswith("[WIFI] connected IP="):
            self.reported_ip = line.split("=", 1)[1].strip()
        if line.startswith("[WIFI] cfg saved ssid="):
            self.reported_ssid = line.split("=", 1)[1].strip()
        if line.startswith("IP:"):
            self.reported_ip = line[3:].strip()

        self.log(f"[ESP32] {line}")

        if line == "PING":
            self.send("PONG\n", quiet=True)
        elif line == "MON_ON":
            self.monitor_mode = True; self.music_mode = False
        elif line == "MON_OFF":
            self.monitor_mode = False
        elif line == "MUS_ON":
            self.monitor_mode = False; self.music_mode = True
            self._last_pushed_meta = None
            self._last_pushed_cover = None
            self._push_meta(force=True); self._push_progress()
            if self.music_cover_jpeg:
                threading.Timer(0.3, self._push_cover).start()
        elif line == "MUS_OFF":
            self.music_mode = False
        elif line == "WEA_ON":
            self.weather_last_fetch = 0
            self._last_pushed_weather = None
            if self.weather_data:
                self._push_weather(force=True)
            threading.Thread(target=self._fetch_weather, daemon=True).start()
        elif line == "MUS_PLAY": self._toggle_playback()
        elif line == "MUS_NEXT": self._skip_next()
        elif line == "MUS_PREV": self._skip_prev()
        elif line in ("KEY1", "KEY2", "KEY3", "KEY1_D", "KEY2_D", "KEY3_D",
                      "ENC_L", "ENC_R", "ENC_P"):
            self._trigger_hotkey(line)

    def _trigger_hotkey(self, key):
        hk = self.hotkeys.get(key, "").strip()
        if not hk or not HAS_KEYBOARD: return
        hk = hk.lower().strip()
        if hk == "win": hk = "windows"
        elif hk.startswith("win+"): hk = "windows+" + hk[4:]
        try:
            time.sleep(0.02); keyboard.send(hk)
            self.log(f"{key} → {hk}")
        except Exception as e:
            self.log(f"执行 {hk} 失败: {e}")

    # ---------------- 背景图 ----------------
    def send_bg_image(self, img):
        if not self.ser or self.bg_sending: return False
        try:
            img = img.convert('RGB').resize((240, 320), Image.Resampling.LANCZOS)
        except: return False
        self.last_bg_image = img
        pixels = img.load()
        buf = bytearray(240 * 320 * 2)
        idx = 0
        for y in range(320):
            for x in range(240):
                r, g, b = pixels[x, y]
                v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                buf[idx] = (v >> 8) & 0xFF; buf[idx+1] = v & 0xFF; idx += 2
        threading.Thread(target=self._do_send_bg, args=(bytes(buf),), daemon=True).start()
        return True

    def resend_bg_image(self):
        if self.last_bg_image is None: return False
        self.log("重发上次背景图")
        return self.send_bg_image(self.last_bg_image)

    def _do_send_bg(self, data):
        self.bg_sending = True
        total = len(data)
        try:
            with self.bin_lock:
                if self.backend == "wifi" and isinstance(self.ser, WsClient):
                    
                    tid = (int(time.time() * 1000) & 0xFFFFFF) | 0x01
                    chunk = 4096
                    with self.send_lock:
                        self.ser.write(f"BEGIN:{tid}:BG:{total}:{chunk}\n")
                    time.sleep(0.12)
                    n_chunks = (total + chunk - 1) // chunk
                    for seq in range(n_chunks):
                        start = seq * chunk
                        end = min(start + chunk, total)
                        with self.send_lock:
                            self.ser.write(f"CHUNK:{tid}:{seq}\n")
                            self.ser.write_bin(data[start:end])
                        time.sleep(0.006)
                    with self.send_lock:
                        self.ser.write(f"END:{tid}\n")
                    time.sleep(1.5 + total / 1024.0 / 30.0)
                    self.log(f"背景图 {total} 字节 (WS) 已发送 id={tid}")
                else:
                    # === 串口原始协议 ===
                    with self.send_lock:
                        self.ser.write(f"BG_IMAGE:{total}\n".encode('utf-8'))
                        self.ser.flush(); time.sleep(0.1)
                        chunk = 2048
                        for i in range(0, total, chunk):
                            self.ser.write(data[i:i+chunk]); time.sleep(0.003)
                        self.ser.flush()
                    time.sleep(total * 10.0 / self.baud + 2.0)
                    self.log(f"背景图 {total} 字节发送完成")
        except Exception as e:
            self.log(f"背景图发送失败: {e}")
        finally:
            self.bg_sending = False

    def _time_loop(self):
        while not self.stop_event.is_set():
            if self.ser:
                now = datetime.datetime.now()
                try:
                    with self.send_lock:
                        self.ser.write(
                            f"TIME:{now.year}|{now.month}|{now.day}|"
                            f"{now.hour}|{now.minute}|{now.second}\n".encode('utf-8'))
                except: pass
            time.sleep(1.0)

    def set_weather_city(self, city):
        city = city.strip()
        if not city: return
        if self.weather_city == city and self.weather_data:
            self._push_weather(force=True); return
        self.weather_city = city
        self.weather_last_fetch = 0
        self._last_pushed_weather = None
        self.hotkeys["weather_city"] = city
        self.save_hotkeys()
        threading.Thread(target=self._fetch_weather, daemon=True).start()

    def set_weather_keys(self, public_key, private_key):
        self.hotkeys["weather_public_key"]  = (public_key  or "").strip()
        self.hotkeys["weather_private_key"] = (private_key or "").strip()
        self._weather_key_warned = False
        self.weather_last_fetch = 0
        self._last_pushed_weather = None
        self.save_hotkeys()
        threading.Thread(target=self._fetch_weather, daemon=True).start()

    def _weather_loop(self):
        while not self.stop_event.is_set():
            now = time.time()
            interval = self.weather_interval if self.weather_data else 30
            if now - self.weather_last_fetch >= interval:
                self._fetch_weather()
                self.weather_last_fetch = now
            if self.weather_data and self.ser and \
               self._last_pushed_weather != self.weather_data:
                self._push_weather(force=True)
                self.log("[WEA] push to ESP32 (auto)")
            time.sleep(5)

    def _build_signed_url(self, endpoint, params):
        public_key  = self.hotkeys["weather_public_key"].strip()
        private_key = self.hotkeys["weather_private_key"].strip()
        params = dict(params)
        params["public_key"] = public_key
        params["ts"]  = str(int(time.time()))
        params["ttl"] = "300"
        raw = "&".join(f"{k}={params[k]}" for k in sorted(params.keys()))
        sig = base64.b64encode(
            hmac.new(private_key.encode(), raw.encode(), hashlib.sha1).digest()
        ).decode()
        params["sig"] = sig
        qs = "&".join(f"{k}={urllib.parse.quote(str(v), safe='')}"
                       for k, v in params.items())
        return f"{endpoint}?{qs}"

    @staticmethod
    def _seniverse_to_wmo(code):
        try: c = int(code)
        except: return 0
        if c in (0,1,2,3): return 0
        if c in (5,6): return 1
        if c in (4,7): return 2
        if c in (8,9): return 3
        if c == 10: return 80
        if c in (11,12): return 95
        if c == 13: return 61
        if c == 14: return 63
        if c == 15: return 65
        if c in (16,17,18): return 82
        if c == 19: return 85
        if c == 20: return 71
        if c == 21: return 73
        if c in (22,23): return 75
        if c in (24,25,36): return 45
        if c == 26: return 66
        if c == 27: return 35
        if c in (28,29,30): return 61
        if c in (31,32): return 82
        if c in (33,34,35): return 73
        if c in (37,38): return 68
        return 0

    def _push_weather(self, force=False):
        if not self.weather_data: return
        city = self.hotkeys.get("weather_city", self.weather_city)
        city = city.replace("|", " ").replace("\n", " ").replace("\r", " ").strip() or "Unknown"
        d = self.weather_data
        msg = (f"WEA:{city}|{d[0]}|{d[1]}|{d[2]}|{d[3]}|{d[4]}|"
               f"{d[5]}|{d[6]}|{d[7]}|{d[8]}|{d[9]}|{d[10]}|{d[11]}|{d[12]}|"
               f"{d[13]}|{d[14]}|{d[15]}\n")
        self.log(f"[WEA TX] {msg.strip()}")
        self.send(msg, quiet=True)
        self._last_pushed_weather = self.weather_data

    def _fetch_weather(self):
        public_key  = self.hotkeys.get("weather_public_key", "").strip()
        private_key = self.hotkeys.get("weather_private_key", "").strip()
        if not public_key or not private_key:
            if not self._weather_key_warned:
                self.log("未配置心知天气公钥/私钥"); self._weather_key_warned = True
            return False
        try:
            loc = self.weather_city.strip()
            url_now = self._build_signed_url(
                "https://api.seniverse.com/v3/weather/now.json",
                {"location": loc, "language": "zh-Hans", "unit": "c"})
            with urllib.request.urlopen(url_now, timeout=15) as r:
                data = json.loads(r.read())
            if not data.get("results"):
                self.log(f"实况返回: {data}"); return False
            res0 = data["results"][0]; now = res0["now"]
            self.weather_city = res0["location"].get("name", self.weather_city)
            temp  = round(float(now.get("temperature", 0)))
            feels = round(float(now.get("feels_like", now.get("temperature", 0))))
            hum   = int(now.get("humidity", 0))
            wind  = round(float(now.get("wind_speed", 0)))
            code  = self._seniverse_to_wmo(now.get("code", 0))
            text  = now.get("text", "")

            url_daily = self._build_signed_url(
                "https://api.seniverse.com/v3/weather/daily.json",
                {"location": loc, "language": "zh-Hans", "unit": "c",
                 "start": "0", "days": "4"})
            with urllib.request.urlopen(url_daily, timeout=15) as r:
                data2 = json.loads(r.read())
            if not data2.get("results"):
                self.log(f"预报返回: {data2}"); return False
            daily = data2["results"][0]["daily"]
            def dval(idx, key, default=0):
                if idx >= len(daily): return default
                return daily[idx].get(key, default)

            self.weather_data = (
                temp, feels, hum, wind, code,
                round(float(dval(0, "high", 0))), round(float(dval(0, "low", 0))),
                self._seniverse_to_wmo(dval(1, "code_day", 0)),
                round(float(dval(1, "high", 0))), round(float(dval(1, "low", 0))),
                self._seniverse_to_wmo(dval(2, "code_day", 0)),
                round(float(dval(2, "high", 0))), round(float(dval(2, "low", 0))),
                self._seniverse_to_wmo(dval(3, "code_day", 0)),
                round(float(dval(3, "high", 0))), round(float(dval(3, "low", 0))),
            )
            self.log(f"天气更新: {self.weather_city} {temp}°C {text}")
            if not self.ser:
                self.log("[WEA] 数据已获取，但当前无连接，暂不推送")
                return True
            self._push_weather(force=True)
            return True
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", "ignore")
            self.log(f"HTTP {e.code}  body={body}"); return False
        except Exception as e:
            self.log(f"天气异常: {type(e).__name__}: {e}"); return False

    def _gpu_loop(self):
        while not self.stop_event.is_set():
            if HAS_GPUTIL and self.monitor_mode and self.resource_type == 'gpu':
                try:
                    gpus = GPUtil.getGPUs()
                    self._gpu_cache = (sum(g.load for g in gpus) / len(gpus) * 100.0) if gpus else 0.0
                except: self._gpu_cache = 0.0
            time.sleep(1.0)

    def _get_metric(self):
        if self.resource_type == 'cpu':
            return psutil.cpu_percent(interval=None)
        elif self.resource_type == 'ram':
            return psutil.virtual_memory().percent
        elif self.resource_type == 'gpu':
            if not HAS_GPUTIL:
                if not getattr(self, '_gpu_warned', False):
                    self.log("未安装 GPUtil，GPU 监控不可用")
                    self._gpu_warned = True
                return 0.0
            return self._gpu_cache
        return 0.0

    def _metric_loop(self):
        last_send = 0.0
        while not self.stop_event.is_set():
            if self.bg_sending or self.cover_sending:
                time.sleep(0.05); continue
            now = time.time()
            if now - last_send >= 0.5:
                v = self._get_metric()
                sent = False
                if self.ser is not None:
                    if self.send_lock.acquire(blocking=False):
                        try:
                            self.ser.write(
                                f"MON:{self.resource_type}:{v:.1f}\n".encode('utf-8'))
                            sent = True
                        except Exception as e:
                            self.log(f"[MON] 发送失败: {e}")
                        finally:
                            self.send_lock.release()
                if sent:
                    last_send = now
                if self.monitor_mode:
                    self.update_status(cpu=v, res_type=self.resource_type)
            time.sleep(0.05)

    def _music_progress_loop(self):
        while not self.stop_event.is_set():
            if self.current_session and self.ser:
                self._push_progress()
                self.update_status(music_title=self.music_title,
                                   music_artist=self.music_artist,
                                   music_playing=self.music_playing)
            time.sleep(0.5)

    def _push_meta(self, force=False):
        if not self.music_title and not self.music_artist: return
        if not force and self._last_pushed_meta == (self.music_title, self.music_artist):
            return
        title  = self.music_title.replace("|", " ").replace("\n", " ")[:300]
        artist = self.music_artist.replace("|", " ").replace("\n", " ")[:300]
        self.send(f"MUS_META:{title}|{artist}\n")
        self._last_pushed_meta = (self.music_title, self.music_artist)

    def _push_progress(self):
        if self.music_duration <= 0.5: return
        cur = self.music_position + (time.time() - self.base_wall) if self.music_playing else self.music_position
        if cur > self.music_duration: cur = self.music_duration
        self.send(f"MUS_PROG:{cur:.1f}|{self.music_duration:.1f}|"
                  f"{'P' if self.music_playing else 'S'}\n")

    def _push_cover(self):
        if not self.music_cover_jpeg: return
        if self._last_pushed_cover == self.music_cover_jpeg: return
        r, g, b = self.music_theme
        self.cover_sending = True                   
        try:
            with self.bin_lock:
                if not self.ser: return
                if self.backend == "wifi" and isinstance(self.ser, WsClient):
                    data = self.music_cover_jpeg
                    total = len(data)
                    tid = (int(time.time() * 1000) & 0xFFFFFF) ^ 0xC0
                    chunk = 4096
                    with self.send_lock:
                        self.ser.write(f"BEGIN:{tid}:COVER:{total}:{chunk}\n")
                    time.sleep(0.08)
                    n_chunks = (total + chunk - 1) // chunk
                    for seq in range(n_chunks):
                        start = seq * chunk
                        end = min(start + chunk, total)
                        with self.send_lock:
                            self.ser.write(f"CHUNK:{tid}:{seq}\n")
                            self.ser.write_bin(data[start:end])
                        time.sleep(0.005)
                    with self.send_lock:
                        self.ser.write(f"END:{tid}\n")
                    self._last_pushed_cover = self.music_cover_jpeg
                else:
                    with self.send_lock:
                        self.ser.write(
                            f"MUS_COVER:{len(self.music_cover_jpeg)}|{r}|{g}|{b}\n".encode())
                        time.sleep(0.1)          
                        self.ser.write(self.music_cover_jpeg)
                        self.ser.flush()
                    self._last_pushed_cover = self.music_cover_jpeg
        except Exception as e:
            self.log(f"发送封面失败: {e}")
        finally:
            self.cover_sending = False     

    # ---------------- SMTC ----------------
    async def _smtc_loop(self):
        while not self.stop_event.is_set():
            try:
                manager = await wmc.GlobalSystemMediaTransportControlsSessionManager.request_async()
                if not self._smtc_manager_logged:
                    self.log("[SMTC] manager ready")
                    self._smtc_manager_logged = True

                session = None
                try:
                    sessions = manager.get_sessions()
                    for s in sessions:
                        try:
                            app_id = (s.source_app_user_model_id or "").lower()
                        except Exception:
                            app_id = ""
                        if not app_id:
                            continue
                        for target in TARGET_APPS:
                            if target in app_id:
                                session = s
                                self.log(f"[SMTC] 会话来源: {app_id}")
                                break
                        if session:
                            break
                except Exception as e:
                    self.log(f"[SMTC] get_sessions 失败: {type(e).__name__}: {e}")

                if not session:
                    try:
                        session = manager.get_current_session()
                        if session:
                            try:
                                app_id = session.source_app_user_model_id or "?"
                            except Exception:
                                app_id = "?"
                            self.log(f"[SMTC] 回退 current_session: {app_id}")
                    except Exception as e:
                        self.log(f"[SMTC] get_current_session 失败: {type(e).__name__}: {e}")

                if session:
                    self.current_session = session
                    await self._read_session(session)
                else:
                    self.current_session = None
            except Exception as e:
                self.log(f"[SMTC] loop 异常: {type(e).__name__}: {e}")
            await asyncio.sleep(1.0)

    async def _read_session(self, session):
        try:
            props = await session.try_get_media_properties_async()
            new_title  = props.title or ""
            new_artist = props.artist or ""

            if (new_title, new_artist) != self.last_meta:
                self.log(f"[SMTC] 元数据: {new_title or '--'} - {new_artist or '--'}")
                self.music_title  = new_title
                self.music_artist = new_artist
                self.last_meta    = (new_title, new_artist)
                self.last_cover_data = None
                self.music_cover_jpeg = None
                self._last_pushed_cover = None

                try:
                    result = await self._get_cover(props)
                    if result:
                        data, theme = result
                        if data:
                            self.music_cover_jpeg = data
                            self.log(f"[SMTC] 封面更新: {len(data)} bytes")
                        self.music_theme = theme
                except Exception as e:
                    self.log(f"[SMTC] 封面获取失败: {type(e).__name__}: {e}")

                if self.ser:
                    self._push_meta(force=True)
                    if self.music_cover_jpeg:
                        self._push_cover()
        except Exception as e:
            self.log(f"[SMTC] 元数据读取失败: {type(e).__name__}: {e}")

        try:
            info = session.get_playback_info()
            self.music_playing = (info.playback_status ==
                wmc.GlobalSystemMediaTransportControlsSessionPlaybackStatus.PLAYING)
        except Exception as e:
            self.log(f"[SMTC] 播放状态失败: {type(e).__name__}: {e}")

        try:
            tl = session.get_timeline_properties()
            self.music_position = tl.position.total_seconds()
            self.music_duration = max(0.0, (tl.end_time - tl.start_time).total_seconds())
            self.base_wall = time.time()
        except Exception as e:
            self.log(f"[SMTC] 时间轴失败: {type(e).__name__}: {e}")

    @staticmethod
    def _extract_theme_color(img):
        try:
            small = img.resize((48, 48), Image.Resampling.BILINEAR)
            buckets = Counter()
            for r, g, b in list(small.getdata()):
                if max(r,g,b) < 40 or min(r,g,b) > 235: continue
                if (max(r,g,b) - min(r,g,b)) / max(r,g,b) < 0.10: continue
                buckets[((r>>5)<<5, (g>>5)<<5, (b>>5)<<5)] += 1
            if not buckets: return (30, 20, 50)
            r, g, b = buckets.most_common(1)[0][0]
            if max(r,g,b) < 90:
                scale = 90.0 / max(r,g,b) if max(r,g,b) > 0 else 1.0
                return (min(255,int(r*scale)), min(255,int(g*scale)), min(255,int(b*scale)))
            return (r, g, b)
        except: return (30, 20, 50)

    async def _get_cover(self, props):
        try:
            thumb = props.thumbnail
            if not thumb: return None
            stream = await thumb.open_read_async()
            buffer = Buffer(2 * 1024 * 1024)
            await stream.read_async(buffer, buffer.capacity, InputStreamOptions.READ_AHEAD)
            img = Image.open(BytesIO(bytes(bytearray(buffer)))).convert('RGB')
            img = img.resize((200, 200), Image.Resampling.BILINEAR)
            theme = self._extract_theme_color(img)
            out = BytesIO()
            img.save(out, format='JPEG', quality=75)
            data = out.getvalue()
            if data == self.last_cover_data: return (None, theme)
            self.last_cover_data = data
            return (data, theme)
        except: return None

    def _toggle_playback(self):
        if self.current_session and self.smtc_loop:
            asyncio.run_coroutine_threadsafe(self._async_toggle(), self.smtc_loop)

    async def _async_toggle(self):
        try:
            if self.music_playing: await self.current_session.try_pause_async()
            else: await self.current_session.try_play_async()
        except: pass

    def _skip_next(self):
        if self.current_session and self.smtc_loop:
            asyncio.run_coroutine_threadsafe(
                self.current_session.try_skip_next_async(), self.smtc_loop)

    def _skip_prev(self):
        if self.current_session and self.smtc_loop:
            asyncio.run_coroutine_threadsafe(
                self.current_session.try_skip_previous_async(), self.smtc_loop)


#GUI
class SMTCApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32同步")
        self.root.geometry("720x600")
        self.root.resizable(False, False)

        self.bridge = Bridge()
        self.connected = False
        self.hotkey_entries = {}
        self._log_count = 0

        self.tray_icon = None
        self._closing   = False

        self._build_ui()
        self._refresh_ports()
        self.root.after(100, self._poll_queues)

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.after(1500, self._auto_connect_if_needed)

   
    def _on_close(self):
        if self._closing:
            return
        if not HAS_TRAY:
            if messagebox.askyesno("退出", "确定退出程序吗？"):
                self._quit_app()
            return
        choice = messagebox.askyesnocancel(
            "关闭窗口",
            "是(Y)：隐藏到托盘 \n否(N)：退出程序",
        )
        if choice is None:
            return
        if choice:
            self._hide_to_tray()
        else:
            self._quit_app()

    def _make_tray_icon(self):
        icon_path = application_path / "icon.png"
        if icon_path.exists():
            return Image.open(icon_path).convert("RGBA").resize((64, 64))
        from PIL import ImageDraw
        img = Image.new('RGB', (64, 64), (24, 28, 40))
        d = ImageDraw.Draw(img)
        d.ellipse((6, 6, 58, 58), fill=(90, 170, 240), outline=(220, 235, 255), width=2)
        d.rectangle((26, 20, 38, 44), fill=(255, 255, 255))
        d.rectangle((22, 30, 42, 34), fill=(255, 200, 60))
        return img

    def _start_tray(self):
        if not HAS_TRAY or self.tray_icon is not None:
            return
        try:
            image = self._make_tray_icon()
            menu = pystray.Menu(
                TrayMenuItem('打开设置', self._tray_open_settings, default=True),
                TrayMenuItem('退出',     self._tray_quit),
            )
            self.tray_icon = pystray.Icon("SmtcBridge", image, "ESP32", menu)
            threading.Thread(target=self.tray_icon.run, daemon=True).start()
        except Exception as e:
            self._log(f"托盘启动失败: {e}")
            self.tray_icon = None

    def _hide_to_tray(self):
        if not HAS_TRAY:
            messagebox.showinfo("提示", "未安装 pystray，将直接退出")
            self._quit_app()
            return
        self._start_tray()
        self.root.withdraw()

    def _tray_open_settings(self, icon=None, item=None):
        try:
            self.root.after(0, self._restore_from_tray)
        except Exception:
            pass

    def _tray_quit(self, icon=None, item=None):
        try:
            self.root.after(0, self._quit_app)
        except Exception:
            pass

    def _restore_from_tray(self):
        if self.tray_icon is not None:
            try:
                self.tray_icon.stop()
            except Exception:
                pass
            self.tray_icon = None
        self.root.deiconify()
        self.root.lift()
        self.root.focus_force()

    def _quit_app(self):
        if self._closing:
            return
        self._closing = True
        if self.tray_icon is not None:
            try:
                self.tray_icon.stop()
            except Exception:
                pass
            self.tray_icon = None
        try:
            self.bridge.close()
        except Exception:
            pass
        try:
            self.root.quit()
            self.root.destroy()
        except Exception:
            pass


    def _auto_connect_if_needed(self):
        if not self.bridge.hotkeys.get("auto_connect", True):
            return
        if self.connected:
            return
        mode = self.bridge.hotkeys.get("comm_mode", "serial")
        ip   = self.bridge.hotkeys.get("wifi_ip", "")
        port = self.bridge.hotkeys.get("wifi_port", "81")
        self.comm_mode_var.set(mode)
        self._on_comm_mode_changed()
        if ip:
            self.wifi_ip_var.set(ip)
        if port:
            self.wifi_port_var.set(str(port))
        serial_port = ""
        if mode == "serial":
            ps = self.port_var.get()
            if ps:
                serial_port = ps.split(" - ")[0].strip()
        threading.Thread(
            target=self._auto_connect_worker,
            args=(mode, ip, port, serial_port),
            daemon=True,
        ).start()

    def _auto_connect_worker(self, mode, ip, port, serial_port):
        self._log(f"开始自连接（{mode}），最多尝试 3 次")
        for attempt in range(1, 4):
            if self._closing:
                return
            self._log(f"   → 第 {attempt}/3 次尝试")
            try:
                ok, msg = self._try_connect(mode, ip, port, serial_port)
            except Exception as e:
                ok, msg = False, f"异常: {type(e).__name__}: {e}"
            if ok:
                self.root.after(0, lambda m=msg: self._mark_connected(m))
                self._log(f"自连接成功: {msg}")
                return
            self._log(f"失败: {msg}")
            if attempt < 3:
                time.sleep(2.0)
        self._log("自连接 3 次均失败，请手动连接")

    def _try_connect(self, mode, ip, port, serial_port=""):
        try:
            if self.bridge.ser:
                self.bridge.close()
        except Exception:
            pass
        try:
            ws_port = int(str(port).strip() or "81")
        except ValueError:
            ws_port = 81
        self.bridge = Bridge(backend=mode, wifi_ip=ip, wifi_port=ws_port)
        if mode == "serial":
            target = serial_port or self.bridge.find_port()
            if not target:
                return False, "未找到串口"
            return self.bridge.start(port=target)
        else:
            if not ip:
                return False, "WiFi IP 为空"
            self.bridge.wifi_ip = ip
            self.bridge.wifi_port = ws_port
            return self.bridge.start()

    def _mark_connected(self, msg):
        self.connected = True
        self.connect_btn.config(text="断开")
        self.conn_status.config(text=f"已连接: {msg}", fg="green")
        self.root.after(500, self._auto_send_hwd)
        self.root.after(700, self._auto_send_clock)
        self.root.after(900, self._auto_send_weather_city)

    #UI
    def _build_ui(self):
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=6, pady=(6, 2))

        tab1 = ttk.Frame(self.notebook); self.notebook.add(tab1, text=" 连接 ")
        self._build_tab_conn(tab1)

        tab_net = ttk.Frame(self.notebook); self.notebook.add(tab_net, text=" 网络 ")
        self._build_tab_network(tab_net)

        tab2 = ttk.Frame(self.notebook); self.notebook.add(tab2, text=" 监视器 ")
        self._build_tab_monitor(tab2)

        tab3 = ttk.Frame(self.notebook); self.notebook.add(tab3, text=" 天气 ")
        self._build_tab_weather(tab3)

        tab4 = ttk.Frame(self.notebook); self.notebook.add(tab4, text=" 时钟 ")
        self._build_tab_clock(tab4)

        tab5b = ttk.Frame(self.notebook); self.notebook.add(tab5b, text=" 背景图 ")
        self._build_tab_bg(tab5b)

        tab6 = ttk.Frame(self.notebook); self.notebook.add(tab6, text=" 快捷键 ")
        self._build_tab_hotkeys(tab6)

        tab_sys = ttk.Frame(self.notebook)
        self.notebook.add(tab_sys, text=" 系统 ")
        self._build_tab_system(tab_sys)

        log_frame = ttk.LabelFrame(self.root, text="日志")
        log_frame.pack(fill=tk.X, padx=6, pady=(2, 6))
        self.log_text = tk.Text(log_frame, height=8, font=("Consolas", 9), state=tk.DISABLED)
        sb = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=sb.set)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        self.log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    def _build_tab_conn(self, parent):
        self.conn_tab = parent
        self.mode_frame = ttk.LabelFrame(parent, text="通信方式")
        self.mode_frame.pack(fill=tk.X, padx=10, pady=8)
        self.comm_mode_var = tk.StringVar(value=self.bridge.hotkeys.get("comm_mode", "serial"))
        ttk.Radiobutton(self.mode_frame, text="串口", value="serial",
                        variable=self.comm_mode_var,
                        command=self._on_comm_mode_changed).pack(side=tk.LEFT, padx=10, pady=6)
        ttk.Radiobutton(self.mode_frame, text="WiFi", value="wifi",
                        variable=self.comm_mode_var,
                        command=self._on_comm_mode_changed).pack(side=tk.LEFT, padx=10, pady=6)

        self.port_frame = ttk.LabelFrame(parent, text="串口参数")
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(self.port_frame, textvariable=self.port_var, width=40)
        self.port_combo.pack(side=tk.LEFT, padx=6, pady=6)
        ttk.Button(self.port_frame, text="刷新", command=self._refresh_ports, width=6).pack(side=tk.LEFT, padx=2)

        self.wifi_frame = ttk.LabelFrame(parent, text="WiFi 参数 (WebSocket)")
        tk.Label(self.wifi_frame, text="ESP32 IP:").pack(side=tk.LEFT, padx=6)
        self.wifi_ip_var = tk.StringVar(value=self.bridge.hotkeys.get("wifi_ip", ""))
        ttk.Entry(self.wifi_frame, textvariable=self.wifi_ip_var, width=18).pack(side=tk.LEFT, padx=2)
        tk.Label(self.wifi_frame, text="WS端口:").pack(side=tk.LEFT, padx=(10, 2))
        self.wifi_port_var = tk.StringVar(value=self.bridge.hotkeys.get("wifi_port", "81"))
        ttk.Entry(self.wifi_frame, textvariable=self.wifi_port_var, width=6).pack(side=tk.LEFT, padx=2)

        action_row = ttk.Frame(parent); action_row.pack(fill=tk.X, padx=10, pady=8)
        self.connect_btn = ttk.Button(action_row, text="连接", command=self._toggle_connect, width=8)
        self.connect_btn.pack(side=tk.LEFT, padx=6)
        self.conn_status = tk.Label(action_row, text="未连接", fg="gray")
        self.conn_status.pack(side=tk.LEFT, padx=8)

        status_frame = ttk.LabelFrame(parent, text="实时状态")
        status_frame.pack(fill=tk.X, padx=10, pady=8)
        self.cpu_var    = tk.StringVar(value="资源: --")
        self.title_var  = tk.StringVar(value="标题: --")
        self.artist_var = tk.StringVar(value="艺术家: --")
        self.state_var  = tk.StringVar(value="播放: --")
        tk.Label(status_frame, textvariable=self.cpu_var,    anchor="w", font=("Consolas", 11)).pack(fill=tk.X, padx=8, pady=2)
        tk.Label(status_frame, textvariable=self.title_var,  anchor="w", font=("Consolas", 10)).pack(fill=tk.X, padx=8, pady=2)
        tk.Label(status_frame, textvariable=self.artist_var, anchor="w", font=("Consolas", 10)).pack(fill=tk.X, padx=8, pady=2)
        tk.Label(status_frame, textvariable=self.state_var,  anchor="w", font=("Consolas", 10)).pack(fill=tk.X, padx=8, pady=2)

        self._on_comm_mode_changed()

    def _on_comm_mode_changed(self):
        m = self.comm_mode_var.get()
        self.port_frame.pack_forget()
        self.wifi_frame.pack_forget()
        if m == "serial":
            self.port_frame.pack(fill=tk.X, padx=10, pady=6, after=self.mode_frame)
        else:
            self.wifi_frame.pack(fill=tk.X, padx=10, pady=6, after=self.mode_frame)

    def _build_tab_network(self, parent):
        cfg = ttk.LabelFrame(parent, text="ESP32 WiFi 配置（先通过串口或已连接的 WiFi 下发）")
        cfg.pack(fill=tk.X, padx=10, pady=10)

        row1 = ttk.Frame(cfg); row1.pack(fill=tk.X, padx=6, pady=4)
        tk.Label(row1, text="SSID:", width=10, anchor="w").pack(side=tk.LEFT)
        self.ssid_var = tk.StringVar(value=self.bridge.hotkeys.get("wifi_ssid", ""))
        ttk.Entry(row1, textvariable=self.ssid_var, width=32).pack(side=tk.LEFT, padx=4)

        row2 = ttk.Frame(cfg); row2.pack(fill=tk.X, padx=6, pady=4)
        tk.Label(row2, text="密码:", width=10, anchor="w").pack(side=tk.LEFT)
        self.pwd_var = tk.StringVar(value=self.bridge.hotkeys.get("wifi_pwd", ""))
        ttk.Entry(row2, textvariable=self.pwd_var, width=32, show="*").pack(side=tk.LEFT, padx=4)

        row3 = ttk.Frame(cfg); row3.pack(fill=tk.X, padx=6, pady=6)
        ttk.Button(row3, text="保存并连接", command=self._wifi_cfg_send).pack(side=tk.LEFT, padx=4)
        ttk.Button(row3, text="关闭 WiFi", command=self._wifi_off_send).pack(side=tk.LEFT, padx=4)
        ttk.Button(row3, text="查询 IP", command=self._wifi_query_ip).pack(side=tk.LEFT, padx=4)

        info = ttk.LabelFrame(parent, text="ESP32 上报信息")
        info.pack(fill=tk.X, padx=10, pady=10)
        self.ip_reported_var   = tk.StringVar(value="—")
        self.ssid_reported_var = tk.StringVar(value="—")
        row4 = ttk.Frame(info); row4.pack(fill=tk.X, padx=8, pady=4)
        tk.Label(row4, text="当前 IP:", width=10, anchor="w").pack(side=tk.LEFT)
        tk.Label(row4, textvariable=self.ip_reported_var, anchor="w",
                 font=("Consolas", 11), fg="#0a0").pack(side=tk.LEFT)
        row5 = ttk.Frame(info); row5.pack(fill=tk.X, padx=8, pady=4)
        tk.Label(row5, text="已配置 SSID:", width=10, anchor="w").pack(side=tk.LEFT)
        tk.Label(row5, textvariable=self.ssid_reported_var, anchor="w",
                 font=("Consolas", 11), fg="#06c").pack(side=tk.LEFT)

    def _build_tab_monitor(self, parent):
        mon_frame = ttk.LabelFrame(parent, text="监视器资源")
        mon_frame.pack(fill=tk.X, padx=10, pady=10)
        row = ttk.Frame(mon_frame); row.pack(fill=tk.X, padx=6, pady=6)
        tk.Label(row, text="监控对象:").pack(side=tk.LEFT)
        self.res_type_var = tk.StringVar(value="cpu")
        self.res_combo = ttk.Combobox(row, textvariable=self.res_type_var,
                                      values=["cpu", "ram", "gpu"], width=10, state="readonly")
        self.res_combo.pack(side=tk.LEFT, padx=6)
        self.res_combo.bind("<<ComboboxSelected>>", self._on_res_type_changed)

        hwd_frame = ttk.LabelFrame(parent, text="硬件同步")
        hwd_frame.pack(fill=tk.X, padx=10, pady=10)
        row1 = ttk.Frame(hwd_frame); row1.pack(fill=tk.X, padx=6, pady=6)
        self.led_var = tk.BooleanVar(value=self.bridge.hotkeys.get("hwd_led", True))
        self.volt_var = tk.BooleanVar(value=self.bridge.hotkeys.get("hwd_volt", True))
        self.max_volt_var = tk.StringVar(value=self.bridge.hotkeys.get("hwd_max_volt", "4.8"))
        ttk.Checkbutton(row1, text="WS2812 灯", variable=self.led_var).pack(side=tk.LEFT, padx=6)
        ttk.Checkbutton(row1, text="PWM 电压", variable=self.volt_var).pack(side=tk.LEFT, padx=6)
        tk.Label(row1, text="  最大电压(V):").pack(side=tk.LEFT, padx=(10, 2))
        ttk.Entry(row1, textvariable=self.max_volt_var, width=6).pack(side=tk.LEFT)
        ttk.Button(row1, text="应用", command=self._apply_hwd_settings).pack(side=tk.LEFT, padx=10)

    def _build_tab_weather(self, parent):
        wx_frame = ttk.LabelFrame(parent, text="心知天气 Seniverse · 公钥签名")
        wx_frame.pack(fill=tk.X, padx=10, pady=10)
        row1 = ttk.Frame(wx_frame); row1.pack(fill=tk.X, padx=6, pady=4)
        tk.Label(row1, text="城市:", width=10, anchor="w").pack(side=tk.LEFT)
        self.wx_city_var = tk.StringVar(value=self.bridge.weather_city)
        ttk.Entry(row1, textvariable=self.wx_city_var, width=24).pack(side=tk.LEFT, padx=4)
        ttk.Button(row1, text="应用并刷新", command=self._apply_weather_city).pack(side=tk.LEFT, padx=8)

        row2 = ttk.Frame(wx_frame); row2.pack(fill=tk.X, padx=6, pady=4)
        tk.Label(row2, text="公钥(uid):", width=10, anchor="w").pack(side=tk.LEFT)
        self.wx_pk_var = tk.StringVar(value=self.bridge.hotkeys.get("weather_public_key", ""))
        ttk.Entry(row2, textvariable=self.wx_pk_var, width=42).pack(side=tk.LEFT, padx=4)

        row3 = ttk.Frame(wx_frame); row3.pack(fill=tk.X, padx=6, pady=4)
        tk.Label(row3, text="私钥(key):", width=10, anchor="w").pack(side=tk.LEFT)
        self.wx_sk_var = tk.StringVar(value=self.bridge.hotkeys.get("weather_private_key", ""))
        ttk.Entry(row3, textvariable=self.wx_sk_var, width=42, show="*").pack(side=tk.LEFT, padx=4)

        row4 = ttk.Frame(wx_frame); row4.pack(fill=tk.X, padx=6, pady=8)
        ttk.Button(row4, text="保存密钥并刷新", command=self._apply_weather_keys).pack(side=tk.LEFT, padx=8)

    def _build_tab_clock(self, parent):
        clock_frame = ttk.LabelFrame(parent, text="主界面时钟")
        clock_frame.pack(fill=tk.X, padx=10, pady=10)
        row1 = ttk.Frame(clock_frame); row1.pack(fill=tk.X, padx=6, pady=4)
        self.clock_en_var = tk.BooleanVar(value=self.bridge.hotkeys.get("clock_en", True))
        ttk.Checkbutton(row1, text="显示时钟", variable=self.clock_en_var).pack(side=tk.LEFT)
        tk.Label(row1, text="  X:").pack(side=tk.LEFT)
        self.clock_x_var = tk.StringVar(value=self.bridge.hotkeys.get("clock_x", "80"))
        ttk.Entry(row1, textvariable=self.clock_x_var, width=6).pack(side=tk.LEFT, padx=2)
        tk.Label(row1, text="Y:").pack(side=tk.LEFT)
        self.clock_y_var = tk.StringVar(value=self.bridge.hotkeys.get("clock_y", "20"))
        ttk.Entry(row1, textvariable=self.clock_y_var, width=6).pack(side=tk.LEFT, padx=2)
        tk.Label(row1, text="字号:").pack(side=tk.LEFT)
        self.clock_size_var = tk.StringVar(value=self.bridge.hotkeys.get("clock_size", "2"))
        ttk.Combobox(row1, textvariable=self.clock_size_var,
                     values=["1", "2", "3", "4"], width=3, state="readonly").pack(side=tk.LEFT, padx=2)

        row2 = ttk.Frame(clock_frame); row2.pack(fill=tk.X, padx=6, pady=4)
        tk.Label(row2, text="颜色 R:").pack(side=tk.LEFT)
        self.clock_r_var = tk.StringVar(value=self.bridge.hotkeys.get("clock_r", "255"))
        ttk.Entry(row2, textvariable=self.clock_r_var, width=4).pack(side=tk.LEFT, padx=2)
        tk.Label(row2, text="G:").pack(side=tk.LEFT)
        self.clock_g_var = tk.StringVar(value=self.bridge.hotkeys.get("clock_g", "255"))
        ttk.Entry(row2, textvariable=self.clock_g_var, width=4).pack(side=tk.LEFT, padx=2)
        tk.Label(row2, text="B:").pack(side=tk.LEFT)
        self.clock_b_var = tk.StringVar(value=self.bridge.hotkeys.get("clock_b", "255"))
        ttk.Entry(row2, textvariable=self.clock_b_var, width=4).pack(side=tk.LEFT, padx=2)
        ttk.Button(row2, text="应用", command=self._apply_clock).pack(side=tk.LEFT, padx=10)

    def _build_tab_bg(self, parent):
        bg_frame = ttk.LabelFrame(parent, text="背景图（240×320 RGB565）")
        bg_frame.pack(fill=tk.X, padx=10, pady=10)
        self.bg_file_var = tk.StringVar(value="未选择")
        tk.Label(bg_frame, textvariable=self.bg_file_var,
                 anchor="w", fg="#555").pack(fill=tk.X, padx=6, pady=4)
        row = ttk.Frame(bg_frame); row.pack(fill=tk.X, padx=6, pady=6)
        ttk.Button(row, text="选择图片并发送", command=self._pick_and_send_bg).pack(side=tk.LEFT, padx=4)
        ttk.Button(row, text="重发上次", command=self._resend_bg).pack(side=tk.LEFT, padx=4)
        ttk.Button(row, text="查询分区", command=self._query_fs).pack(side=tk.LEFT, padx=4)
        ttk.Button(row, text="恢复默认", command=self._reset_bg).pack(side=tk.LEFT, padx=4)

    def _build_tab_hotkeys(self, parent):
        hk_frame = ttk.LabelFrame(parent, text="快捷键设置")
        hk_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        header = ttk.Frame(hk_frame); header.pack(fill=tk.X, padx=6, pady=4)
        ttk.Label(header, text="按键", width=8).pack(side=tk.LEFT)
        ttk.Label(header, text="动作", width=8).pack(side=tk.LEFT, padx=2)
        ttk.Label(header, text="快捷键", width=36).pack(side=tk.LEFT, padx=2)
        ttk.Label(header, text="操作", width=14).pack(side=tk.LEFT)
        for key_name in ["KEY1", "KEY2", "KEY3"]:
            self._build_hotkey_group(hk_frame, key_name)
        ttk.Separator(hk_frame, orient='horizontal').pack(fill=tk.X, padx=10, pady=8)
        tk.Label(hk_frame, text="编码器（仅背景图状态生效）",
                 font=("微软雅黑", 9, "bold"), fg="#666").pack(anchor="w", padx=10)
        self._build_hotkey_group(hk_frame, "ENC")
        btn_row = ttk.Frame(hk_frame); btn_row.pack(fill=tk.X, padx=6, pady=10)
        ttk.Button(btn_row, text="保存快捷键", command=self._save_hotkeys).pack(side=tk.LEFT, padx=4)
        ttk.Button(btn_row, text="恢复默认", command=self._reset_hotkeys).pack(side=tk.LEFT, padx=4)

    def _build_tab_system(self, parent):
        boot = ttk.LabelFrame(parent, text="启动")
        boot.pack(fill=tk.X, padx=10, pady=10)

        row1 = ttk.Frame(boot); row1.pack(fill=tk.X, padx=6, pady=6)
        self.autostart_var = tk.BooleanVar(
            value=is_autostart_enabled() if HAS_WINREG else False)
        chk = ttk.Checkbutton(
            row1,
            text="开机自启动（写入 HKCU\\...\\CurrentVersion\\Run）",
            variable=self.autostart_var,
            command=self._on_autostart_toggle,
        )
        chk.pack(side=tk.LEFT)
        if not HAS_WINREG:
            chk.state(["disabled"])

        row2 = ttk.Frame(boot); row2.pack(fill=tk.X, padx=6, pady=6)
        self.auto_connect_var = tk.BooleanVar(
            value=self.bridge.hotkeys.get("auto_connect", True))
        ttk.Checkbutton(
            row2,
            text="启动时自动连接（最多 3 次，间隔 2 秒）",
            variable=self.auto_connect_var,
            command=self._on_auto_connect_toggle,
        ).pack(side=tk.LEFT)

        tip = ttk.LabelFrame(parent, text="关闭行为 / 托盘")
        tip.pack(fill=tk.X, padx=10, pady=10)
        desc = (
            "• 点击关闭按钮会弹窗询问：\n"
            "    是(Y) → 隐藏到系统托盘（继续在后台运行）\n"
            "    否(N) → 直接退出程序\n"
            "    取消 → 不做任何操作\n"
            "• 隐藏到托盘后：右键托盘图标 → 打开设置 / 退出"
        )
        tk.Label(tip, text=desc, justify="left", anchor="w",
                 font=("微软雅黑", 9), fg="#444").pack(fill=tk.X, padx=8, pady=6)

        if not HAS_TRAY:
            tk.Label(parent, foreground="red",
                     text="未安装 pystray，托盘功能不可用（pip install pystray）"
                     ).pack(padx=10, pady=4, anchor="w")

    def _on_autostart_toggle(self):
        if not HAS_WINREG:
            self.autostart_var.set(False)
            messagebox.showwarning("提示", "当前环境不支持注册表操作")
            return
        enable = bool(self.autostart_var.get())
        if set_autostart(enable):
            self.bridge.hotkeys["auto_start"] = enable
            self.bridge.save_hotkeys()
            self._log(f"开机自启动: {'开启' if enable else '关闭'}")
        else:
            self.autostart_var.set(not enable)
            messagebox.showerror("失败", "写入注册表失败")

    def _on_auto_connect_toggle(self):
        enable = bool(self.auto_connect_var.get())
        self.bridge.hotkeys["auto_connect"] = enable
        self.bridge.save_hotkeys()
        self._log(f"启动自连接: {'开启' if enable else '关闭'}")

    def _wifi_cfg_send(self):
        ssid = self.ssid_var.get().strip()
        pwd  = self.pwd_var.get()
        if not ssid:
            messagebox.showwarning("提示", "请输入 WiFi SSID")
            return
        if not self.connected:
            messagebox.showwarning("提示", "请先连接（串口或 WiFi）")
            return
        self.bridge.hotkeys["wifi_ssid"] = ssid
        self.bridge.hotkeys["wifi_pwd"]  = pwd
        self.bridge.save_hotkeys()
        self.bridge.send(f"WIFI_CFG:{ssid}|{pwd}\n")
        self._log(f"发送 WIFI_CFG: {ssid}")

    def _wifi_off_send(self):
        if not self.connected:
            messagebox.showwarning("提示", "请先连接"); return
        self.bridge.send("WIFI_OFF\n")
        self._log("发送 WIFI_OFF")

    def _wifi_query_ip(self):
        if not self.connected:
            messagebox.showwarning("提示", "请先连接"); return
        self.bridge.send("IP_QUERY\n")
        self._log("发送 IP_QUERY")

    def _apply_weather_city(self):
        city = self.wx_city_var.get().strip()
        if not city:
            messagebox.showwarning("提示", "请输入城市名"); return
        self.bridge.set_weather_city(city)
        self._log(f"天气城市: {city}")

    def _apply_weather_keys(self):
        pk = self.wx_pk_var.get().strip()
        sk = self.wx_sk_var.get().strip()
        if not pk or not sk:
            messagebox.showwarning("提示", "请填写公钥和私钥"); return
        self.bridge.set_weather_keys(pk, sk)
        self._log("心知天气密钥对已保存")

    def _apply_clock(self):
        try:
            x = int(self.clock_x_var.get()); y = int(self.clock_y_var.get())
            sz = int(self.clock_size_var.get())
            r = max(0, min(255, int(self.clock_r_var.get())))
            g = max(0, min(255, int(self.clock_g_var.get())))
            b = max(0, min(255, int(self.clock_b_var.get())))
            en = 1 if self.clock_en_var.get() else 0
            if self.connected:
                self.bridge.send(f"CLOCK_SET:{x}|{y}|{sz}|{r}|{g}|{b}\n", quiet=True)
                self.bridge.send(f"CLOCK_EN:{en}\n", quiet=True)
            self.bridge.hotkeys.update({
                "clock_en": self.clock_en_var.get(),
                "clock_x": str(x), "clock_y": str(y),
                "clock_size": str(sz),
                "clock_r": str(r), "clock_g": str(g), "clock_b": str(b),
            })
            self.bridge.save_hotkeys()
            messagebox.showinfo("成功", f"时钟已应用")
        except Exception as e:
            messagebox.showerror("错误", f"参数不合法: {e}")

    def _apply_hwd_settings(self):
        try:
            mv = float(self.max_volt_var.get())
            mv = max(0.0, min(4.8, mv))
            self.max_volt_var.set(f"{mv:.1f}")
        except:
            mv = 4.8; self.max_volt_var.set("4.8")
        led = 1 if self.led_var.get() else 0
        volt = 1 if self.volt_var.get() else 0
        if self.connected:
            self.bridge.send(f"HWD_SET:{led}|{volt}|{mv:.1f}\n", quiet=True)
        self.bridge.hotkeys.update({
            "hwd_led": self.led_var.get(),
            "hwd_volt": self.volt_var.get(),
            "hwd_max_volt": f"{mv:.1f}",
        })
        self.bridge.save_hotkeys()
        messagebox.showinfo("成功", "硬件设置已应用")

    def _query_fs(self):
        if not self.connected:
            messagebox.showwarning("提示", "请先连接"); return
        self.bridge.send("BG_DUMP\n")

    def _pick_and_send_bg(self):
        if self.bridge.bg_sending:
            messagebox.showwarning("提示", "正在发送中，请稍候"); return
        path = filedialog.askopenfilename(filetypes=[("图片", "*.jpg *.jpeg *.png *.bmp")])
        if not path: return
        try:
            img = Image.open(path)
            self.bg_file_var.set(f"{Path(path).name} ({img.size[0]}×{img.size[1]})")
            if self.bridge.send_bg_image(img):
                messagebox.showinfo("成功", "背景图已开始发送")
            else:
                messagebox.showerror("失败", "发送失败")
        except Exception as e:
            messagebox.showerror("错误", str(e))

    def _resend_bg(self):
        if not self.connected:
            messagebox.showwarning("提示", "请先连接"); return
        if self.bridge.resend_bg_image():
            messagebox.showinfo("成功", "正在重发上次背景图")
        else:
            messagebox.showwarning("提示", "没有可重发的背景图")

    def _reset_bg(self):
        if not self.connected:
            messagebox.showwarning("提示", "请先连接"); return
        if not messagebox.askyesno("确认", "删除 ESP32 Flash 里的背景图？"):
            return
        self.bridge.send("BG_RESET\n")

    def _save_hotkeys(self):
        for k, v in self.hotkey_entries.items():
            self.bridge.hotkeys[k] = v.get().strip()
        if self.bridge.save_hotkeys():
            messagebox.showinfo("成功", "快捷键配置已保存")

    def _reset_hotkeys(self):
        if not messagebox.askyesno("确认", "恢复所有快捷键为默认？"): return
        for k, v in self.hotkey_entries.items():
            v.set(DEFAULT_HOTKEYS.get(k, ""))

    def _on_res_type_changed(self, e=None):
        self.bridge.resource_type = self.res_type_var.get()
        self._log(f"监控: {self.res_type_var.get().upper()}")

    def _build_hotkey_group(self, parent, key_name):
        group = ttk.Frame(parent); group.pack(fill=tk.X, padx=6, pady=(2, 0))
        suffixes = ["_L", "_R", "_P"] if key_name == "ENC" else ["", "_D"]
        base_display = "ENC" if key_name == "ENC" else key_name
        row_labels = {"_L": "左旋", "_R": "右旋", "_P": "按下"} if key_name == "ENC" else {"": "单击", "_D": "双击"}
        for suffix in suffixes:
            full_key = key_name + suffix
            row = ttk.Frame(group); row.pack(fill=tk.X, pady=1)
            ttk.Label(row, text=base_display, width=8).pack(side=tk.LEFT)
            ttk.Label(row, text=row_labels[suffix], width=8).pack(side=tk.LEFT, padx=2)
            var = tk.StringVar(value=self.bridge.hotkeys.get(full_key, ""))
            ttk.Entry(row, textvariable=var, width=36).pack(side=tk.LEFT, padx=2)
            self.hotkey_entries[full_key] = var
            ttk.Button(row, text="捕获", command=lambda k=full_key, v=var: self._capture_hotkey(k, v),
                       width=5).pack(side=tk.LEFT, padx=2)
            ttk.Button(row, text="清空", command=lambda v=var: v.set(""),
                       width=5).pack(side=tk.LEFT, padx=2)

    def _refresh_ports(self):
        items = [f"{p.device} - {p.description}" for p in serial.tools.list_ports.comports()]
        self.port_combo['values'] = items
        if items and not self.port_var.get():
            self.port_combo.current(0)

    def _toggle_connect(self):
        if self.connected:
            try:
                self.bridge.close()
            except Exception:
                pass
            self.connected = False
            self.connect_btn.config(text="连接")
            self.conn_status.config(text="未连接", fg="gray")
            mode = self.comm_mode_var.get()
            try:
                ws_port = int(self.wifi_port_var.get().strip() or "81")
            except ValueError:
                ws_port = 81
            self.bridge = Bridge(
                backend=mode,
                wifi_ip=self.wifi_ip_var.get().strip(),
                wifi_port=ws_port,
            )
            self.bridge.hotkeys["comm_mode"] = mode
            self.bridge.hotkeys["wifi_ip"]   = self.wifi_ip_var.get().strip()
            self.bridge.hotkeys["wifi_port"] = str(ws_port)
            self.bridge.save_hotkeys()
            return

        mode = self.comm_mode_var.get()
        ip   = self.wifi_ip_var.get().strip()
        port = self.wifi_port_var.get().strip()
        serial_port = ""
        if mode == "serial":
            ps = self.port_var.get()
            if not ps:
                messagebox.showwarning("提示", "请先选择串口")
                return
            serial_port = ps.split(" - ")[0].strip()
        else:
            if not ip:
                messagebox.showwarning("提示", "请填写 ESP32 IP")
                return

        ok, msg = self._try_connect(mode, ip, port, serial_port)
        if ok:
            self._mark_connected(msg)
            self.bridge.hotkeys["comm_mode"] = mode
            self.bridge.hotkeys["wifi_ip"]   = ip
            self.bridge.hotkeys["wifi_port"] = port or "81"
            self.bridge.save_hotkeys()
        else:
            messagebox.showerror("连接失败", msg)

    def _auto_send_hwd(self):
        try: mv = float(self.max_volt_var.get())
        except: mv = 4.8
        led = 1 if self.led_var.get() else 0
        volt = 1 if self.volt_var.get() else 0
        if self.connected:
            self.bridge.send(f"HWD_SET:{led}|{volt}|{mv:.1f}\n", quiet=True)

    def _auto_send_clock(self):
        try:
            x = int(self.clock_x_var.get()); y = int(self.clock_y_var.get())
            sz = int(self.clock_size_var.get())
            r = int(self.clock_r_var.get()); g = int(self.clock_g_var.get()); b = int(self.clock_b_var.get())
            en = 1 if self.clock_en_var.get() else 0
            if self.connected:
                self.bridge.send(f"CLOCK_SET:{x}|{y}|{sz}|{r}|{g}|{b}\n", quiet=True)
                self.bridge.send(f"CLOCK_EN:{en}\n", quiet=True)
        except: pass

    def _auto_send_weather_city(self):
        city = self.wx_city_var.get().strip()
        if city and self.connected:
            self.bridge.set_weather_city(city)

    def _capture_hotkey(self, key_name, var):
        popup = tk.Toplevel(self.root)
        popup.title(f"捕获 {key_name}")
        popup.geometry("520x370")
        popup.transient(self.root); popup.grab_set()
        tk.Label(popup, text=f"捕获 [{key_name}]", font=("微软雅黑", 12, "bold")).pack(pady=8)
        captured = tk.StringVar(value=var.get())
        ttk.Entry(popup, textvariable=captured, font=("Consolas", 14),
                  justify="center").pack(fill=tk.X, padx=16, pady=4)
        status_label = tk.Label(popup, text="点击捕获或手动输入",
                                font=("微软雅黑", 9), fg="#888")
        status_label.pack(pady=(0, 4))
        cap_state = {"active": False}

        def start_capture():
            if cap_state["active"] or not HAS_KEYBOARD: return
            cap_state["active"] = True
            cap_btn.config(text="请按下快捷键…", state=tk.DISABLED)
            captured.set("")
            status_label.config(text="正在捕获…", fg="blue")
            def worker():
                try:
                    keyboard._pressed_events.clear()
                    keyboard.unhook_all()
                    mods, keys = set(), []
                    result_hk = ""
                    deadline = time.time() + 15
                    while time.time() < deadline:
                        e = keyboard.read_event(suppress=True)
                        if e.event_type == keyboard.KEY_DOWN:
                            name = e.name
                            if name in ("ctrl","left ctrl","right ctrl"): mods.add("ctrl")
                            elif name in ("shift","left shift","right shift"): mods.add("shift")
                            elif name in ("alt","left alt","right alt"): mods.add("alt")
                            elif name in ("windows","left windows","right windows"): mods.add("windows")
                            else:
                                if name not in keys: keys.append(name)
                        elif e.event_type == keyboard.KEY_UP:
                            name = e.name
                            if name in ("ctrl","shift","alt","windows","left ctrl","right ctrl",
                                        "left shift","right shift","left alt","right alt",
                                        "left windows","right windows"): continue
                            if keys:
                                order = ["windows","ctrl","shift","alt"]
                                result_hk = "+".join([m for m in order if m in mods] + keys)
                                break
                    def done():
                        if result_hk:
                            captured.set(result_hk)
                            status_label.config(text=f"{result_hk}", fg="green")
                        else:
                            status_label.config(text="超时", fg="red")
                        cap_btn.config(text="开始捕获", state=tk.NORMAL)
                        cap_state["active"] = False
                    self.root.after(0, done)
                except: pass
            threading.Thread(target=worker, daemon=True).start()

        cap_btn = ttk.Button(popup, text="开始捕获", command=start_capture)
        cap_btn.pack(pady=6)
        ttk.Button(popup, text="确定",
                   command=lambda: [var.set(captured.get().strip()), popup.destroy()]).pack()

    def _log(self, msg):
        ts = time.strftime("%H:%M:%S")
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.insert(tk.END, f"[{ts}] {msg}\n")
        self._log_count += 1
        if self._log_count >= 50:
            self._log_count = 0
            lines = int(self.log_text.index('end-1c').split('.')[0])
            if lines > 200: self.log_text.delete('1.0', f'{lines - 200}.0')
        self.log_text.see(tk.END)
        self.log_text.configure(state=tk.DISABLED)

    def _poll_queues(self):
        try:
            n = 0
            while n < 50:
                self._log(self.bridge.log_queue.get_nowait()); n += 1
        except queue.Empty: pass
        try:
            while True:
                s = self.bridge.status_queue.get_nowait()
                if 'cpu' in s:
                    self.cpu_var.set(f"资源: {s.get('res_type','cpu').upper()} {s['cpu']:.1f}%")
                if 'music_title' in s:
                    self.title_var.set(f"标题: {s['music_title'] or '--'}")
                    self.artist_var.set(f"艺术家: {s['music_artist'] or '--'}")
                    self.state_var.set(f"播放: {'是' if s.get('music_playing') else '否'}")
        except queue.Empty: pass
        if self.bridge.reported_ip:
            self.ip_reported_var.set(self.bridge.reported_ip)
        if self.bridge.reported_ssid:
            self.ssid_reported_var.set(self.bridge.reported_ssid)
        self.root.after(100, self._poll_queues)


def main():
    root = tk.Tk()
    app = SMTCApp(root)
    try:
        root.mainloop()
    finally:
        try:
            app.bridge.close()
        except Exception:
            pass
        try:
            if app.tray_icon is not None:
                app.tray_icon.stop()
        except Exception:
            pass


if __name__ == "__main__":
    main()
