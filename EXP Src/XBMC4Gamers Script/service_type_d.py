# Q:\system\scripts\service_type_d.py
# Background broadcaster for Type-D: sends the currently highlighted title
# (and TitleID if present) while you browse XBMC4Gamers/XBMC4Xbox.
#
# Optional unicast target:
#   Create Q:\system\scripts\type_d.ini with:  host=192.168.1.42
# Otherwise we broadcast to 255.255.255.255:50506

import socket, os

PORT = 50506
INI_PATH = r"q:\system\scripts\type_d.ini"

# Probe these containers (common in XBMC4Gamers):
CAND_CONTAINER_IDS = [50, 51, 500, 501, 700, 701, 25, 950, 951]

# NEW: label controls to try via Control.GetLabel(<id>)
CONTROL_LABEL_IDS = [
    2, 3, 4, 5,
    20, 21, 22, 23, 25,
    30, 31,
    50, 51, 52, 60,
    100, 101,
    555, 560,
    700, 701,
    950, 951
]

# Keep diagnostics off by default
DEBUG_DIAGNOSTICS = False
DIAG_ROUNDS = 10

try:
    import xbmc
except:
    xbmc = None  # runs only inside XBMC

def log(msg):
    try:
        xbmc.log('[Type-D service] ' + msg, 2)  # 2 = LOGNOTICE
    except:
        pass

def load_ini_host():
    try:
        if not os.path.exists(INI_PATH):
            return None
        f = open(INI_PATH, 'r')
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if line.lower().startswith('host='):
                return line.split('=',1)[1].strip()
    except Exception as e:
        log('load_ini_host error: %s' % e)
    finally:
        try: f.close()
        except: pass
    return None

def send_udp(payload, host=None):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        if host:
            addr = (host, PORT)
        else:
            try:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            except:
                pass
            addr = ('255.255.255.255', PORT)
        s.sendto(payload, addr)
        return True
    except Exception as e:
        log('send_udp failed: %s' % e)
        return False
    finally:
        try: s.close()
        except: pass

def info_label(lbl):
    # guard every call so we never crash the loop during launches
    try:
        v = xbmc.getInfoLabel(lbl)
        return v or ''
    except:
        return ''

def cond(vis):
    try:
        return xbmc.getCondVisibility(vis)
    except:
        return False

def control_label(cid):
    # safe wrapper for Control.GetLabel()
    try:
        v = xbmc.getInfoLabel("Control.GetLabel(%d)" % int(cid))
        return v or ''
    except:
        return ''

def read_from_container(cid):
    # Try common per-container labels/properties used by XBMC4Gamers
    title = info_label("Container(%d).ListItem.Title" % cid)
    if not title:
        title = info_label("Container(%d).ListItem.Label" % cid)

    tid = info_label("Container(%d).ListItem.Property(TitleID)" % cid)
    if not tid:
        tid = info_label("Container(%d).ListItem.Property(GameID)" % cid)
    if not tid:
        tid = info_label("Container(%d).ListItem.Property(TID)" % cid)

    # XBE path if the skin exposes it (optional)
    xbe = info_label("Container(%d).ListItem.Property(XBEPath)" % cid)
    if not xbe:
        xbe = info_label("Container(%d).ListItem.Property(XbePath)" % cid)
    if not xbe:
        xbe = info_label("Container(%d).ListItem.FolderPath" % cid)

    # If still no title and this container has focus, try nearby control labels
    if not title and cond("Control.HasFocus(%d)" % cid):
        for nid in (cid, cid+1, cid-1, cid+2, cid-2):
            lab = control_label(nid)
            if lab:
                title = lab
                break

    return title, tid, xbe

def read_fallback_global():
    # Global fallback if no container-specific value is present
    title = info_label("ListItem.Title")
    if not title:
        title = info_label("ListItem.Label")

    tid = info_label("ListItem.Property(TitleID)")
    if not tid:
        tid = info_label("ListItem.Property(GameID)")
    if not tid:
        tid = info_label("ListItem.Property(TID)")

    xbe = info_label("ListItem.Property(XBEPath)")
    if not xbe:
        xbe = info_label("ListItem.Property(XbePath)")
    if not xbe:
        xbe = info_label("ListItem.FolderPath")

    return title, tid, xbe

def read_from_controls():
    # 1) label of any focused control we know about
    for cid in CONTROL_LABEL_IDS:
        if cond("Control.HasFocus(%d)" % cid):
            lab = control_label(cid)
            if lab:
                return lab
    # 2) first non-empty label among control IDs
    for cid in CONTROL_LABEL_IDS:
        lab = control_label(cid)
        if lab:
            return lab
    return ''

def current_title_and_tid():
    focused_title = ''
    focused_tid = ''
    focused_xbe = ''
    focused_found = False

    # 1) Prefer focused containers
    for cid in CAND_CONTAINER_IDS:
        has_focus = cond("Control.HasFocus(%d)" % cid)
        t, tid, xbe = read_from_container(cid)

        if DEBUG_DIAGNOSTICS:
            log("diag cid=%d focus=%s title='%s' tid='%s' xbe='%s'" %
                (cid, has_focus and '1' or '0', t, tid, xbe))

        if has_focus and not (t or tid):
            # fallback to a focused label control (same view)
            t = read_from_controls()

        if has_focus and (t or tid):
            focused_title, focused_tid, focused_xbe = t, tid, xbe
            focused_found = True
            break

    # 2) Any container with something
    if not focused_found:
        for cid in CAND_CONTAINER_IDS:
            t, tid, xbe = read_from_container(cid)
            if not (t or tid):
                t = read_from_controls()
            if t or tid:
                focused_title, focused_tid, focused_xbe = t, tid, xbe
                focused_found = True
                break

    # 3) Global fallback then control labels
    if not focused_found:
        focused_title, focused_tid, focused_xbe = read_fallback_global()
        if not (focused_title or focused_tid):
            focused_title = read_from_controls()

    # 4) Dashboard fallback name
    if not focused_title:
        dash = info_label("System.BuildVersion")
        focused_title = "XBMC4Gamers" if dash else "XBMC"

    return focused_title, focused_tid, focused_xbe

def broadcast(title, tid, host=None):
    msg = 'APP:%s' % title
    if tid:
        msg += '|TID:%s' % tid
    try:
        payload = msg.encode('ascii', 'ignore')
    except:
        payload = 'APP:XBMC4Gamers'.encode('ascii', 'ignore')
    ok = send_udp(payload, host)
    if ok:
        log('sent: %s %s' % (msg, '(unicast)' if host else '(broadcast)'))
    return ok

def _has_abort():
    # XBMC4Xbox exposes abortRequested in services; guard it anyway
    try:
        return getattr(xbmc, 'abortRequested', False)
    except:
        return False

def run():
    host = load_ini_host()
    last = None
    diag_left = DIAG_ROUNDS if DEBUG_DIAGNOSTICS else 0

    # Initial heartbeat (guarded)
    try:
        t, tid, _ = current_title_and_tid()
        broadcast(t, tid, host)
    except Exception as e:
        log('startup read error: %s' % e)

    while True:
        if _has_abort():
            break
        try:
            t, tid, _ = current_title_and_tid()
            key = (t or '').strip() + '|' + (tid or '')
            if key and key != last:
                broadcast(t, tid, host)
                last = key
            if diag_left > 0:
                diag_left -= 1
        except Exception as e:
            # Never die during a game/app launch; just log + slow down briefly
            log('loop error: %s' % e)
        try:
            xbmc.sleep(800)  # ~1 update/sec while browsing
        except:
            # If sleep fails (shutdown), exit cleanly
            break

if __name__ == '__main__':
    try:
        if xbmc:
            run()
    except Exception as e:
        log('fatal: %s' % e)
