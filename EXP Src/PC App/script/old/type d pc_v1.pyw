# typed_viewer_xbox_theme_plus_ext.py
# Type-D PC Viewer — OG Xbox-themed UDP monitor
# - Main telemetry :50504 (XboxStatus: <iii32s>)
# - Extended SMBus :50505 (tray/avpack/pic/xboxVer/enc/w/h: <iiiiiii>)
# - Realistic 7-segment digits (bevel), defined black border
# - °C/°F toggle, min/max for FAN/CPU/AMBIENT
# - AV Pack decoding supports both common PIC schemes (0x00..0x07 and 0x00/0x02/0x06/0x0A/0x0E)

import socket, struct, threading
import tkinter as tk
from tkinter import ttk

# ---- Ports / wire formats ----
PORT_MAIN = 50504
FMT_MAIN  = "<iii32s"                 # fan%, cpu°C, ambient°C, char[32] app
SIZE_MAIN = struct.calcsize(FMT_MAIN) # 44

PORT_EXT  = 50505
FMT_EXT   = "<iiiiiii"                # tray, av, pic, xboxver, enc, width, height
SIZE_EXT  = struct.calcsize(FMT_EXT)  # 28

# ---- Theme (OG Xbox) ----
CLR_BG      = "#0b1d0b"
CLR_PANEL   = "#0f220f"
CLR_EDGE    = "#1c3a1c"
CLR_TEXT    = "#baf5ba"
CLR_ACCENT  = "#66ff33"

# Segment colors
SEG_ON     = CLR_ACCENT
SEG_OFF    = "#153315"
BORDER_ON  = "#000000"
BORDER_OFF = ""
BORDER_W   = 2
BORDER_JOIN= "round"

# Bevel colors (subtle)
BEVEL_HI   = "#a6ff8c"
BEVEL_SH   = "#0c3a0c"
BEVEL_W    = 2

# ---- SMBus decode tables ----
TRAY_LABELS = {0x00: "Closed", 0x01: "Open", 0x02: "Busy"}

AVPACK_TABLE_PRIMARY = {
    0x00: "SCART", 0x01: "HDTV (Component)", 0x02: "VGA", 0x03: "RFU",
    0x04: "Advanced (S-Video)", 0x05: "Undefined",
    0x06: "Standard (Composite)", 0x07: "Missing/Disconnected",
}
AVPACK_TABLE_FALLBACK = {
    0x00: "None/Disconnected", 0x02: "Standard (Composite)",
    0x06: "Advanced (S-Video)", 0x0A: "HDTV (Component)", 0x0E: "SCART",
}

def decode_avpack(raw_val: int):
    v = int(raw_val) & 0xFF
    if v in AVPACK_TABLE_PRIMARY: return AVPACK_TABLE_PRIMARY[v], v
    m = v & 0x0E
    if m in AVPACK_TABLE_FALLBACK: return AVPACK_TABLE_FALLBACK[m], v
    return "Unknown", v

# Heuristics we can do on the viewer side
def av_is_hd(av_raw: int) -> bool:
    """Component/VGA imply progressive SD when 480/576; also true HD modes."""
    v = int(av_raw) & 0xFF
    return (v == 0x01) or (v == 0x02) or ((v & 0x0E) == 0x0A)  # HDTV (comp) or VGA

def sd_system_from_height(h: int) -> str:
    return "PAL" if int(h) >= 570 else "NTSC"

def mode_from_resolution(width: int, height: int, av_raw: int) -> str | None:
    """Return a label like '480i', '480p', '576i', '576p', '720p', '1080i' when we can."""
    try:
        w, h = int(width), int(height)
    except Exception:
        return None
    if w <= 0 or h <= 0: return None

    # Exact HD cases
    if w >= 1900 and h == 1080:  # firmware reports 1920 x 1080
        return "1080i"           # Xbox outputs interlaced for 1080
    if w == 1280 and h == 720:
        return "720p"

    # SD cases (use AV pack to guess interlace vs progressive)
    if (w in (640, 704, 720)) and h == 480:
        return "480p" if av_is_hd(av_raw) else "480i"
    if (w in (720,)) and h == 576:
        return "576p" if av_is_hd(av_raw) else "576i"

    return None

# Xbox version + encoder
XBOXVER_LABELS = {0x00:"v1.0",0x01:"v1.1",0x02:"v1.2",0x03:"v1.3",0x04:"v1.4",0x05:"v1.5",0x06:"v1.6"}
ENCODER_LABELS = {0x45:"Conexant", 0x6A:"Focus", 0x70:"Xcalibur"}

def decode_xboxver(val: int):
    v = int(val)
    if v < 0 or (v & 0xFF) == 0xFF:
        return "Not reported"
    return XBOXVER_LABELS.get(v & 0xFF, f"Unknown ({v})")

def decode_encoder(val: int):
    v = int(val) & 0xFF
    return ENCODER_LABELS.get(v, f"Unknown (0x{v:02X})")

# --- helper to robustly assign enc/width/height regardless of field order ---
def _assign_enc_res(a, b, c):
    candidates = [a, b, c]
    enc = None
    for x in candidates:
        if (int(x) & 0xFF) in ENCODER_LABELS:
            enc = x; break
    if enc is None:
        for x in candidates:
            if 0 <= int(x) <= 0xFF:
                enc = x; break
    if enc is None:
        enc = a
    rest = [x for x in candidates if x is not enc]
    if len(rest) == 2:
        w, h = rest[0], rest[1]
    else:
        w, h = 0, 0
    return int(enc), int(w), int(h)

# ---- Seven-segment widget (unchanged) ----
class SevenSeg(tk.Canvas):
    SEG_MAP = {
        '0': "abcdef", '1': "bc", '2': "abged", '3': "abgcd", '4': "fgbc",
        '5': "afgcd", '6': "afgcde", '7': "abc", '8': "abcdefg", '9': "abcfgd",
        '-': "g", ' ': ""
    }
    def __init__(self, master, digits=3, seg_width=14, seg_len=52, pad=8, gap=12,
                 show_dots=False, bg=CLR_PANEL, **kw):
        self.digits   = digits; self.seg_w = seg_width; self.seg_l = seg_len
        self.pad = pad; self.gap = gap; self.show_dots = show_dots
        self.digit_w  = seg_len + seg_width + pad*2 + gap
        self.digit_h  = seg_len*2 + seg_width + pad*4
        width, height = self.digit_w * digits - gap, self.digit_h
        super().__init__(master, width=width, height=height, bg=bg,
                         highlightthickness=0, bd=0, **kw)
        self._digits = []
        for i in range(digits):
            x0 = i * self.digit_w
            self._digits.append(self._create_digit(x0, 0))
        self._dp_items = []
        for i in range(digits):
            x0 = i * self.digit_w
            r  = self.seg_w // 2 + 1
            cx = x0 + self.digit_w - self.pad - self.gap - r
            cy = self.digit_h - self.pad - r
            dp = self.create_oval(cx-r, cy-r, cx+r, cy+r, fill=SEG_OFF, outline="", width=0)
            self._dp_items.append(dp); self.itemconfigure(dp, state="hidden")
        self.set("")
    def _hseg_poly(self, x, y):
        sw, sl, p = self.seg_w, self.seg_l, self.pad
        return [(x+p, y+p),(x+p+sl, y+p),(x+p+sl+sw//2, y+p+sw//2),
                (x+p+sl, y+p+sw),(x+p, y+p+sw),(x+p-sw//2, y+p+sw//2)]
    def _vseg_poly(self, x, y):
        sw, sl, p = self.seg_w, self.seg_l, self.pad
        return [(x+p, y+p+sw//2),(x+p+sw, y+p),(x+p+sw, y+p+sl),
                (x+p, y+p+sl+sw//2),(x+p-sw//2, y+p+sl),(x+p-sw//2, y+p+sw//2)]
    def _create_digit(self, x, y):
        parts = {}
        pts = self._hseg_poly(x, y); parts['a'] = self._make_segment(pts,'h')
        pts = self._vseg_poly(x + self.seg_l + self.seg_w//2, y); parts['b'] = self._make_segment(pts,'v')
        pts = self._vseg_poly(x + self.seg_l + self.seg_w//2, y + self.seg_l); parts['c'] = self._make_segment(pts,'v')
        pts = self._hseg_poly(x, y + 2*self.seg_l); parts['d'] = self._make_segment(pts,'h')
        pts = self._vseg_poly(x, y + self.seg_l); parts['e'] = self._make_segment(pts,'v')
        pts = self._vseg_poly(x, y); parts['f'] = self._make_segment(pts,'v')
        pts = self._hseg_poly(x, y + self.seg_l); parts['g'] = self._make_segment(pts,'h')
        return parts
    def _make_segment(self, pts, orientation='h'):
        body = self.create_polygon(pts, fill=SEG_OFF, outline=BORDER_OFF, width=0, joinstyle=BORDER_JOIN)
        if orientation == 'h':
            hi = self.create_line(pts[0][0]+1, pts[0][1]+1, pts[1][0]-1, pts[1][1]+1, fill=BEVEL_HI, width=BEVEL_W, capstyle="round")
            sh = self.create_line(pts[4][0]+1, pts[4][1]-1, pts[3][0]-1, pts[3][1]-1, fill=BEVEL_SH, width=BEVEL_W, capstyle="round")
        else:
            hi = self.create_line(pts[0][0]+1, pts[0][1], pts[5][0]+1, pts[5][1], fill=BEVEL_HI, width=BEVEL_W, capstyle="round")
            sh = self.create_line(pts[2][0]-1, pts[2][1], pts[1][0]-1, pts[1][1], fill=BEVEL_SH, width=BEVEL_W, capstyle="round")
        self.itemconfigure(hi, state="hidden"); self.itemconfigure(sh, state="hidden")
        return {'body': body, 'hi': hi, 'sh': sh}
    def set(self, text):
        s = str(text); dots = [False]*self.digits; chars = []
        for ch in s:
            if ch == '.':
                if chars: dots[len(chars)-1] = True
                continue
            chars.append(ch)
        chars = chars[-self.digits:]; chars = [' '] * (self.digits - len(chars)) + chars
        for i, ch in enumerate(chars):
            segs_on = self.SEG_MAP.get(ch, ""); parts = self._digits[i]
            for name, seg in parts.items():
                if name in segs_on:
                    self.itemconfigure(seg['body'], fill=SEG_ON, outline=BORDER_ON, width=BORDER_W, joinstyle=BORDER_JOIN)
                    self.itemconfigure(seg['hi'], state="normal"); self.itemconfigure(seg['sh'], state="normal")
                else:
                    self.itemconfigure(seg['body'], fill=SEG_OFF, outline=BORDER_OFF, width=0)
                    self.itemconfigure(seg['hi'], state="hidden"); self.itemconfigure(seg['sh'], state="hidden")

# ---- App ----------------------------------------------------------------------
class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Type-D PC Viewer")
        self.configure(bg=CLR_BG)
        self.resizable(False, False)

        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TFrame", background=CLR_BG)
        style.configure("Panel.TFrame", background=CLR_PANEL)
        style.configure("TLabel", background=CLR_BG, foreground=CLR_TEXT)
        style.configure("Panel.TLabel", background=CLR_PANEL, foreground=CLR_TEXT)
        style.configure("Title.TLabel", background=CLR_BG, foreground=CLR_ACCENT, font=("Segoe UI", 13, "bold"))
        style.configure("Small.TLabel", background=CLR_BG, foreground=CLR_TEXT, font=("Segoe UI", 9))
        style.configure("TCheckbutton", background=CLR_BG, foreground=CLR_TEXT)

        container = ttk.Frame(self, style="TFrame", padding=12)
        container.grid(row=0, column=0, sticky="nsew")

        ttk.Label(container, text="TYPE-D LIVE TELEMETRY", style="Title.TLabel")\
            .grid(row=0, column=0, columnspan=3, sticky="w", pady=(0,10))

        fan_panel = ttk.Frame(container, style="Panel.TFrame", padding=12)
        cpu_panel = ttk.Frame(container, style="Panel.TFrame", padding=12)
        amb_panel = ttk.Frame(container, style="Panel.TFrame", padding=12)
        app_panel = ttk.Frame(container, style="Panel.TFrame", padding=12)
        ext_panel = ttk.Frame(container, style="Panel.TFrame", padding=12)

        fan_panel.grid(row=1, column=0, padx=(0,10), pady=6, sticky="nsew")
        cpu_panel.grid(row=1, column=1, padx=10,  pady=6, sticky="nsew")
        amb_panel.grid(row=1, column=2, padx=(10,0), pady=6, sticky="nsew")
        app_panel.grid(row=2, column=0, columnspan=3, padx=0, pady=(6,0), sticky="nsew")
        ext_panel.grid(row=3, column=0, columnspan=3, padx=0, pady=(6,0), sticky="nsew")

        for f in (fan_panel, cpu_panel, amb_panel, app_panel, ext_panel):
            f.configure(borderwidth=2)
            tk.Frame(f, bg=CLR_EDGE, height=2).place(relx=0, rely=0, relwidth=1, height=2)

        # --- Fan
        ttk.Label(fan_panel, text="FAN (%)", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.seg_fan = SevenSeg(fan_panel, digits=3, bg=CLR_PANEL)
        self.seg_fan.grid(row=1, column=0, pady=(6,0))
        self.lbl_fan_minmax = ttk.Label(fan_panel, text="min —   max —", style="Panel.TLabel")
        self.lbl_fan_minmax.grid(row=2, column=0, sticky="e")

        # --- CPU
        ttk.Label(cpu_panel, text="CPU TEMP", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.seg_cpu = SevenSeg(cpu_panel, digits=3, bg=CLR_PANEL)
        self.seg_cpu.grid(row=1, column=0, pady=(6,0))
        self.var_unit = tk.StringVar(value="°C")
        self.lbl_cpu_minmax = ttk.Label(cpu_panel, text="min —   max —   °C", style="Panel.TLabel")
        self.lbl_cpu_minmax.grid(row=2, column=0, sticky="e")

        # --- Ambient
        ttk.Label(amb_panel, text="AMBIENT", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.seg_amb = SevenSeg(amb_panel, digits=3, bg=CLR_PANEL)
        self.seg_amb.grid(row=1, column=0, pady=(6,0))
        self.lbl_amb_minmax = ttk.Label(amb_panel, text="min —   max —   °C", style="Panel.TLabel")
        self.lbl_amb_minmax.grid(row=2, column=0, sticky="e")

        # --- Current App
        ttk.Label(app_panel, text="CURRENT APP", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.var_app = tk.StringVar(value="—")
        ttk.Label(app_panel, textvariable=self.var_app, style="Panel.TLabel", font=("Consolas", 12))\
            .grid(row=1, column=0, sticky="w", pady=(4,0))

        # --- Extended SMBus
        ttk.Label(ext_panel, text="EXTENDED SMBUS STATUS", style="Panel.TLabel")\
            .grid(row=0, column=0, sticky="w", pady=(0,4))
        ext_grid = ttk.Frame(ext_panel, style="Panel.TFrame")
        ext_grid.grid(row=1, column=0, sticky="w")

        ttk.Label(ext_grid, text="Tray:", style="Panel.TLabel").grid(row=0, column=0, sticky="e", padx=(0,6))
        self.var_tray = tk.StringVar(value="—"); ttk.Label(ext_grid, textvariable=self.var_tray, style="Panel.TLabel").grid(row=0, column=1, sticky="w")

        ttk.Label(ext_grid, text="AV Pack:", style="Panel.TLabel").grid(row=1, column=0, sticky="e", padx=(0,6))
        self.var_av = tk.StringVar(value="—"); ttk.Label(ext_grid, textvariable=self.var_av, style="Panel.TLabel").grid(row=1, column=1, sticky="w")

        ttk.Label(ext_grid, text="PIC Ver:", style="Panel.TLabel").grid(row=2, column=0, sticky="e", padx=(0,6))
        self.var_pic = tk.StringVar(value="—"); ttk.Label(ext_grid, textvariable=self.var_pic, style="Panel.TLabel").grid(row=2, column=1, sticky="w")

        ttk.Label(ext_grid, text="Xbox Ver:", style="Panel.TLabel").grid(row=3, column=0, sticky="e", padx=(0,6))
        self.var_xboxver = tk.StringVar(value="—"); ttk.Label(ext_grid, textvariable=self.var_xboxver, style="Panel.TLabel").grid(row=3, column=1, sticky="w")

        ttk.Label(ext_grid, text="Encoder:", style="Panel.TLabel").grid(row=4, column=0, sticky="e", padx=(0,6))
        self.var_encoder = tk.StringVar(value="—"); ttk.Label(ext_grid, textvariable=self.var_encoder, style="Panel.TLabel").grid(row=4, column=1, sticky="w")

        ttk.Label(ext_grid, text="Video Res:", style="Panel.TLabel").grid(row=5, column=0, sticky="e", padx=(0,6))
        self.var_res = tk.StringVar(value="—"); ttk.Label(ext_grid, textvariable=self.var_res, style="Panel.TLabel").grid(row=5, column=1, sticky="w")

        # Footer: °C/°F + status
        footer = ttk.Frame(container, style="TFrame")
        footer.grid(row=4, column=0, columnspan=3, sticky="ew", pady=(10,0))
        self.show_f = tk.BooleanVar(value=False)
        ttk.Checkbutton(footer, text="Show Fahrenheit (°F)", variable=self.show_f,
                        command=self._refresh_units, style="TCheckbutton").pack(side="left")
        self.var_status = tk.StringVar(value=f"Listening on UDP :{PORT_MAIN} (main) / :{PORT_EXT} (ext)")
        ttk.Label(footer, textvariable=self.var_status, style="Small.TLabel").pack(side="right")

        # --- Min/Max state
        self.fan_min = None; self.fan_max = None
        self.cpu_min = None; self.cpu_max = None
        self.amb_min = None; self.amb_max = None

        # Threads
        self._stop = False
        self._t_main = threading.Thread(target=self._listen_main, daemon=True); self._t_main.start()
        self._t_ext  = threading.Thread(target=self._listen_ext,  daemon=True); self._t_ext.start()

    # ---- Unit helpers ----
    @staticmethod
    def c_to_f(c): return int(round((c * 9/5) + 32))
    def _refresh_units(self):
        if self.show_f.get():
            self.lbl_cpu_minmax.configure(text=self._minmax_text(self._to_f(self.cpu_min), self._to_f(self.cpu_max), "°F"))
            self.lbl_amb_minmax.configure(text=self._minmax_text(self._to_f(self.amb_min), self._to_f(self.amb_max), "°F"))
        else:
            self.lbl_cpu_minmax.configure(text=self._minmax_text(self.cpu_min, self.cpu_max, "°C"))
            self.lbl_amb_minmax.configure(text=self._minmax_text(self.amb_min, self.amb_max, "°C"))
    def _to_f(self, v): return None if v is None else self.c_to_f(v)
    def _fmt3(self, n):
        try: n = int(n)
        except Exception: return "—"
        if n < -99: return "-99"
        if n > 999: return "999"
        return f"{n:d}"
    def _minmax_text(self, mn, mx, unit=""):
        if mn is None or mx is None: return f"min —   max —   {unit}".rstrip()
        return f"min {int(mn)}   max {int(mx)}   {unit}".rstrip()

    # ---- Renderers ----
    def _render_main(self, fan, cpu_c, amb_c, app):
        fan   = max(0, min(100, int(fan)))
        cpu_c = max(-100, min(200, int(cpu_c)))
        amb_c = max(-100, min(200, int(amb_c)))
        self.fan_min = fan if self.fan_min is None else min(self.fan_min, fan)
        self.fan_max = fan if self.fan_max is None else max(self.fan_max, fan)
        self.cpu_min = cpu_c if self.cpu_min is None else min(self.cpu_min, cpu_c)
        self.cpu_max = cpu_c if self.cpu_max is None else max(self.cpu_max, cpu_c)
        self.amb_min = amb_c if self.amb_min is None else min(self.amb_min, amb_c)
        self.amb_max = amb_c if self.amb_max is None else max(self.amb_max, amb_c)
        if self.show_f.get():
            cpu_show = self.c_to_f(cpu_c);  amb_show = self.c_to_f(amb_c)
            self.lbl_cpu_minmax.configure(text=self._minmax_text(self._to_f(self.cpu_min), self._to_f(self.cpu_max), "°F"))
            self.lbl_amb_minmax.configure(text=self._minmax_text(self._to_f(self.amb_min), self._to_f(self.amb_max), "°F"))
        else:
            cpu_show = cpu_c; amb_show = amb_c
            self.lbl_cpu_minmax.configure(text=self._minmax_text(self.cpu_min, self.cpu_max, "°C"))
            self.lbl_amb_minmax.configure(text=self._minmax_text(self.amb_min, self.amb_max, "°C"))
        self.lbl_fan_minmax.configure(text=self._minmax_text(self.fan_min, self.fan_max))
        self.seg_fan.set(self._fmt3(fan))
        self.seg_cpu.set(self._fmt3(cpu_show))
        self.seg_amb.set(self._fmt3(amb_show))
        self.var_app.set(app if app else "—")

    def _render_ext(self, tray_state, av_state, pic_ver, xbox_ver, enc, v_w, v_h):
        tray_txt = TRAY_LABELS.get(int(tray_state), f"Unknown ({tray_state})")
        av_label, raw = decode_avpack(av_state)
        self.var_tray.set(tray_txt)
        self.var_av.set(f"{av_label} (0x{raw:02X})")
        self.var_pic.set(str(int(pic_ver)))
        self.var_xboxver.set(decode_xboxver(xbox_ver))
        self.var_encoder.set(decode_encoder(enc))

        # Resolution + Mode label
        vw, vh = int(v_w), int(v_h)
        if 160 <= vw <= 4096 and 120 <= vh <= 2160:
            mode = mode_from_resolution(vw, vh, av_state)
            if mode:
                # Include system tag for SD where helpful
                sys_tag = ""
                if mode.startswith(("480", "576")):
                    sys_tag = f" {sd_system_from_height(vh)}"
                self.var_res.set(f"{vw} x {vh} ({mode}{sys_tag})")
            else:
                self.var_res.set(f"{vw} x {vh}")
        else:
            self.var_res.set("—")

    # ---- UDP listeners ----
    def _listen_main(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try: s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        except OSError: pass
        s.bind(("", PORT_MAIN))
        while not self._stop:
            try: data, addr = s.recvfrom(4096)
            except OSError: break
            if len(data) != SIZE_MAIN:
                self.after(0, self.var_status.set, f"Main: got {len(data)} bytes from {addr[0]} (expected {SIZE_MAIN})")
                continue
            fan, cpu_c, amb_c, app_raw = struct.unpack(FMT_MAIN, data)
            app = app_raw.split(b"\x00", 1)[0].decode("utf-8", errors="ignore")
            self.after(0, self._render_main, fan, cpu_c, amb_c, app)
            self.after(0, self.var_status.set, f"OK from {addr[0]} — main/ext active")

    def _listen_ext(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try: s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        except OSError: pass
        s.bind(("", PORT_EXT))
        while not self._stop:
            try: data, addr = s.recvfrom(4096)
            except OSError: break
            if len(data) != SIZE_EXT:
                self.after(0, self.var_status.set, f"Ext: got {len(data)} bytes from {addr[0]} (expected {SIZE_EXT})")
                continue
            t, a, p, xb, x5, x6, x7 = struct.unpack(FMT_EXT, data)
            enc, vw, vh = _assign_enc_res(x5, x6, x7)
            self.after(0, self._render_ext, t, a, p, xb, enc, vw, vh)

    def on_close(self):
        self._stop = True
        self.destroy()

if __name__ == "__main__":
    app = App()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()
