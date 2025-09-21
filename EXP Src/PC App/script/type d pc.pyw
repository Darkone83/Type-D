#!/usr/bin/env python3

# typed_viewer_qt.py — Type-D PC Viewer (Qt port with true transparency)
# Requires: PySide6  (pip install PySide6)

import os, sys, time, csv, base64, binascii, struct, socket, threading, configparser, subprocess, json
from PySide6.QtCore import Qt, QRect, QPointF, QTimer, Signal, QObject, QSize
from PySide6.QtGui import (
    QPainter, QPolygonF, QColor, QPen, QBrush, QPixmap, QGuiApplication,
    QAction, QIcon, QScreen, QFont
)
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QLabel, QGridLayout, QVBoxLayout, QHBoxLayout, QFrame,
    QFileDialog, QMessageBox, QColorDialog, QDialog, QSlider, QPushButton, QCheckBox, QStatusBar,
    QFontDialog, QMenuBar, QSizePolicy, QSpacerItem, QComboBox
)

APP_NAME    = "Type-D PC Viewer"
APP_VERSION = "v5.0.0-TR"

# ---- Ports / wire formats ----
PORT_MAIN = 50504
FMT_MAIN  = "<iii32s"                 # fan%, cpu°C, ambient°C, char[32] app
SIZE_MAIN = struct.calcsize(FMT_MAIN) # 44

PORT_EXT  = 50505
FMT_EXT   = "<iiiiiii"                # tray, av, pic, xboxver, enc, width, height
SIZE_EXT  = struct.calcsize(FMT_EXT)  # 28

PORT_EE   = 50506                     # EEPROM broadcast

# ---- OLED Emulator (integrated) ----
OLED_UDP_PORT = 35182
OLED_INI_NAME = "lcd_viewer.ini"

# ---- Theme (OG Xbox) ----
DEFAULT_THEME = {
    "bg":        "#0b1d0b",
    "panel":     "#0f220f",
    "edge":      "#1c3a1c",
    "text":      "#baf5ba",
    "accent":    "#66ff33",
    "seg_on":    "#66ff33",
    "seg_off":   "#153315",
    "border_on": "#000000",
    "border_off": "",
    "bevel_hi":  "#a6ff8c",
    "bevel_sh":  "#0c3a0c",
}

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
XBOXVER_LABELS = {0x00:"v1.0",0x01:"v1.1",0x02:"v1.2",0x03:"v1.3",0x04:"v1.4",0x05:"v1.5",0x06:"v1.6"}
ENCODER_LABELS = {0x45:"Conexant", 0x6A:"Focus", 0x70:"Xcalibur"}

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

# ---- Helpers ----
def hex_to_qcolor(h): return QColor(h)

def blend_hex(c1, c2, t):
    a = QColor(c1); b = QColor(c2)
    r = int(a.red()*(1-t) + b.red()*t)
    g = int(a.green()*(1-t) + b.green()*t)
    bl = int(a.blue()*(1-t) + b.blue()*t)
    return QColor(r, g, bl)

def auto_seg_off(panel_hex): return blend_hex(panel_hex, "#000000", 0.35).name()

def decode_avpack(raw_val):
    v = int(raw_val) & 0xFF
    if v in AVPACK_TABLE_PRIMARY: return AVPACK_TABLE_PRIMARY[v], v
    m = v & 0x0E
    if m in AVPACK_TABLE_FALLBACK: return AVPACK_TABLE_FALLBACK[m], v
    return "Unknown", v

def av_is_hd(av_raw):
    v = int(av_raw) & 0xFF
    return (v == 0x01) or (v == 0x02) or ((v & 0x0E) == 0x0A)

def mode_from_resolution(w, h, av_raw):
    w = int(w); h = int(h)
    if w >= 1900 and h == 1080: return "1080i"
    if w == 1280 and h == 720:  return "720p"
    if w in (640,704,720) and h == 480: return "480p" if av_is_hd(av_raw) else "480i"
    if w == 720 and h == 576:  return "576p" if av_is_hd(av_raw) else "576i"
    return None

def sd_system_from_height(h): return "PAL" if int(h) >= 570 else "NTSC"

def decode_encoder(v):
    vv = int(v) & 0xFF
    return ENCODER_LABELS.get(vv, f"Unknown (0x{vv:02X})")

def decode_xboxver(v):
    v = int(v)
    if v < 0 or (v & 0xFF) == 0xFF: return "Not reported"
    return XBOXVER_LABELS.get(v & 0xFF, f"Unknown ({v})")

def _assign_enc_res(a,b,c):
    cand = [a,b,c]; enc=None
    for x in cand:
        if (int(x) & 0xFF) in ENCODER_LABELS:
            enc = x; break
    if enc is None:
        for x in cand:
            if 0 <= int(x) <= 0xFF:
                enc = x; break
    if enc is None: enc = a
    rest = [x for x in cand if x is not enc]
    w,h = (rest+[0,0])[:2]
    return (int(enc) & 0xFF), int(w), int(h)

def _parse_serial(s):
    ds=''.join(ch for ch in (s or '') if ch.isdigit())
    if len(ds)<12: return None
    try: return (int(ds[7]), int(ds[8:10]), int(ds[10:12]))
    except: return None

def _guess_ver_by_serial(s):
    p=_parse_serial(s)
    if not p: return None
    y,ww,ff=p; group=10*y+(ww//10)
    base=None
    if group in (20,21): base="v1.0"
    elif group==23:
        base="v1.0–1.1"
        if ff==3: base="v1.0"
        elif ff in (5,6): base="v1.1"
    elif group in (24,25): base="v1.1"
    elif group==30: base="v1.2"
    elif group in (31,32): base="v1.3"
    elif group==33: base="v1.4–1.5"
    elif group==41: base="v1.6"
    elif group==43: base="v1.6b"
    return base

def _range_from_encoder(enc):
    v=int(enc)&0xFF
    if v==0x45: return "v1.0–1.3"
    if v==0x6A: return "v1.4–1.5"
    if v==0x70: return "v1.6"
    return None

# --------------------- Custom Widgets ---------------------
class SevenSegWidget(QWidget):
    SEG_MAP = {'0':"abcdef",'1':"bc",'2':"abged",'3':"abgcd",'4':"fgbc",'5':"afgcd",
               '6':"afgcde",'7':"abc",'8':"abcdefg",'9':"abcfgd",'-':"g",' ':""}

    def __init__(self, digits=3, seg_width=14, seg_len=52, pad=8, gap=12, theme=DEFAULT_THEME, parent=None):
        super().__init__(parent)
        self.digits=max(1,int(digits)); self.sw=max(1,int(seg_width)); self.sl=max(1,int(seg_len))
        self.pad=max(0,int(pad)); self.gap=max(0,int(gap)); self.text=""; self.theme=dict(theme)
        self.setAttribute(Qt.WA_TranslucentBackground, True); self.setAttribute(Qt.WA_NoSystemBackground, True)
        self.setAutoFillBackground(False); self.setStyleSheet("background: transparent;")
        w=max(1,self._digit_w()*self.digits - self.gap); h=max(1,self._digit_h()); self.setMinimumSize(QSize(w,h))

    def setTheme(self, theme: dict): self.theme.update(theme or {}); self.update()
    def setText(self, s: str): self.text=str(s); self.update()
    def sizeHint(self)->QSize: return QSize(self._digit_w()*self.digits - self.gap, self._digit_h())
    def _digit_w(self): return self.sl + self.sw + self.pad*2 + self.gap
    def _digit_h(self): return self.sl*2 + self.sw + self.pad*4

    def _hseg(self, x, y):
        p, sw, sl = self.pad, self.sw, self.sl
        return QPolygonF([QPointF(x+p,y+p),QPointF(x+p+sl,y+p),QPointF(x+p+sl+sw/2,y+p+sw/2),
                          QPointF(x+p+sl,y+p+sw),QPointF(x+p,y+p+sw),QPointF(x+p-sw/2,y+p+sw/2)])

    def _vseg(self, x, y):
        p, sw, sl = self.pad, self.sw, self.sl
        return QPolygonF([QPointF(x+p,y+p+sw/2),QPointF(x+p+sw,y+p),QPointF(x+p+sw,y+p+sl),
                          QPointF(x+p,y+p+sl+sw/2),QPointF(x+p-sw/2,y+p+sl),QPointF(x+p-sw/2,y+p+sw/2)])

    def paintEvent(self, _):
        p=QPainter(self); p.setRenderHint(QPainter.Antialiasing, True)
        seg_on=QColor(self.theme.get("seg_on","#66ff33")); seg_off=QColor(self.theme.get("seg_off","#153315"))
        b_on_hex=self.theme.get("border_on") or None; b_off_hex=self.theme.get("border_off") or None
        pen_on=QPen(QColor(b_on_hex),2) if b_on_hex else Qt.NoPen
        pen_off=QPen(QColor(b_off_hex),0) if b_off_hex else Qt.NoPen
        digit_w,digit_h=self._digit_w(),self._digit_h()

        s=self.text; chars,dots=[],[False]*self.digits
        for ch in s:
            if ch=='.':
                if chars: dots[len(chars)-1]=True
                continue
            chars.append(ch)
        chars=([' '] * (self.digits - len(chars[-self.digits:]))) + chars[-self.digits:]

        for i,ch in enumerate(chars):
            x0=i*digit_w
            parts={'a':self._hseg(x0,0),'b':self._vseg(x0+self.sl+self.sw/2,0),
                   'c':self._vseg(x0+self.sl+self.sw/2,self.sl),'d':self._hseg(x0,2*self.sl),
                   'e':self._vseg(x0,self.sl),'f':self._vseg(x0,0),'g':self._hseg(x0,self.sl)}
            on=self.SEG_MAP.get(ch,"")
            for name,poly in parts.items():
                if name in on: p.setPen(pen_on); p.setBrush(QBrush(seg_on))
                else:          p.setPen(pen_off); p.setBrush(QBrush(seg_off))
                p.drawPolygon(poly)

            r=max(3,int(self.sw/2)+1); cx=x0+digit_w-self.pad-self.gap-r; cy=digit_h-self.pad-r
            if dots[i]: p.setPen(Qt.NoPen); p.setBrush(QBrush(seg_on)); p.drawEllipse(QPointF(cx,cy),r,r)

class MiniGraphWidget(QWidget):
    def __init__(self, theme=DEFAULT_THEME, parent=None):
        super().__init__(parent)
        self.theme=dict(theme); self.values=[]; self.value_suffix=""
        self.setAttribute(Qt.WA_TranslucentBackground, True); self.setAttribute(Qt.WA_NoSystemBackground, True)
        self.setAutoFillBackground(False); self.setStyleSheet("background: transparent;")
        self.setMinimumSize(QSize(260,110))

    def setTheme(self, theme: dict): self.theme.update(theme or {}); self.update()
    def setData(self, values, suffix=""): self.values=list(values) if values else []; self.value_suffix=suffix or ""; self.update()

    def paintEvent(self, _):
        p=QPainter(self); p.setRenderHint(QPainter.Antialiasing, True)
        edge=hex_to_qcolor(self.theme.get("edge","#1c3a1c")); text=hex_to_qcolor(self.theme.get("text","#baf5ba"))
        w,h,pad=self.width(),self.height(),8
        p.setPen(QPen(edge,2)); p.drawRect(1,1,w-2,h-2)
        for i in range(1,5):
            x=pad+i*((w-2*pad)/5.0); y=pad+i*((h-2*pad)/5.0)
            p.setPen(QPen(edge,1)); p.drawLine(int(x),pad,int(x),h-pad); p.drawLine(pad,int(y),w-pad,int(y))
        if not self.values: return
        vmin,vmax=min(self.values),max(self.values)
        if vmin==vmax: vmin-=1; vmax+=1
        span=vmax-vmin; vmin-=0.05*span; vmax+=0.05*span
        inner_w,inner_h=w-2*pad,h-2*pad
        n=len(self.values)
        if n==1:
            x=pad; y=pad+inner_h*(1-(self.values[0]-vmin)/(vmax-vmin))
            p.setPen(Qt.NoPen); p.setBrush(QBrush(QColor("yellow"))); p.drawEllipse(QPointF(x,y),3,3); return
        step=float(inner_w)/float(max(1,n-1)); p.setPen(QPen(text,2)); last=None
        for i,v in enumerate(self.values):
            x=pad+i*step; y=pad+inner_h*(1-(float(v)-vmin)/(vmax-vmin))
            if last: p.drawLine(last,QPointF(x,y))
            last=QPointF(x,y)
        x_last=pad+(n-1)*step; y_last=pad+inner_h*(1-(self.values[-1]-vmin)/(vmax-vmin))
        x_last=max(pad+3,min(w-pad-3,x_last)); y_last=max(pad+3,min(h-pad-3,y_last))
        p.setPen(Qt.NoPen); p.setBrush(QBrush(QColor("yellow"))); p.drawEllipse(QPointF(x_last,y_last),3,3)
        label=f"{int(round(self.values[-1]))}{self.value_suffix}"
        p.setPen(QPen(text)); p.setFont(QFont("Segoe UI",9,QFont.Bold))
        right_thresh=w-pad-60; lx=x_last+6 if x_last<=right_thresh else x_last-6
        align=Qt.AlignLeft if x_last<=right_thresh else Qt.AlignRight
        p.drawText(QRect(int(lx-120 if align==Qt.AlignRight else lx), int(y_last-10), 120, 20),
                   align|Qt.AlignVCenter, label)

# --------------------- UDP Workers ---------------------
class UdpMainWorker(QObject):
    data = Signal(int,int,int,str,str)
    def __init__(self): super().__init__(); self._stop=False
    def start(self):
        def _run():
            s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            try:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR,1)
                try: s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST,1)
                except OSError: pass
                s.bind(("", PORT_MAIN)); s.settimeout(0.5)
                while not self._stop:
                    try: data,addr=s.recvfrom(4096)
                    except socket.timeout: continue
                    except OSError: break
                    if len(data)!=SIZE_MAIN: continue
                    fan,cpu,amb,app_raw = struct.unpack(FMT_MAIN,data)
                    app = app_raw.split(b"\x00",1)[0].decode("utf-8","ignore")
                    self.data.emit(int(fan),int(cpu),int(amb),app,addr[0])
            finally:
                try: s.close()
                except: pass
        threading.Thread(target=_run, daemon=True).start()
    def stop(self): self._stop=True

class UdpExtWorker(QObject):
    data = Signal(int,int,int,int,int,int,int,str)
    def __init__(self): super().__init__(); self._stop=False
    def start(self):
        def _run():
            s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            try:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR,1)
                try: s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST,1)
                except OSError: pass
                s.bind(("", PORT_EXT)); s.settimeout(0.5)
                while not self._stop:
                    try: data,addr=s.recvfrom(4096)
                    except socket.timeout: continue
                    except OSError: break
                    if len(data)!=SIZE_EXT: continue
                    t,a,p,xb,x5,x6,x7 = struct.unpack(FMT_EXT,data)
                    enc,w,h = _assign_enc_res(x5,x6,x7)
                    self.data.emit(int(t),int(a),int(p),int(xb),int(enc),int(w),int(h),addr[0])
            finally:
                try: s.close()
                except: pass
        threading.Thread(target=_run, daemon=True).start()
    def stop(self): self._stop=True

class UdpEEWorker(QObject):
    data = Signal(str,str,str,str,str)
    raw  = Signal(str)
    status = Signal(str)
    def __init__(self): super().__init__(); self._stop=False
    def start(self):
        def _run():
            s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            try:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR,1)
                try: s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST,1)
                except OSError: pass
                s.bind(("", PORT_EE)); s.settimeout(0.5)
                while not self._stop:
                    try: data,addr=s.recvfrom(65535)
                    except socket.timeout: continue
                    except OSError: break
                    txt=data.decode("ascii","ignore").strip()
                    if not txt.startswith("EE:"): continue
                    payload=txt[3:]
                    serial_txt=mac_txt=region_txt=hdd_hex=None
                    if "=" in payload and ("|" in payload or payload.startswith(("SN=","SER="))):
                        fields={}
                        for part in payload.split("|"):
                            if "=" in part:
                                k,v=part.split("=",1); fields[k.strip().upper()]=v.strip()
                        serial_txt = fields.get("SN") or fields.get("SER") or ""
                        mac_txt    = fields.get("MAC") or ""
                        region_txt = fields.get("REG") or ""
                        hdd_hex    = fields.get("HDD") or ""
                    elif payload.startswith("RAW="):
                        b64 = payload[4:]; self.raw.emit(b64)
                        try:
                            raw = base64.b64decode(b64)
                            if len(raw)==256:
                                s_, m_, r_, h_ = MainWindow.ee_from_raw_static(raw)
                                serial_txt,mac_txt,region_txt,hdd_hex = s_,m_,r_,h_
                        except Exception:
                            pass
                    elif payload.startswith("ERR="):
                        self.status.emit(f"EEPROM error from {addr[0]}: {payload[4:]}")
                        continue
                    if any([serial_txt,mac_txt,region_txt,hdd_hex]):
                        self.data.emit(serial_txt,mac_txt,region_txt,hdd_hex,addr[0])
            finally:
                try: s.close()
                except: pass
        threading.Thread(target=_run, daemon=True).start()
    def stop(self): self._stop=True


# --------------------- OLED Emulator (Integrated Window) ---------------------
class OledUdpReceiver(QObject):
    packet = Signal(dict)
    def __init__(self, port:int=OLED_UDP_PORT):
        super().__init__(); self._port=port; self._stop=False
    def start(self):
        def _run():
            sock=socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                sock.bind(("", self._port))
                while not self._stop:
                    try:
                        sock.settimeout(0.25)
                        data,_addr = sock.recvfrom(4096)
                    except socket.timeout:
                        continue
                    except OSError:
                        break
                    try:
                        msg = json.loads(data.decode("utf-8"))
                    except json.JSONDecodeError:
                        continue
                    if isinstance(msg, dict) and msg.get("type")=="lcd20x4":
                        self.packet.emit(msg)
            except Exception as e:
                self.packet.emit({"type":"error","error":str(e)})
            finally:
                try: sock.close()
                except: pass
        threading.Thread(target=_run, daemon=True).start()
    def stop(self): self._stop=True

class OledEmuWindow(QMainWindow):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Type D OLED Emulator")
        self.setMinimumSize(640, 280)

        # defaults
        self.bg_color = "#000000"
        self.fg_color = "#FFFFFF"
        self.font = QFont("Courier New", 16, QFont.Bold)

        # ini path
        self._ini_path = os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), OLED_INI_NAME)
        self._load_settings()

        # UI
        central = QWidget(self)
        self.setCentralWidget(central)
        root_v = QVBoxLayout(central); root_v.setContentsMargins(16,16,12,12); root_v.setSpacing(6)
        root_v.addStretch(1)

        self.lcd_container = QWidget(central)
        lcd_v = QVBoxLayout(self.lcd_container)
        lcd_v.setContentsMargins(0, 0, 0, 0)
        lcd_v.setSpacing(4)
        lcd_v.setAlignment(Qt.AlignHCenter | Qt.AlignVCenter)

        self.row_labels=[]
        for _ in range(4):
            lbl = QLabel(" " * 20, self.lcd_container)
            lbl.setAlignment(Qt.AlignHCenter | Qt.AlignVCenter)
            lbl.setFont(self.font)
            lbl.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
            lbl.setFrameShape(QFrame.NoFrame); lbl.setLineWidth(0); lbl.setAttribute(Qt.WA_StyledBackground, True)
            self.row_labels.append(lbl)
            lcd_v.addWidget(lbl)

        center_h = QHBoxLayout()
        center_h.addStretch(1)
        center_h.addWidget(self.lcd_container, 0, Qt.AlignCenter)
        center_h.addStretch(1)
        root_v.addLayout(center_h)

        root_v.addStretch(1)

        self.footer = QLabel("Copyright 2025 Darkone83 -=- Team Resurgent", central)
        self.footer.setAlignment(Qt.AlignHCenter)
        root_v.addWidget(self.footer)

        self.apply_colors()
        self._build_menu()

        # UDP
        self.rx = OledUdpReceiver(OLED_UDP_PORT)
        self.rx.packet.connect(self.on_packet)
        self.rx.start()

    def _build_menu(self):
        menubar: QMenuBar = self.menuBar()
        settings_menu = menubar.addMenu("&Settings")

        act_bg = QAction("Set &Background Color…", self); act_bg.triggered.connect(self.choose_background); settings_menu.addAction(act_bg)
        act_fg = QAction("Set &Text Color…", self); act_fg.triggered.connect(self.choose_text); settings_menu.addAction(act_fg)
        act_font = QAction("Set &Font…", self); act_font.triggered.connect(self.choose_font); settings_menu.addAction(act_font)
        settings_menu.addSeparator()
        act_reset = QAction("&Reset to Defaults", self); act_reset.triggered.connect(self.reset_defaults); settings_menu.addAction(act_reset)

    def apply_colors(self):
        self.centralWidget().setStyleSheet(f"background-color:{self.bg_color};")
        self.lcd_container.setStyleSheet(f"background-color:{self.bg_color}; border:none;")
        for lbl in self.row_labels:
            lbl.setStyleSheet(f"color:{self.fg_color}; background-color:{self.bg_color}; border:none;")
            lbl.setFont(self.font)
        self.footer.setStyleSheet(f"color:{self.fg_color}; background-color:{self.bg_color};")

    def choose_background(self):
        col = QColorDialog.getColor(self.bg_color, self, "Choose LCD Background Color")
        if col.isValid():
            self.bg_color = col.name(); self.apply_colors(); self._save_settings()

    def choose_text(self):
        col = QColorDialog.getColor(self.fg_color, self, "Choose LCD Text Color")
        if col.isValid():
            self.fg_color = col.name(); self.apply_colors(); self._save_settings()

    def choose_font(self):
        ok, font = QFontDialog.getFont(self.font, self, "Choose LCD Font")
        if ok:
            self.font = font; self.apply_colors(); self._save_settings()

    def reset_defaults(self):
        self.bg_color="#000000"; self.fg_color="#FFFFFF"; self.font=QFont("Courier New",16,QFont.Bold)
        self.apply_colors(); self._save_settings()

    def _load_settings(self):
        cfg=configparser.ConfigParser()
        if os.path.exists(self._ini_path):
            try:
                cfg.read(self._ini_path)
                if "Display" in cfg:
                    self.bg_color = cfg["Display"].get("bg_color", self.bg_color)
                    self.fg_color = cfg["Display"].get("fg_color", self.fg_color)
                if "Font" in cfg:
                    fam = cfg["Font"].get("family", self.font.family())
                    size = cfg["Font"].getint("size", self.font.pointSize())
                    weight = cfg["Font"].getint("weight", self.font.weight())
                    italic = cfg["Font"].getboolean("italic", self.font.italic())
                    f = QFont(fam, size, weight); f.setItalic(italic); self.font=f
            except Exception:
                pass

    def _save_settings(self):
        cfg=configparser.ConfigParser()
        cfg["Display"]={"bg_color":self.bg_color,"fg_color":self.fg_color}
        cfg["Font"]={"family":self.font.family(),"size":str(self.font.pointSize()),
                     "weight":str(self.font.weight()),"italic":"true" if self.font.italic() else "false"}
        try:
            with open(self._ini_path,"w",encoding="utf-8") as f: cfg.write(f)
        except Exception:
            pass

    def on_packet(self, msg:dict):
        if not isinstance(msg, dict): return
        if msg.get("type")=="error": return
        rows = msg.get("rows", [])
        if not isinstance(rows, list): rows=[]
        for i in range(4):
            if i < len(rows) and isinstance(rows[i], str):
                text = rows[i][:20].ljust(20)
            else:
                text = " " * 20
            self.row_labels[i].setText(text)

    def closeEvent(self, e):
        try:
            if hasattr(self, "rx") and self.rx: self.rx.stop()
            self._save_settings()
        finally:
            super().closeEvent(e)


# --------------------- Main Window ---------------------
class MainWindow(QMainWindow):
    def _label_col_px(self, texts):
        fm = self.fontMetrics()
        return max(fm.horizontalAdvance(t) for t in texts) + 12

    def __init__(self):
        super().__init__()
        self.setWindowTitle(APP_NAME)
        self.theme = dict(DEFAULT_THEME)
        self.transparent_panels = False
        self.bg_pix = None
        self.bg_path = None
        self.bg_opacity = 0.60
        self._ee_raw_b64 = None
        self._ee_hdd_full = None
        self._ver_from_serial = None
        self._last_enc = None
        self._last_smc_code = None

        # Integrated OLED window (replaces external module)
        self._oled_win = None
        self._oled_enabled = False

        # history
        self.hist_window_sec = 600.0  # default 10 min
        self.hist_fan=[]; self.hist_cpuC=[]; self.hist_ambC=[]
        self.fan_min=self.fan_max=self.cpu_min=self.cpu_max=self.amb_min=self.amb_max=None
        self.graph_mode=False
        self.show_F=False

        self._log_fp=None; self._log_csv=None; self._log_rows=0

        # central background
        cw = QWidget(); self.setCentralWidget(cw)
        self.bg_label = QLabel(cw); self.bg_label.setScaledContents(True); self.bg_label.lower()
        root = QGridLayout(cw); root.setContentsMargins(12,12,12,12); root.setSpacing(0)

        title = QLabel("TYPE-D LIVE TELEMETRY"); title.setStyleSheet(self._css_title()); root.addWidget(title,0,0,1,3)

        # panels
        self.panel_fan = self._panelFrame()
        self.panel_cpu = self._panelFrame()
        self.panel_amb = self._panelFrame()
        self.panel_app = self._panelFrame()
        self.panel_ext = self._panelFrame()
        root.addWidget(self.panel_fan,1,0); root.addWidget(self.panel_cpu,1,1); root.addWidget(self.panel_amb,1,2)
        root.addWidget(self.panel_app,2,0,1,3); root.addWidget(self.panel_ext,3,0,1,3)

        # FAN
        v=QVBoxLayout(self.panel_fan); v.setContentsMargins(12,12,12,12)
        lab=QLabel("FAN (%)"); lab.setStyleSheet(self._css_panel_label()); v.addWidget(lab,0,Qt.AlignLeft)
        self.seg_fan = SevenSegWidget(theme=self.theme); v.addWidget(self.seg_fan,1)
        self.graph_fan = MiniGraphWidget(theme=self.theme); v.addWidget(self.graph_fan,1); self.graph_fan.hide()
        self.lbl_fan_minmax = QLabel("min —   max —"); self._style_value(self.lbl_fan_minmax); v.addWidget(self.lbl_fan_minmax,0,Qt.AlignRight)

        # CPU
        v=QVBoxLayout(self.panel_cpu); v.setContentsMargins(12,12,12,12)
        lab=QLabel("CPU TEMP"); lab.setStyleSheet(self._css_panel_label()); v.addWidget(lab,0,Qt.AlignLeft)
        self.seg_cpu = SevenSegWidget(theme=self.theme); v.addWidget(self.seg_cpu,1)
        self.graph_cpu = MiniGraphWidget(theme=self.theme); v.addWidget(self.graph_cpu,1); self.graph_cpu.hide()
        self.lbl_cpu_minmax = QLabel("min —   max —   °C"); self._style_value(self.lbl_cpu_minmax); v.addWidget(self.lbl_cpu_minmax,0,Qt.AlignRight)

        # AMBIENT
        v=QVBoxLayout(self.panel_amb); v.setContentsMargins(12,12,12,12)
        lab=QLabel("AMBIENT"); lab.setStyleSheet(self._css_panel_label()); v.addWidget(lab,0,Qt.AlignLeft)
        self.seg_amb = SevenSegWidget(theme=self.theme); v.addWidget(self.seg_amb,1)
        self.graph_amb = MiniGraphWidget(theme=self.theme); v.addWidget(self.graph_amb,1); self.graph_amb.hide()
        self.lbl_amb_minmax = QLabel("min —   max —   °C"); self._style_value(self.lbl_amb_minmax); v.addWidget(self.lbl_amb_minmax,0,Qt.AlignRight)

        # CURRENT APP
        v=QVBoxLayout(self.panel_app); v.setContentsMargins(12,12,12,12)
        lab=QLabel("CURRENT APP"); lab.setStyleSheet(self._css_panel_label()); v.addWidget(lab,0,Qt.AlignLeft)
        self.lbl_app = QLabel("—"); self._style_value(self.lbl_app); self.lbl_app.setFont(QFont("Consolas", 12))
        v.addWidget(self.lbl_app,0,Qt.AlignLeft)

        # EXT + EEPROM merged
        g=QGridLayout(self.panel_ext); g.setContentsMargins(12,12,12,12); g.setHorizontalSpacing(30)
        l=QLabel("EXTENDED SMBUS STATUS"); l.setStyleSheet(self._css_panel_label()); g.addWidget(l,0,0,1,1,Qt.AlignLeft)
        l=QLabel("EEPROM DATA"); l.setStyleSheet(self._css_panel_label()); g.addWidget(l,0,2,1,1,Qt.AlignLeft)

        left=QGridLayout(); left.setContentsMargins(0,0,0,0); left.setHorizontalSpacing(12)
        left.setColumnMinimumWidth(0, self._label_col_px(["Tray:","AV Pack:","PIC Ver:","Xbox Ver:","Encoder:","Video Res:"]))
        left.setColumnStretch(0,0); left.setColumnStretch(1,1)
        row=0
        def rowL(name, varlbl):
            nonlocal row
            lab=QLabel(name); self._style_value(lab); lab.setAlignment(Qt.AlignRight|Qt.AlignVCenter)
            self._style_value(varlbl); varlbl.setAlignment(Qt.AlignLeft|Qt.AlignVCenter)
            left.addWidget(lab,row,0,Qt.AlignRight|Qt.AlignVCenter)
            left.addWidget(varlbl,row,1,Qt.AlignLeft|Qt.AlignVCenter)
            row+=1

        self.var_tray=QLabel("—"); rowL("Tray:", self.var_tray)
        self.var_av=QLabel("—"); rowL("AV Pack:", self.var_av)
        self.var_pic=QLabel("—"); rowL("PIC Ver:", self.var_pic)
        self.var_xboxver=QLabel("—"); rowL("Xbox Ver:", self.var_xboxver)
        self.var_encoder=QLabel("—"); rowL("Encoder:", self.var_encoder)
        self.var_res=QLabel("—"); rowL("Video Res:", self.var_res)

        right=QGridLayout(); mono="Consolas"
        def rl(name, w, lbl):
            r=right.rowCount()
            lab=QLabel(name); self._style_value(lab); lab.setAlignment(Qt.AlignRight|Qt.AlignVCenter)
            right.addWidget(lab,r,0,Qt.AlignRight|Qt.AlignVCenter)
            self._style_value(lbl); lbl.setAlignment(Qt.AlignLeft|Qt.AlignVCenter); lbl.setMinimumWidth(w*8); lbl.setFont(QFont(mono,10))
            right.addWidget(lbl,r,1,Qt.AlignLeft|Qt.AlignVCenter)

        self.var_ee_serial=QLabel("—"); rl("Serial:",14,self.var_ee_serial)
        self.var_ee_mac   =QLabel("—"); rl("MAC:",18,self.var_ee_mac)
        self.var_ee_region=QLabel("—"); rl("Region:",10,self.var_ee_region)
        self.var_ee_hdd   =QLabel("—"); rl("HDD Key:",39,self.var_ee_hdd)
        self.chk_reveal = QCheckBox("Reveal"); self.chk_reveal.setStyleSheet(self._css_check()); right.addWidget(self.chk_reveal,3,2,Qt.AlignLeft)
        self.chk_reveal.toggled.connect(self._refresh_hdd)

        g.addLayout(left,1,0,1,1)
        spacer = QSpacerItem(40, 1, QSizePolicy.Fixed, QSizePolicy.Minimum)  # non-painting spacer
        g.addItem(spacer, 1, 1)
        g.addLayout(right,1,2,1,1)

        # FOOTER
        foot = QHBoxLayout(); foot.setContentsMargins(0,10,0,0)
        self.chk_F = QCheckBox("Show Fahrenheit (°F)"); self.chk_F.setStyleSheet(self._css_check()); self.chk_F.toggled.connect(self._toggle_units); foot.addWidget(self.chk_F)
        self.chk_graph = QCheckBox("Graph mode (charts)"); self.chk_graph.setStyleSheet(self._css_check()); self.chk_graph.toggled.connect(self._toggle_graph); foot.addWidget(self.chk_graph)

        # Graph time-base dropdown (shown only when graph mode is ON)
        foot.addSpacing(12)
        self.lbl_tb = QLabel("Window:"); self._style_value(self.lbl_tb); foot.addWidget(self.lbl_tb)
        self.combo_timebase = QComboBox(); self.combo_timebase.setSizeAdjustPolicy(QComboBox.AdjustToContents)
        self.combo_timebase.addItems(["1 min","5 min","10 min","15 min"])
        self.combo_timebase.setCurrentText("10 min")
        self.combo_timebase.currentIndexChanged.connect(self._timebase_changed)
        foot.addWidget(self.combo_timebase)
        # hidden by default; appears when graph mode is enabled
        self.lbl_tb.hide(); self.combo_timebase.hide()

        foot.addStretch(1)
        self.status = QStatusBar(); self.status.setStyleSheet(self._css_status()); self.setStatusBar(self.status)
        root.addLayout(foot,4,0,1,3)

        # Menus
        m=self.menuBar()
        file = m.addMenu("File")
        act = QAction("Save PNG…", self); act.triggered.connect(self.save_png); file.addAction(act)
        file.addSeparator()
        act = QAction("About…", self); act.triggered.connect(self.about_box); file.addAction(act)
        file.addSeparator()
        act = QAction("Exit", self); act.triggered.connect(self.close); file.addAction(act)

        tools = m.addMenu("Tools")
        self.act_log = QAction("Start Logging…", self); self.act_log.triggered.connect(self.toggle_logging); tools.addAction(self.act_log)
        act = QAction("Copy EEPROM to Clipboard", self); act.triggered.connect(self.copy_eeprom); tools.addAction(act)
        act = QAction("Copy EEPROM RAW (base64)", self); act.triggered.connect(self.copy_eeraw); tools.addAction(act)
        act = QAction("Save EEPROM BIN…", self); act.triggered.connect(self.save_eeprom_bin); tools.addAction(act)
        tools.addSeparator()
        self.act_oled = QAction("OLED Emulator Window", self, checkable=True)
        self.act_oled.triggered.connect(self._toggle_oled_window)
        tools.addAction(self.act_oled)

        appm = m.addMenu("Appearance")
        act = QAction("Load Background Image…", self); act.triggered.connect(self.load_bg); appm.addAction(act)
        act = QAction("Clear Background", self); act.triggered.connect(self.clear_bg); appm.addAction(act)
        self.act_trans = QAction("Use Transparent Panels with Background", self, checkable=True); self.act_trans.triggered.connect(self.apply_transparency); appm.addAction(self.act_trans)
        act = QAction("Adjust Background Opacity…", self); act.triggered.connect(self.adjust_opacity); appm.addAction(act)
        appm.addSeparator()
        act = QAction("Pick Accent Color…", self); act.triggered.connect(lambda: self.pick_color("accent")); appm.addAction(act)
        act = QAction("Pick Panel Color…", self); act.triggered.connect(lambda: self.pick_color("panel")); appm.addAction(act)
        act = QAction("Pick Window Background Color…", self); act.triggered.connect(lambda: self.pick_color("bg")); appm.addAction(act)
        appm.addSeparator()
        act = QAction("Reset to Default Theme", self); act.triggered.connect(self.reset_theme); appm.addAction(act)
        appm.addSeparator()
        act = QAction("Save Theme to theme.ini", self); act.triggered.connect(self.save_theme_default); appm.addAction(act)
        act = QAction("Load theme.ini", self); act.triggered.connect(self.load_theme_default); appm.addAction(act)
        appm.addSeparator()
        act = QAction("Save Theme As…", self); act.triggered.connect(self.save_theme_as); appm.addAction(act)
        act = QAction("Load Theme From File…", self); act.triggered.connect(self.load_theme_from_file); appm.addAction(act)

        # theme + geometry
        self.apply_theme(self.theme)
        self.resize(960, 640)

        # background follow
        self._geometry_timer = QTimer(self); self._geometry_timer.setInterval(60)
        self._geometry_timer.timeout.connect(self._sync_bg)
        self._geometry_timer.start()

        # auto-load theme.ini
        self.load_theme_default(silent=True)

        # Apply saved OLED window state
        if self._oled_enabled:
            self._open_oled_window()
            self.act_oled.setChecked(True)

        # UDP workers
        self.mainW = UdpMainWorker(); self.mainW.data.connect(self.on_main)
        self.extW  = UdpExtWorker();  self.extW.data.connect(self.on_ext)
        self.eeW   = UdpEEWorker();   self.eeW.data.connect(self.on_ee); self.eeW.raw.connect(self.on_eeraw); self.eeW.status.connect(self.set_status)
        self.mainW.start(); self.extW.start(); self.eeW.start()
        self.set_status(f"Listening on UDP :{PORT_MAIN} / :{PORT_EXT} / :{PORT_EE}")

    # ---------- Graph time-base ----------
    def _timebase_changed(self, _index:int):
        text = self.combo_timebase.currentText()
        mins = 10
        if text.startswith("1"): mins = 1
        elif text.startswith("5"): mins = 5
        elif text.startswith("10"): mins = 10
        elif text.startswith("15"): mins = 15
        self.hist_window_sec = float(mins) * 60.0
        # Trim immediately and refresh
        for hist in (self.hist_fan, self.hist_cpuC, self.hist_ambC):
            self._trim_hist(hist)
        if self.graph_mode:
            self._update_graphs()
        self.set_status(f"Graph window: {mins} minute(s)")

    # ---------- OLED integrated control ----------
    def _open_oled_window(self):
        if self._oled_win and not self._oled_win.isHidden():
            self._oled_win.raise_(); self._oled_win.activateWindow(); return
        self._oled_win = OledEmuWindow(self)
        self._oled_win.destroyed.connect(lambda *_: self._on_oled_closed())
        self._oled_win.show()
        self._oled_enabled = True
        self._save_modules_state()
        self.set_status("OLED Emulator window opened")

    def _close_oled_window(self):
        if not self._oled_win: return
        try:
            self._oled_win.close()
        finally:
            self._oled_win = None
            self._oled_enabled = False
            self._save_modules_state()
            self.set_status("OLED Emulator window closed")

    def _on_oled_closed(self):
        self._oled_win = None
        self._oled_enabled = False
        if self.act_oled: self.act_oled.setChecked(False)
        self._save_modules_state()

    def _toggle_oled_window(self, checked: bool):
        if checked:
            self._open_oled_window()
        else:
            self._close_oled_window()

    def _save_modules_state(self):
        cfg = configparser.ConfigParser()
        if os.path.exists("theme.ini"):
            try: cfg.read("theme.ini", encoding="utf-8")
            except Exception: cfg = configparser.ConfigParser()
        if not cfg.has_section("modules"): cfg.add_section("modules")
        cfg["modules"]["oled_emu_enabled"] = "1" if self._oled_enabled else "0"

        # merge current theme (so we don't lose settings)
        tcfg = self._theme_to_ini()
        for sect in tcfg.sections():
            if sect == "modules": continue
            if not cfg.has_section(sect): cfg.add_section(sect)
            for k, v in tcfg[sect].items():
                cfg[sect][k] = v
        try:
            with open("theme.ini", "w", encoding="utf-8") as fp:
                cfg.write(fp)
        except Exception:
            pass

    # ---------- Styling
    def _css_title(self):
        return f"color:{self.theme['accent']}; background:{self.theme['bg']}; font: bold 13pt 'Segoe UI'; padding-bottom:8px;"
    def _css_panel_label(self):
        return f"color:{self.theme['text']}; background: transparent;"
    def _css_check(self):
        return f"color:{self.theme['text']};"
    def _css_status(self):
        return f"color:{self.theme['text']}; background:{self.theme['bg']};"

    def _panelFrame(self):
        f=QFrame(); f.setFrameShape(QFrame.StyledPanel)
        f.setStyleSheet(f"background:{self.theme['panel']}; border:2px solid {self.theme['edge']};")
        return f

    def _style_value(self, lbl: QLabel):
        lbl.setFrameShape(QFrame.NoFrame); lbl.setLineWidth(0); lbl.setAutoFillBackground(False)
        lbl.setAttribute(Qt.WA_TranslucentBackground, True)
        lbl.setStyleSheet(f"{self._css_panel_label()}border: none; background: transparent; padding: 0px;")

    # ---------- Theme / BG
    def apply_theme(self, theme):
        t=dict(theme)
        if "seg_off" not in t: t["seg_off"]=auto_seg_off(t.get("panel", DEFAULT_THEME["panel"]))
        self.theme.update(t)
        self.setStyleSheet(f"QMainWindow{{background:{self.theme['bg']};}}")
        for fr in (self.panel_fan,self.panel_cpu,self.panel_amb,self.panel_app,self.panel_ext):
            fr.setStyleSheet(f"background:{self._panel_bg_css()}; border:2px solid {self.theme['edge']};")
        for seg in (self.seg_fan,self.seg_cpu,self.seg_amb):
            seg.setTheme({"seg_on": self.theme["seg_on"], "seg_off": self.theme["seg_off"],
                          "border_on": self.theme["border_on"] or "#000000",
                          "border_off": self.theme["border_off"] or ""})
        for g in (self.graph_fan,self.graph_cpu,self.graph_amb): g.setTheme(self.theme)
        self.menuBar().setStyleSheet(f"color:{self.theme['text']}; background:{self.theme['bg']};")
        self.status.setStyleSheet(self._css_status()); self._sync_bg()

    def _panel_bg_css(self):
        if self.transparent_panels and self.bg_pix:
            c = QColor(self.theme["bg"])
            return f"rgba({c.red()},{c.green()},{c.blue()},{int(self.bg_opacity*255)})"
        else:
            return self.theme["panel"]

    def load_bg(self):
        path,_ = QFileDialog.getOpenFileName(self,"Choose background image","","Images (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*.*)")
        if not path: return
        self.bg_path = path; pm = QPixmap(path)
        if pm.isNull(): QMessageBox.critical(self,"Background","Could not load image."); return
        self.bg_pix = pm; self._sync_bg(); self.apply_theme(self.theme)
        self.set_status(f"Background set: {os.path.basename(path)}")

    def clear_bg(self):
        self.bg_pix=None; self.bg_path=None; self.bg_label.clear(); self.apply_theme(self.theme)
        self.set_status("Background cleared")

    def apply_transparency(self, checked):
        self.transparent_panels = bool(checked); self.apply_theme(self.theme)

    def adjust_opacity(self):
        d=QDialog(self); d.setWindowTitle("Adjust Background Opacity")
        v=QVBoxLayout(d); s=QSlider(Qt.Horizontal); s.setRange(0,100); s.setValue(int(self.bg_opacity*100)); v.addWidget(s)
        ok=QPushButton("Close"); v.addWidget(ok); ok.clicked.connect(d.accept)
        def onchg(val):
            self.bg_opacity=max(0.0,min(1.0,val/100.0)); self.apply_theme(self.theme); self.set_status(f"Background opacity: {val}%")
        s.valueChanged.connect(onchg); d.exec()

    def pick_color(self, key):
        col = QColorDialog.getColor(QColor(self.theme[key]), self, "Pick Color")
        if not col.isValid(): return
        t=dict(self.theme)
        if key=="accent": t["accent"]=col.name(); t["seg_on"]=col.name()
        elif key=="panel": t["panel"]=col.name(); t["seg_off"]=auto_seg_off(t["panel"])
        elif key=="bg":    t["bg"]=col.name()
        self.apply_theme(t); self.set_status("Theme updated")

    def reset_theme(self):
        self.transparent_panels=False; self.act_trans.setChecked(False)
        self.bg_opacity=0.60; self.apply_theme(dict(DEFAULT_THEME)); self.clear_bg()
        self.set_status("Theme reset to defaults")

    def _theme_to_ini(self):
        cfg=configparser.ConfigParser()
        cfg["theme"]={
            "bg": self.theme["bg"], "panel": self.theme["panel"], "edge": self.theme["edge"],
            "text": self.theme["text"], "accent": self.theme["accent"], "seg_on": self.theme["seg_on"],
            "seg_off": self.theme["seg_off"], "border_on": self.theme["border_on"],
            "border_off": self.theme["border_off"], "bevel_hi": self.theme["bevel_hi"],
            "bevel_sh": self.theme["bevel_sh"], "background_path": self.bg_path or "",
            "transparent_panels": "1" if self.transparent_panels else "0",
            "bg_opacity": f"{self.bg_opacity:.3f}",
        }
        cfg["modules"] = {"oled_emu_enabled": "1" if self._oled_enabled else "0"}
        return cfg

    def _apply_ini(self, cfg):
        if cfg.has_section("theme"):
            t=cfg["theme"]; new=dict(self.theme)
            for k in ["bg","panel","edge","text","accent","seg_on","seg_off","border_on","border_off","bevel_hi","bevel_sh"]:
                if k in t: new[k]=t.get(k,new.get(k))
            self.apply_theme(new)
            p=t.get("background_path","").strip()
            if p and os.path.exists(p): self.bg_path=p; self.bg_pix=QPixmap(p)
            else: self.bg_pix=None; self.bg_path=None
            self.transparent_panels = t.get("transparent_panels","0") in ("1","true","yes","on")
            self.act_trans.setChecked(self.transparent_panels)
            try: self.bg_opacity=max(0.0,min(1.0,float(t.get("bg_opacity","0.60"))))
            except: self.bg_opacity=0.60
            self.apply_theme(self.theme)

        self._oled_enabled = cfg.has_section("modules") and cfg["modules"].get("oled_emu_enabled","0") in ("1","true","yes","on")
        return True

    def save_theme_default(self):
        try:
            cfg=self._theme_to_ini()
            with open("theme.ini","w",encoding="utf-8") as fp: cfg.write(fp)
            self.set_status("Saved theme.ini")
        except Exception as e:
            QMessageBox.critical(self,"Save Theme", str(e))

    def load_theme_default(self, silent=False):
        if not os.path.exists("theme.ini"):
            if not silent: QMessageBox.information(self,"Load Theme","theme.ini not found.")
            return False
        try:
            cfg=configparser.ConfigParser(); cfg.read("theme.ini",encoding="utf-8")
            ok=self._apply_ini(cfg)
            if ok and not silent: self.set_status("Loaded theme.ini")
            return ok
        except Exception as e:
            if not silent: QMessageBox.critical(self,"Load Theme", str(e))
            return False

    def save_theme_as(self):
        path,_ = QFileDialog.getSaveFileName(self,"Save Theme As","theme.ini","INI (*.ini);;All Files (*.*)")
        if not path: return
        try:
            cfg=self._theme_to_ini()
            with open(path,"w",encoding="utf-8") as fp: cfg.write(fp)
            self.set_status(f"Saved theme → {os.path.basename(path)}")
        except Exception as e:
            QMessageBox.critical(self,"Save Theme", str(e))

    def load_theme_from_file(self):
        path,_ = QFileDialog.getOpenFileName(self,"Load Theme File","","INI (*.ini);;All Files (*.*)")
        if not path: return
        try:
            cfg=configparser.ConfigParser(); cfg.read(path,encoding="utf-8")
            if self._apply_ini(cfg): self.set_status(f"Loaded theme ← {os.path.basename(path)}")
        except Exception as e:
            QMessageBox.critical(self,"Load Theme", str(e))

    def _sync_bg(self):
        if not self.bg_pix: self.bg_label.clear(); return
        r = self.centralWidget().rect(); self.bg_label.setGeometry(r)
        pm = self.bg_pix.scaled(r.size(), Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation)
        if self.transparent_panels:
            tinted = QPixmap(pm.size()); tinted.fill(Qt.transparent)
            p=QPainter(tinted); p.drawPixmap(0,0,pm)
            c=QColor(self.theme["bg"]); c.setAlpha(int(self.bg_opacity*255))
            p.fillRect(tinted.rect(), c); p.end()
            self.bg_label.setPixmap(tinted)
        else:
            self.bg_label.setPixmap(pm)

    # ---------- Utilities
    def set_status(self, msg): self.status.showMessage(msg)

    def save_png(self):
        path,_ = QFileDialog.getSaveFileName(self,"Save window as PNG", time.strftime("typed_viewer_%Y%m%d_%H%M%S.png"), "PNG Image (*.png)")
        if not path: return
        try:
            screen: QScreen = QGuiApplication.primaryScreen()
            pix = screen.grabWindow(self.winId()); pix.save(path, "PNG")
            self.set_status(f"Saved PNG to {os.path.basename(path)}")
        except Exception as e:
            QMessageBox.critical(self,"Screenshot failed", str(e))

    def about_box(self):
        QMessageBox.information(self,"About", f"{APP_NAME}\nVersion: {APP_VERSION}\n\nWritten By: Darkone83")

    def copy_eeprom(self):
        serial_txt = (self.var_ee_serial.text() or "—")
        mac_txt    = (self.var_ee_mac.text() or "—")
        region_txt = (self.var_ee_region.text() or "—")
        hdd_txt    = (self._ee_hdd_full or "—")
        text = f"SN={serial_txt}  MAC={mac_txt}  REG={region_txt}  HDD={hdd_txt}"
        QGuiApplication.clipboard().setText(text); self.set_status("EEPROM copied to clipboard")

    def copy_eeraw(self):
        if not self._ee_raw_b64:
            QMessageBox.information(self, "EEPROM RAW", "No RAW (base64) block received yet."); return
        QGuiApplication.clipboard().setText(self._ee_raw_b64); self.set_status("EEPROM RAW (base64) copied")

    def save_eeprom_bin(self):
        """Save the latest RAW EEPROM (base64) as a .bin with serial in filename."""
        if not self._ee_raw_b64:
            QMessageBox.information(self, "Save EEPROM", "No RAW (base64) EEPROM received yet."); return
        try:
            raw = base64.b64decode(self._ee_raw_b64, validate=True)
        except Exception as e:
            QMessageBox.critical(self, "Save EEPROM", f"Base64 decode failed:\n{e}")
            return
        if len(raw) != 256:
            QMessageBox.critical(self, "Save EEPROM", f"Unexpected EEPROM length: {len(raw)} (expected 256)")
            return
        ser_digits = "".join(ch for ch in (self.var_ee_serial.text() or "") if ch.isdigit())
        default_name = f"eeprom_{ser_digits}.bin" if ser_digits else "eeprom.bin"
        path,_ = QFileDialog.getSaveFileName(self, "Save EEPROM BIN", default_name, "Binary (*.bin);;All Files (*.*)")
        if not path: return
        try:
            with open(path, "wb") as fp:
                fp.write(raw)
            self.set_status(f"Saved EEPROM → {os.path.basename(path)}")
        except Exception as e:
            QMessageBox.critical(self, "Save EEPROM", str(e))

    # ---------- Units / Graphs
    @staticmethod
    def c_to_f(c): return int(round((c*9/5)+32))
    def _fmt3(self,n):
        try: n=int(n)
        except: return "—"
        if n<-99: return "-99"
        if n>999: return "999"
        return f"{n:d}"
    def _minmax_text(self,mn,mx,unit=""):
        if mn is None or mx is None: return f"min —   max —   {unit}".rstrip()
        return f"min {int(mn)}   max {int(mx)}   {unit}".rstrip()

    def _toggle_units(self, checked):
        self.show_F = bool(checked)
        self._update_minmax_labels()
        if self.graph_mode: self._update_graphs()
    def _toggle_graph(self, checked):
        self.graph_mode = bool(checked)
        # show/hide time-base controls
        self.lbl_tb.setVisible(self.graph_mode)
        self.combo_timebase.setVisible(self.graph_mode)
        if self.graph_mode:
            self.seg_fan.hide(); self.graph_fan.show()
            self.seg_cpu.hide(); self.graph_cpu.show()
            self.seg_amb.hide(); self.graph_amb.show()
            self._update_graphs()
        else:
            self.graph_fan.hide(); self.seg_fan.show()
            self.graph_cpu.hide(); self.seg_cpu.show()
            self.graph_amb.hide(); self.seg_amb.show()
            self._update_minmax_labels()

    def _update_minmax_labels(self):
        if self.show_F:
            self.lbl_cpu_minmax.setText(self._minmax_text(self._to_f(self.cpu_min), self._to_f(self.cpu_max), "°F"))
            self.lbl_amb_minmax.setText(self._minmax_text(self._to_f(self.amb_min), self._to_f(self.amb_max), "°F"))
        else:
            self.lbl_cpu_minmax.setText(self._minmax_text(self.cpu_min, self.cpu_max, "°C"))
            self.lbl_amb_minmax.setText(self._minmax_text(self.amb_min, self.amb_max, "°C"))
    def _to_f(self,v): return None if v is None else self.c_to_f(v)

    def _trim_hist(self, hist):
        cutoff=time.time()-self.hist_window_sec
        while hist and hist[0][0] < cutoff: hist.pop(0)
    def _recent_values(self, hist):
        cutoff=time.time()-self.hist_window_sec
        return [v for (t,v) in hist if t>=cutoff]
    def _update_graphs(self):
        fan_vals   = self._recent_values(self.hist_fan)
        cpu_vals_c = self._recent_values(self.hist_cpuC)
        amb_vals_c = self._recent_values(self.hist_ambC)
        if self.show_F:
            cpu_vals=[self.c_to_f(v) for v in cpu_vals_c]
            amb_vals=[self.c_to_f(v) for v in amb_vals_c]
            unit="°F"
        else:
            cpu_vals=cpu_vals_c; amb_vals=amb_vals_c; unit="°C"
        self.graph_fan.setData(fan_vals, "%"); self.graph_cpu.setData(cpu_vals, unit); self.graph_amb.setData(amb_vals, unit)
        if fan_vals:
            cur=int(fan_vals[-1]); mn=int(min(fan_vals)); mx=int(max(fan_vals))
            self.lbl_fan_minmax.setText(f"cur {cur}   min {mn}   max {mx}")
        if cpu_vals:
            cur=int(cpu_vals[-1]); mn=int(min(cpu_vals)); mx=int(max(cpu_vals))
            self.lbl_cpu_minmax.setText(f"cur {cur}   min {mn}   max {mx}   {unit}")
        if amb_vals:
            cur=int(amb_vals[-1]); mn=int(min(amb_vals)); mx=int(max(amb_vals))
            self.lbl_amb_minmax.setText(f"cur {cur}   min {mn}   max {mx}   {unit}")

    # ---------- HDD key masking ----------
    def _refresh_hdd(self):
        if not self._ee_hdd_full: return
        if self.chk_reveal.isChecked():
            self.var_ee_hdd.setText(self._ee_hdd_full)
        else:
            s=self._ee_hdd_full
            self.var_ee_hdd.setText(s[:4] + "…" * 6 + s[-4:] if len(s)>=8 else "…")

    # ---------- EEPROM raw parsing ----------
    @staticmethod
    def ee_from_raw_static(buf):
        def digits_only(s): return "".join(ch for ch in s if ch.isdigit())
        def fmt_mac(raw6): return ":".join("{:02X}".format(b) for b in raw6)
        def try_decode_with(offsets):
            def take(name):
                off, ln = offsets[name]; return buf[off:off+ln]
            hdd=take("HDD_KEY"); mac=take("MAC"); regb=take("REGION")[0]; serb=take("SERIAL")
            serial_txt = digits_only(serb.decode("ascii","ignore")).strip()
            mac_txt = fmt_mac(mac); region_txt=EE_REGION_MAP.get(regb, f"UNKNOWN({regb:02X})")
            hdd_hex=binascii.hexlify(hdd).decode().upper()
            ok_serial = len(serial_txt) in (11,12)
            mac_all_same = all(b==mac[0] for b in mac)
            ok_mac = (len(mac)==6) and not mac_all_same
            ok_hdd = any(b!=0x00 for b in hdd) and any(b!=0xFF for b in hdd)
            score = (1 if ok_serial else 0) + (1 if ok_mac else 0) + (1 if ok_hdd else 0)
            return score,serial_txt,mac_txt,region_txt,hdd_hex
        maps=[EE_OFFSETS]+EE_FALLBACKS; best=(-1,"","","","")
        for m in maps:
            score,s,mac,reg,hdd=try_decode_with(m)
            if score>best[0]: best=(score,s,mac,reg,hdd)
            if score==3: break
        _,serial_txt,mac_txt,region_txt,hdd_hex = best
        return serial_txt,mac_txt,region_txt,hdd_hex

    # ---------- UDP handlers ----------
    def on_main(self, fan, cpu_c, amb_c, app, peer):
        fan=max(0,min(100,int(fan))); cpu_c=max(-100,min(200,int(cpu_c))); amb_c=max(-100,min(200,int(amb_c)))
        self.fan_min = fan if self.fan_min is None else min(self.fan_min, fan)
        self.fan_max = fan if self.fan_max is None else max(self.fan_max, fan)
        self.cpu_min = cpu_c if self.cpu_min is None else min(self.cpu_min, cpu_c)
        self.cpu_max = cpu_c if self.cpu_max is None else max(self.cpu_max, cpu_c)
        self.amb_min = amb_c if self.amb_min is None else min(self.amb_min, amb_c)
        self.amb_max = amb_c if self.amb_max is None else max(self.amb_max, amb_c)

        cpu_show=self.c_to_f(cpu_c) if self.show_F else cpu_c
        amb_show=self.c_to_f(amb_c) if self.show_F else amb_c

        self.lbl_fan_minmax.setText(self._minmax_text(self.fan_min, self.fan_max))
        self.lbl_cpu_minmax.setText(self._minmax_text(self._to_f(self.cpu_min) if self.show_F else self.cpu_min,
                                                      self._to_f(self.cpu_max) if self.show_F else self.cpu_max,
                                                      "°F" if self.show_F else "°C"))
        self.lbl_amb_minmax.setText(self._minmax_text(self._to_f(self.amb_min) if self.show_F else self.amb_min,
                                                      self._to_f(self.amb_max) if self.show_F else self.amb_max,
                                                      "°F" if self.show_F else "°C"))

        self.seg_fan.setText(self._fmt3(fan))
        self.seg_cpu.setText(self._fmt3(cpu_show))
        self.seg_amb.setText(self._fmt3(amb_show))
        self.lbl_app.setText(app if app else "—")

        now=time.time()
        self.hist_fan.append((now,fan)); self._trim_hist(self.hist_fan)
        self.hist_cpuC.append((now,cpu_c)); self._trim_hist(self.hist_cpuC)
        self.hist_ambC.append((now,amb_c)); self._trim_hist(self.hist_ambC)

        if self._log_csv:
            ts=time.strftime("%Y-%m-%dT%H:%M:%S")
            try:
                self._log_csv.writerow([ts, fan, cpu_c, amb_c, (app or "").strip()])
                self._log_rows+=1
                if self._log_rows%10==0: self._log_fp.flush()
            except Exception as e:
                self.set_status(f"Log write failed: {e}"); self.stop_logging()

        if self.graph_mode: self._update_graphs()
        self.set_status(f"OK from {peer} — main/ext/ee active")

    def _refresh_version_label(self):
        if self._ver_from_serial:
            label=f"{self._ver_from_serial} (serial)"
            if self._last_enc is not None:
                enc_range=_range_from_encoder(self._last_enc)
                if enc_range=="v1.6" and not self._ver_from_serial.startswith("v1.6"):
                    label=f"{enc_range} (encoder)"
            self.var_xboxver.setText(label); return
        if self._last_enc is not None:
            enc_range=_range_from_encoder(self._last_enc)
            if enc_range:
                self.var_xboxver.setText(f"{enc_range} (encoder)"); return
        if self._last_smc_code is not None:
            self.var_xboxver.setText(f"{decode_xboxver(self._last_smc_code)} (SMC)"); return
        self.var_xboxver.setText("Not reported")

    def on_ext(self, tray_state, av_state, pic_ver, xbox_ver, enc, v_w, v_h, peer):
        tray_txt = TRAY_LABELS.get(int(tray_state), f"Unknown ({tray_state})")
        av_label, raw = decode_avpack(av_state)
        self.var_tray.setText(tray_txt)
        self.var_av.setText(f"{av_label} (0x{raw:02X})")
        self.var_pic.setText(str(int(pic_ver)))
        self._last_smc_code = int(xbox_ver)

        try:
            self.var_encoder.setText(decode_encoder(enc))
        except Exception:
            self.var_encoder.setText("Unknown (err)")
        self._last_enc = int(enc) & 0xFF
        self._refresh_version_label()

        vw,vh = int(v_w), int(v_h)
        if 160 <= vw <= 4096 and 120 <= vh <= 2160:
            mode = mode_from_resolution(vw,vh,av_state)
            if mode:
                sys_tag = f" {sd_system_from_height(vh)}" if mode.startswith(("480","576")) else ""
                self.var_res.setText(f"{vw} x {vh} ({mode}{sys_tag})")
            else:
                self.var_res.setText(f"{vw} x {vh}")
        else:
            self.var_res.setText("—")

    def on_eeraw(self, b64): self._ee_raw_b64=b64

    def on_ee(self, serial_txt, mac_txt, region_txt, hdd_hex, peer):
        self.var_ee_serial.setText((serial_txt or "").strip() or "—")
        self.var_ee_mac.setText((mac_txt or "").strip() or "—")
        self.var_ee_region.setText((region_txt or "").strip() or "—")
        self._ee_hdd_full = (hdd_hex or "").strip(); self._refresh_hdd()
        self._ver_from_serial = _guess_ver_by_serial((serial_txt or "").strip())
        self._refresh_version_label()
        self.set_status(f"EEPROM from {peer}")

    # ---------- Cleanup
    def closeEvent(self, e):
        try:
            self.mainW.stop(); self.extW.stop(); self.eeW.stop()
            if self._oled_win:
                self._oled_win.close()
        finally:
            self.stop_logging()
        super().closeEvent(e)

    # ---------- Logging
    def toggle_logging(self):
        if self._log_csv: self.stop_logging()
        else: self.start_logging()

    def start_logging(self):
        path,_ = QFileDialog.getSaveFileName(self,"Start CSV logging", time.strftime("typed_viewer_log_%Y%m%d_%H%M%S.csv"), "CSV (*.csv)")
        if not path: return
        try:
            self._log_fp=open(path,"w",newline="",encoding="utf-8")
            self._log_csv=csv.writer(self._log_fp); self._log_csv.writerow(["timestamp_iso","fan_pct","cpu_c","amb_c","app"])
            self._log_rows=0; self.act_log.setText("Stop Logging"); self.set_status(f"Logging → {os.path.basename(path)}")
        except Exception as e:
            self._log_fp=None; self._log_csv=None
            QMessageBox.critical(self,"Logging", f"Could not start logging: {e}")

    def stop_logging(self):
        try:
            if self._log_fp: self._log_fp.flush(); self._log_fp.close()
        finally:
            self._log_fp=None; self._log_csv=None; self._log_rows=0
            self.act_log.setText("Start Logging…"); self.set_status("Logging stopped")

def main():
    app = QApplication(sys.argv)
    app.setStyleSheet("QLabel { border: none; background: transparent; }")
    w = MainWindow()
    ico_path=os.path.join(os.path.abspath("."), "dc.ico")
    if os.path.exists(ico_path): w.setWindowIcon(QIcon(ico_path))
    w.show()
    sys.exit(app.exec())

if __name__=="__main__":
    main()
