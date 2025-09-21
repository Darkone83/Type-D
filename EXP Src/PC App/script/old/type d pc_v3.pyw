# typed_viewer_xbox_theme_plus_ext.py
# Type-D PC Viewer — OG Xbox-themed UDP monitor
# - Main telemetry :50504 (XboxStatus: <iii32s>)
# - Extended SMBus :50505 (tray/avpack/pic/xboxVer/enc/w/h: <iiiiiii>)
# - EEPROM one-shot :50506
#     Accepts:
#       EE:SN=...|MAC=...|REG=...|HDD=...|RAW=<base64 256B>   (new, labeled)
#       EE:SER=...|MAC=...|REG=...|HDD=...                   (older labeled)
#       EE:RAW=<base64 256B>                                 (raw only)
# - Realistic 7-segment digits (bevel)
# - °C/°F toggle, min/max
# - Graph mode toggle for FAN/CPU/AMBIENT (cur/min/max)
# - NEW: PNG capture, EEPROM→clipboard (incl. RAW), CSV logging, window icon

import socket, struct, threading, base64, binascii
import tkinter as tk
import time, os, sys, csv
from tkinter import ttk, filedialog, messagebox

# Optional Pillow for screen capture
try:
    from PIL import ImageGrab
    PIL_AVAILABLE = True
except Exception:
    PIL_AVAILABLE = False

# ---- Ports / wire formats ----
PORT_MAIN = 50504
FMT_MAIN  = "<iii32s"                 # fan%, cpu°C, ambient°C, char[32] app
SIZE_MAIN = struct.calcsize(FMT_MAIN) # 44

PORT_EXT  = 50505
FMT_EXT   = "<iiiiiii"                # tray, av, pic, xboxver, enc, width, height
SIZE_EXT  = struct.calcsize(FMT_EXT)  # 28

PORT_EE   = 50506                     # EEPROM broadcast

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

# Bevel colors
BEVEL_HI   = "#a6ff8c"
BEVEL_SH   = "#0c3a0c"
BEVEL_W    = 2

# ---- EEPROM decode (PRIMARY MAP + FALLBACKS) ----
EE_OFFSETS = {
    "HDD_KEY": (0x04, 16),
    "MAC":     (0x40, 6),
    "REGION":  (0x58, 1),
    "SERIAL":  (0x34, 12),
}
EE_FALLBACKS = [
    {"HDD_KEY": (0x50, 16), "MAC": (0x3C, 6), "REGION": (0x58, 1), "SERIAL": (0x14, 12)},
    {"HDD_KEY": (0x04, 16), "MAC": (0x24, 6), "REGION": (0x58, 1), "SERIAL": (0x09, 12)},
]
EE_REGION_MAP = {0x00: "NTSC-U", 0x01: "NTSC-J", 0x02: "PAL"}

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

def av_is_hd(av_raw: int) -> bool:
    v = int(av_raw) & 0xFF
    return (v == 0x01) or (v == 0x02) or ((v & 0x0E) == 0x0A)

def sd_system_from_height(h: int) -> str:
    return "PAL" if int(h) >= 570 else "NTSC"

def mode_from_resolution(width: int, height: int, av_raw: int):
    try:
        w, h = int(width), int(height)
    except Exception:
        return None
    if w <= 0 or h <= 0: return None
    if w >= 1900 and h == 1080: return "1080i"
    if w == 1280 and h == 720:  return "720p"
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
    if v < 0 or (v & 0xFF) == 0xFF: return "Not reported"
    return XBOXVER_LABELS.get(v & 0xFF, f"Unknown ({v})")

def decode_encoder(val: int):
    v = int(val) & 0xFF
    return ENCODER_LABELS.get(v, f"Unknown (0x{v:02X})")

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
    if enc is None: enc = a
    rest = [x for x in candidates if x is not enc]
    if len(rest) == 2: w, h = rest[0], rest[1]
    else: w, h = 0, 0
    return int(enc), int(w), int(h)

# ===================== Serial→Version (with factory hint) =====================
def _parse_serial(serial_txt: str):
    s = "".join(ch for ch in (serial_txt or "") if ch.isdigit())
    if len(s) < 12: return None
    try:
        y  = int(s[7]); ww = int(s[8:10]); ff = int(s[10:12])
        return (y, ww, ff)
    except Exception:
        return None

def _guess_ver_by_serial(serial_txt: str):
    p = _parse_serial(serial_txt)
    if not p: return None
    y, ww, ff = p
    group = 10*y + (ww // 10)
    base = None
    if group in (20, 21): base = "v1.0"
    elif group == 23:
        base = "v1.0–1.1"
        if ff == 3: base = "v1.0"
        elif ff in (5, 6): base = "v1.1"
    elif group in (24, 25): base = "v1.1"
    elif group == 30: base = "v1.2"
    elif group in (31, 32): base = "v1.3"
    elif group == 33: base = "v1.4–1.5"
    elif group == 41: base = "v1.6"
    elif group == 43: base = "v1.6b"
    return base

def _range_from_encoder(enc_byte: int):
    v = int(enc_byte) & 0xFF
    if v == 0x45: return "v1.0–1.3"
    if v == 0x6A: return "v1.4–1.5"
    if v == 0x70: return "v1.6"
    return None

# ---- Mini graph widget (grid + current dot + value label) ---------------------
class MiniGraph(tk.Canvas):
    def __init__(self, master, width=260, height=110, bg=CLR_PANEL, fg=CLR_ACCENT, **kw):
        super().__init__(master, width=width, height=height, bg=bg,
                         highlightthickness=0, bd=0, **kw)
        self.w = width; self.h = height
        self.fg = CLR_TEXT
        self.pad = 8

    def draw(self, values, value_suffix=""):
        self.delete("all")
        if not values: return

        vmin = min(values); vmax = max(values)
        if vmin == vmax: vmin -= 1; vmax += 1
        span = vmax - vmin
        vmin -= 0.05 * span; vmax += 0.05 * span

        inner_w = self.w - 2*self.pad
        inner_h = self.h - 2*self.pad

        self.create_rectangle(1, 1, self.w-2, self.h-2, outline=CLR_EDGE, width=2)
        for i in range(1, 5):
            x = self.pad + i * (inner_w / 5.0)
            y = self.pad + i * (inner_h / 5.0)
            self.create_line(x, self.pad, x, self.h-self.pad, fill=CLR_EDGE)
            self.create_line(self.pad, y, self.w-self.pad, y, fill=CLR_EDGE)

        n = len(values)
        if n == 1:
            x_last = self.pad
            y_last = self.pad + inner_h * (1 - (values[0]-vmin)/(vmax-vmin))
            self.create_oval(x_last-3, y_last-3, x_last+3, y_last+3, fill="yellow", outline="")
            return

        step = float(inner_w) / float(n-1)
        pts = []
        for i, v in enumerate(values):
            x = self.pad + i*step
            y = self.pad + inner_h * (1 - (float(v)-vmin)/(vmax-vmin))
            pts.extend((x, y))
        self.create_line(*pts, fill=self.fg, width=2, smooth=False)

        x_last = self.pad + (n-1)*step
        y_last = self.pad + inner_h * (1 - (values[-1]-vmin)/(vmax-vmin))
        x_last = min(self.w - self.pad - 3, max(self.pad + 3, x_last))
        y_last = min(self.h - self.pad - 3, max(self.pad + 3, y_last))
        self.create_oval(x_last-3, y_last-3, x_last+3, y_last+3, fill="yellow", outline="")

        label = f"{int(round(values[-1]))}{value_suffix}"
        margin = 6
        right_thresh = self.w - self.pad - 60
        if x_last > right_thresh:
            lx = x_last - margin; anchor = "e"
        else:
            lx = x_last + margin; anchor = "w"
        ly = max(self.pad+10, min(self.h - self.pad - 10, y_last))
        self.create_text(lx, ly, text=label, fill=self.fg, anchor=anchor, font=("Segoe UI", 9, "bold"))

# ---- Seven-segment widget -----------------------------------------------------
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
        self._ee_raw_b64 = None

        self._apply_icon()

        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TFrame", background=CLR_BG)
        style.configure("Panel.TFrame", background=CLR_PANEL)
        style.configure("TLabel", background=CLR_BG, foreground=CLR_TEXT)
        style.configure("Panel.TLabel", background=CLR_PANEL, foreground=CLR_TEXT)
        style.configure("Title.TLabel", background=CLR_BG, foreground=CLR_ACCENT, font=("Segoe UI", 13, "bold"))
        style.configure("Small.TLabel", background=CLR_BG, foreground=CLR_TEXT)
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
        ee_panel  = ttk.Frame(container, style="Panel.TFrame", padding=12)

        fan_panel.grid(row=1, column=0, padx=(0,10), pady=6, sticky="nsew")
        cpu_panel.grid(row=1, column=1, padx=10,  pady=6, sticky="nsew")
        amb_panel.grid(row=1, column=2, padx=(10,0), pady=6, sticky="nsew")
        app_panel.grid(row=2, column=0, columnspan=3, padx=0, pady=(6,0), sticky="nsew")
        ext_panel.grid(row=3, column=0, columnspan=3, padx=0, pady=(6,0), sticky="nsew")
        ee_panel.grid(row=4, column=0, columnspan=3, padx=0, pady=(6,0), sticky="nsew")

        for f in (fan_panel, cpu_panel, amb_panel, app_panel, ext_panel, ee_panel):
            f.configure(borderwidth=2)
            tk.Frame(f, bg=CLR_EDGE, height=2).place(relx=0, rely=0, relwidth=1, height=2)

        # --- Fan
        ttk.Label(fan_panel, text="FAN (%)", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.seg_fan = SevenSeg(fan_panel, digits=3, bg=CLR_PANEL); self.seg_fan.grid(row=1, column=0, pady=(6,0))
        self.graph_fan = MiniGraph(fan_panel); self.graph_fan.grid(row=1, column=0, pady=(6,0)); self.graph_fan.grid_remove()
        self.lbl_fan_minmax = ttk.Label(fan_panel, text="min —   max —", style="Panel.TLabel"); self.lbl_fan_minmax.grid(row=2, column=0, sticky="e")

        # --- CPU
        ttk.Label(cpu_panel, text="CPU TEMP", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.seg_cpu = SevenSeg(cpu_panel, digits=3, bg=CLR_PANEL); self.seg_cpu.grid(row=1, column=0, pady=(6,0))
        self.graph_cpu = MiniGraph(cpu_panel); self.graph_cpu.grid(row=1, column=0, pady=(6,0)); self.graph_cpu.grid_remove()
        self.var_unit = tk.StringVar(value="°C")
        self.lbl_cpu_minmax = ttk.Label(cpu_panel, text="min —   max —   °C", style="Panel.TLabel"); self.lbl_cpu_minmax.grid(row=2, column=0, sticky="e")

        # --- Ambient
        ttk.Label(amb_panel, text="AMBIENT", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.seg_amb = SevenSeg(amb_panel, digits=3, bg=CLR_PANEL); self.seg_amb.grid(row=1, column=0, pady=(6,0))
        self.graph_amb = MiniGraph(amb_panel); self.graph_amb.grid(row=1, column=0, pady=(6,0)); self.graph_amb.grid_remove()
        self.lbl_amb_minmax = ttk.Label(amb_panel, text="min —   max —   °C", style="Panel.TLabel"); self.lbl_amb_minmax.grid(row=2, column=0, sticky="e")

        # --- Current App
        ttk.Label(app_panel, text="CURRENT APP", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.var_app = tk.StringVar(value="—")
        ttk.Label(app_panel, textvariable=self.var_app, style="Panel.TLabel", font=("Consolas", 12))\
            .grid(row=1, column=0, sticky="w", pady=(4,0))

        # --- Extended SMBus
        ttk.Label(ext_panel, text="EXTENDED SMBUS STATUS", style="Panel.TLabel")\
            .grid(row=0, column=0, sticky="w", pady=(0,4))
        ext_grid = ttk.Frame(ext_panel, style="Panel.TFrame"); ext_grid.grid(row=1, column=0, sticky="w")

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

        # --- EEPROM panel
        ttk.Label(ee_panel, text="EEPROM DATA", style="Panel.TLabel")\
            .grid(row=0, column=0, sticky="w", pady=(0,4))
        ee_grid = ttk.Frame(ee_panel, style="Panel.TFrame"); ee_grid.grid(row=1, column=0, sticky="w")
        ttk.Label(ee_grid, text="Serial:", style="Panel.TLabel").grid(row=0, column=0, sticky="e", padx=(0,6))
        self.var_ee_serial = tk.StringVar(value="—"); ttk.Label(ee_grid, textvariable=self.var_ee_serial, style="Panel.TLabel").grid(row=0, column=1, sticky="w")

        ttk.Label(ee_grid, text="MAC:", style="Panel.TLabel").grid(row=1, column=0, sticky="e", padx=(0,6))
        self.var_ee_mac = tk.StringVar(value="—"); ttk.Label(ee_grid, textvariable=self.var_ee_mac, style="Panel.TLabel").grid(row=1, column=1, sticky="w")

        ttk.Label(ee_grid, text="Region:", style="Panel.TLabel").grid(row=2, column=0, sticky="e", padx=(0,6))
        self.var_ee_region = tk.StringVar(value="—"); ttk.Label(ee_grid, textvariable=self.var_ee_region, style="Panel.TLabel").grid(row=2, column=1, sticky="w")

        ttk.Label(ee_grid, text="HDD Key:", style="Panel.TLabel").grid(row=3, column=0, sticky="e", padx=(0,6))
        self.var_ee_hdd = tk.StringVar(value="—"); ttk.Label(ee_grid, textvariable=self.var_ee_hdd, style="Panel.TLabel").grid(row=3, column=1, sticky="w")
        self.show_hdd = tk.BooleanVar(value=False)
        ttk.Checkbutton(ee_grid, text="Reveal", variable=self.show_hdd, command=self._refresh_hdd, style="TCheckbutton").grid(row=3, column=2, padx=(8,0), sticky="w")

        # Footer: toggles + status
        footer = ttk.Frame(container, style="TFrame"); footer.grid(row=5, column=0, columnspan=3, sticky="ew", pady=(10,0))
        self.show_f = tk.BooleanVar(value=False)
        ttk.Checkbutton(footer, text="Show Fahrenheit (°F)", variable=self.show_f,
                        command=self._refresh_units, style="TCheckbutton").pack(side="left", padx=(0,12))
        self.graph_mode = tk.BooleanVar(value=False)
        ttk.Checkbutton(footer, text="Graph mode (charts)", variable=self.graph_mode,
                        command=self._toggle_graph_mode, style="TCheckbutton").pack(side="left")
        self.var_status = tk.StringVar(value=f"Listening on UDP :{PORT_MAIN} (main) / :{PORT_EXT} (ext) / :{PORT_EE} (eeprom)")
        ttk.Label(footer, textvariable=self.var_status, style="Small.TLabel").pack(side="right")

        # --- Menus (NEW)
        self._menu = tk.Menu(self); self.config(menu=self._menu)
        m_file = tk.Menu(self._menu, tearoff=0)
        m_file.add_command(label="Save PNG…", command=self.do_save_png)
        m_file.add_separator()
        m_file.add_command(label="Exit", command=self.on_close)
        self._menu.add_cascade(label="File", menu=m_file)

        self._menu_tools = tk.Menu(self._menu, tearoff=0)
        self._log_menu_index = 0
        self._menu_tools.add_command(label="Start Logging…", command=self.toggle_logging)
        self._menu_tools.add_command(label="Copy EEPROM to Clipboard", command=self.do_copy_eeprom)
        self._menu_tools.add_command(label="Copy EEPROM RAW (base64)", command=self.do_copy_eeraw)
        self._menu.add_cascade(label="Tools", menu=self._menu_tools)

        # --- Min/Max state
        self.fan_min = None; self.fan_max = None
        self.cpu_min = None; self.cpu_max = None   # track in °C
        self.amb_min = None; self.amb_max = None   # track in °C

        # --- History for graphs (keep ~10 minutes by wall time)
        self.hist_window_sec = 600.0
        self.hist_fan  = []   # list[(t,val)]
        self.hist_cpuC = []
        self.hist_ambC = []

        # --- EEPROM cache + version inference state
        self._ee_hdd_full = None
        self._ee_serial_txt = None
        self._ver_from_serial = None
        self._last_enc = None
        self._last_smc_code = None

        # --- Logging state (NEW)
        self._log_fp = None
        self._log_csv = None
        self._log_rows = 0

        # Threads
        self._stop = False
        self._t_main = threading.Thread(target=self._listen_main, daemon=True); self._t_main.start()
        self._t_ext  = threading.Thread(target=self._listen_ext,  daemon=True); self._t_ext.start()
        self._t_ee   = threading.Thread(target=self._listen_ee,   daemon=True); self._t_ee.start()

    # ---- Icon/resource helper (NEW) ----
    def _apply_icon(self):
        try:
            base = getattr(sys, "_MEIPASS", os.path.abspath("."))
            ico_path = os.path.join(base, "dc.ico")
            if os.path.exists(ico_path):
                # Windows accepts .ico directly
                self.iconbitmap(ico_path)
        except Exception:
            pass  # non-Windows or missing icon; ignore

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
        if self.graph_mode.get():
            self._update_graphs()
    def _to_f(self, v):
        return None if v is None else self.c_to_f(v)
    def _fmt3(self, n):
        try: n = int(n)
        except Exception: return "—"
        if n < -99: return "-99"
        if n > 999: return "999"
        return f"{n:d}"
    def _minmax_text(self, mn, mx, unit=""):
        if mn is None or mx is None:
            return f"min —   max —   {unit}".rstrip()
        return f"min {int(mn)}   max {int(mx)}   {unit}".rstrip()

    # ---- HDD key masking ----
    def _refresh_hdd(self):
        if not self._ee_hdd_full:
            return
        if self.show_hdd.get():
            self.var_ee_hdd.set(self._ee_hdd_full)
        else:
            s = self._ee_hdd_full
            if len(s) >= 8:
                self.var_ee_hdd.set(s[:4] + "…" * 6 + s[-4:])
            else:
                self.var_ee_hdd.set("…")

    # ---- Graph mode toggle ----
    def _toggle_graph_mode(self):
        if self.graph_mode.get():
            self.seg_fan.grid_remove(); self.graph_fan.grid()
            self.seg_cpu.grid_remove(); self.graph_cpu.grid()
            self.seg_amb.grid_remove(); self.graph_amb.grid()
            self._update_graphs()
        else:
            self.graph_fan.grid_remove(); self.seg_fan.grid()
            self.graph_cpu.grid_remove(); self.seg_cpu.grid()
            self.graph_amb.grid_remove(); self.seg_amb.grid()
            self._refresh_units()

    # ---- Graph helpers ----
    def _trim_hist(self, hist):
        cutoff = time.time() - self.hist_window_sec
        while hist and hist[0][0] < cutoff:
            hist.pop(0)

    def _recent_values(self, hist):
        cutoff = time.time() - self.hist_window_sec
        return [v for (t, v) in hist if t >= cutoff]

    def _update_graphs(self):
        fan_vals   = self._recent_values(self.hist_fan)
        cpu_vals_c = self._recent_values(self.hist_cpuC)
        amb_vals_c = self._recent_values(self.hist_ambC)

        if self.show_f.get():
            cpu_vals = [self.c_to_f(v) for v in cpu_vals_c]
            amb_vals = [self.c_to_f(v) for v in amb_vals_c]
            unit = "°F"
        else:
            cpu_vals = cpu_vals_c
            amb_vals = amb_vals_c
            unit = "°C"

        self.graph_fan.draw(fan_vals, value_suffix="%")
        self.graph_cpu.draw(cpu_vals, value_suffix=unit)
        self.graph_amb.draw(amb_vals, value_suffix=unit)

        if fan_vals:
            cur = int(fan_vals[-1]); mn = int(min(fan_vals)); mx = int(max(fan_vals))
            self.lbl_fan_minmax.configure(text=f"cur {cur}   min {mn}   max {mx}")
        if cpu_vals:
            cur = int(cpu_vals[-1]); mn = int(min(cpu_vals)); mx = int(max(cpu_vals))
            self.lbl_cpu_minmax.configure(text=f"cur {cur}   min {mn}   max {mx}   {unit}")
        if amb_vals:
            cur = int(amb_vals[-1]); mn = int(min(amb_vals)); mx = int(max(amb_vals))
            self.lbl_amb_minmax.configure(text=f"cur {cur}   min {mn}   max {mx}   {unit}")

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

        now = time.time()
        self.hist_fan.append((now, fan));    self._trim_hist(self.hist_fan)
        self.hist_cpuC.append((now, cpu_c)); self._trim_hist(self.hist_cpuC)
        self.hist_ambC.append((now, amb_c)); self._trim_hist(self.hist_ambC)

        # CSV logging (NEW)
        if self._log_csv:
            ts = time.strftime("%Y-%m-%dT%H:%M:%S")
            try:
                self._log_csv.writerow([ts, fan, cpu_c, amb_c, (app or "").strip()])
                self._log_rows += 1
                if self._log_rows % 10 == 0:  # flush every 10 rows
                    self._log_fp.flush()
            except Exception as e:
                self.var_status.set(f"Log write failed: {e}")
                self.stop_logging()

        if self.graph_mode.get():
            self._update_graphs()

    def _refresh_version_label(self):
        if self._ver_from_serial:
            label = f"{self._ver_from_serial} (serial)"
            if self._last_enc is not None:
                enc_range = _range_from_encoder(self._last_enc)
                if enc_range == "v1.6" and not self._ver_from_serial.startswith("v1.6"):
                    label = f"{enc_range} (encoder)"
            self.var_xboxver.set(label); return

        if self._last_enc is not None:
            enc_range = _range_from_encoder(self._last_enc)
            if enc_range:
                self.var_xboxver.set(f"{enc_range} (encoder)"); return

        if self._last_smc_code is not None:
            self.var_xboxver.set(f"{decode_xboxver(self._last_smc_code)} (SMC)"); return

        self.var_xboxver.set("Not reported")

    def _render_ext(self, tray_state, av_state, pic_ver, xbox_ver, enc, v_w, v_h):
        tray_txt = TRAY_LABELS.get(int(tray_state), f"Unknown ({tray_state})")
        av_label, raw = decode_avpack(av_state)
        self.var_tray.set(tray_txt)
        self.var_av.set(f"{av_label} (0x{raw:02X})")
        self.var_pic.set(str(int(pic_ver)))
        self._last_smc_code = int(xbox_ver)
        self.var_encoder.set(decode_encoder(enc))
        self._last_enc = int(enc) & 0xFF
        self._refresh_version_label()

        vw, vh = int(v_w), int(v_h)
        if 160 <= vw <= 4096 and 120 <= vh <= 2160:
            mode = mode_from_resolution(vw, vh, av_state)
            if mode:
                sys_tag = ""
                if mode.startswith(("480", "576")):
                    sys_tag = f" {sd_system_from_height(vh)}"
                self.var_res.set(f"{vw} x {vh} ({mode}{sys_tag})")
            else:
                self.var_res.set(f"{vw} x {vh}")
        else:
            self.var_res.set("—")

    # ---- EEPROM render ----
    def _render_ee(self, serial_txt, mac_txt, region_txt, hdd_hex):
        self.var_ee_serial.set((serial_txt or "").strip() or "—")
        self.var_ee_mac.set((mac_txt or "").strip() or "—")
        self.var_ee_region.set((region_txt or "").strip() or "—")
        self._ee_hdd_full = (hdd_hex or "").strip()
        self._refresh_hdd()
        self._ee_serial_txt = (serial_txt or "").strip()
        self._ver_from_serial = _guess_ver_by_serial(self._ee_serial_txt)
        self._refresh_version_label()

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
            self.after(0, self.var_status.set, f"OK from {addr[0]} — main/ext/ee active")

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

    def _listen_ee(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try: s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        except OSError: pass
        s.bind(("", PORT_EE))
        while not self._stop:
            try: data, addr = s.recvfrom(65535)
            except OSError: break
            txt = data.decode("ascii", "ignore").strip()
            if not txt.startswith("EE:"):
                continue

            try:
                print("[EE RX]", txt[:120] + ("…" if len(txt) > 120 else ""))
            except Exception:
                pass

            payload = txt[3:]
            serial_txt = mac_txt = region_txt = hdd_hex = None

            if "=" in payload and ("|" in payload or payload.startswith(("SN=","SER="))):
                fields = {}
                for part in payload.split("|"):
                    if "=" in part:
                        k, v = part.split("=", 1)
                        fields[k.strip().upper()] = v.strip()
                serial_txt = fields.get("SN") or fields.get("SER") or ""
                mac_txt    = fields.get("MAC") or ""
                region_txt = fields.get("REG") or ""
                hdd_hex    = fields.get("HDD") or ""
                b64 = fields.get("RAW")
                if b64:
                    self._ee_raw_b64 = b64
                if b64 and (not (serial_txt and mac_txt and region_txt and hdd_hex)):
                    try:
                        raw = base64.b64decode(b64)
                        if len(raw) == 256:
                            serial_txt, mac_txt, region_txt, hdd_hex = self._ee_from_raw(raw)
                    except Exception:
                        pass
            elif payload.startswith("RAW="):
                b64 = payload[4:]
                self._ee_raw_b64 = b64
                try:
                    raw = base64.b64decode(b64)
                    if len(raw) == 256:
                        serial_txt, mac_txt, region_txt, hdd_hex = self._ee_from_raw(raw)
                except Exception:
                    pass
            elif payload.startswith("ERR="):
                self.after(0, self.var_status.set, f"EEPROM error from {addr[0]}: {payload[4:]}")
                continue

            if any([serial_txt, mac_txt, region_txt, hdd_hex]):
                self.after(0, self._render_ee, serial_txt, mac_txt, region_txt, hdd_hex)
                self.after(0, self.var_status.set, f"EEPROM from {addr[0]}")

    # ---- EEPROM raw parsing ----
    def _ee_from_raw(self, buf):
        def digits_only(s):
            return "".join(ch for ch in s if ch.isdigit())

        def fmt_mac(raw6):
            return ":".join("{:02X}".format(b) for b in raw6)

        def try_decode_with(offsets):
            def take(name):
                off, ln = offsets[name]
                return buf[off:off+ln]

            hdd  = take("HDD_KEY")
            mac  = take("MAC")
            regb = take("REGION")[0]
            serb = take("SERIAL")

            serial_txt = digits_only(serb.decode("ascii", "ignore")).strip()
            mac_txt    = fmt_mac(mac)
            region_txt = EE_REGION_MAP.get(regb, f"UNKNOWN({regb:02X})")
            hdd_hex    = binascii.hexlify(hdd).decode().upper()

            ok_serial = len(serial_txt) in (11, 12)
            mac_all_same = all(b == mac[0] for b in mac)
            ok_mac = (len(mac) == 6) and not mac_all_same
            ok_hdd = any(b != 0x00 for b in hdd) and any(b != 0xFF for b in hdd)
            score = (1 if ok_serial else 0) + (1 if ok_mac else 0) + (1 if ok_hdd else 0)
            return score, serial_txt, mac_txt, region_txt, hdd_hex

        maps = [EE_OFFSETS] + EE_FALLBACKS
        best = (-1, "", "", "", ""); best_i = -1
        for i, m in enumerate(maps):
            score, s, mac, reg, hdd = try_decode_with(m)
            if score > best[0]:
                best = (score, s, mac, reg, hdd); best_i = i
            if score == 3: break

        _, serial_txt, mac_txt, region_txt, hdd_hex = best
        try:
            print(f"[EE] used map #{best_i} -> SN={serial_txt} MAC={mac_txt} REG={region_txt} HDD={hdd_hex[:8]}…")
        except Exception:
            pass
        return serial_txt, mac_txt, region_txt, hdd_hex

    # ---- PNG capture (NEW) ----
    def do_save_png(self):
        if not PIL_AVAILABLE:
            messagebox.showerror("Screenshot", "Pillow (PIL) not available. Install with: pip install pillow")
            return
        x = self.winfo_rootx(); y = self.winfo_rooty()
        w = self.winfo_width(); h = self.winfo_height()
        if w <= 1 or h <= 1:
            self.update_idletasks()
            w = self.winfo_width(); h = self.winfo_height()
        path = filedialog.asksaveasfilename(
            defaultextension=".png",
            filetypes=[("PNG Image","*.png")],
            initialfile=time.strftime("typed_viewer_%Y%m%d_%H%M%S.png"),
            title="Save window as PNG"
        )
        if not path: return
        try:
            img = ImageGrab.grab(bbox=(x, y, x+w, y+h))
            img.save(path, "PNG")
            self.var_status.set(f"Saved PNG to {os.path.basename(path)}")
        except Exception as e:
            messagebox.showerror("Screenshot failed", str(e))

    # ---- EEPROM copy (NEW) ----
    def do_copy_eeprom(self):
        serial_txt = (self.var_ee_serial.get() or "—")
        mac_txt    = (self.var_ee_mac.get() or "—")
        region_txt = (self.var_ee_region.get() or "—")
        hdd_txt    = (self._ee_hdd_full or "—")
        text = f"SN={serial_txt}  MAC={mac_txt}  REG={region_txt}  HDD={hdd_txt}"
        try:
            self.clipboard_clear(); self.clipboard_append(text); self.update()
            self.var_status.set("EEPROM copied to clipboard")
        except Exception as e:
            messagebox.showerror("Clipboard", f"Copy failed: {e}")

    def do_copy_eeraw(self):
        if not self._ee_raw_b64:
            messagebox.showinfo("EEPROM RAW", "No RAW (base64) block received yet.")
            return
        try:
            self.clipboard_clear(); self.clipboard_append(self._ee_raw_b64); self.update()
            self.var_status.set("EEPROM RAW (base64) copied")
        except Exception as e:
            messagebox.showerror("Clipboard", f"Copy failed: {e}")

    # ---- Logging (NEW) ----
    def toggle_logging(self):
        if self._log_csv:
            self.stop_logging()
        else:
            self.start_logging()

    def start_logging(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV","*.csv")],
            initialfile=time.strftime("typed_viewer_log_%Y%m%d_%H%M%S.csv"),
            title="Start CSV logging"
        )
        if not path: return
        try:
            self._log_fp = open(path, "w", newline="", encoding="utf-8")
            self._log_csv = csv.writer(self._log_fp)
            self._log_csv.writerow(["timestamp_iso","fan_pct","cpu_c","amb_c","app"])
            self._log_rows = 0
            self._menu_tools.entryconfigure(self._log_menu_index, label="Stop Logging")
            self.var_status.set(f"Logging → {os.path.basename(path)}")
        except Exception as e:
            self._log_fp = None; self._log_csv = None
            messagebox.showerror("Logging", f"Could not start logging: {e}")

    def stop_logging(self):
        try:
            if self._log_fp:
                self._log_fp.flush(); self._log_fp.close()
        finally:
            self._log_fp = None; self._log_csv = None; self._log_rows = 0
            self._menu_tools.entryconfigure(self._log_menu_index, label="Start Logging…")
            self.var_status.set("Logging stopped")

    def on_close(self):
        self._stop = True
        self.stop_logging()
        self.destroy()

if __name__ == "__main__":
    app = App()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()
