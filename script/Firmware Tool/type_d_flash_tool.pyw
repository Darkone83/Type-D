import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import subprocess
import threading
import sys
import os

TYPE_D_LOGO = "icon.png"
DARKONE_LOGO = "DC logo.png"

DEVICE_TYPES = [
    ("Type D", 0x410000, True),
    ("Type D EXP", None, False),
    ("Type D XL", 0x610000, True)
]

FIRMWARE_ADDR = "0x10000"

def ensure_dependencies():
    import importlib.util
    missing = []
    for pkg, mod in [("pyserial", "serial"), ("esptool", "esptool"), ("Pillow", "PIL")]:
        if importlib.util.find_spec(mod) is None:
            missing.append(pkg)
    if missing:
        resp = messagebox.askyesno(
            "Dependencies Missing",
            f"The required package(s) {', '.join(missing)} are missing.\n\nInstall now?"
        )
        if resp:
            try:
                subprocess.check_call([sys.executable, "-m", "pip", "install", *missing])
            except Exception as e:
                messagebox.showerror("Install Failed", f"Failed to install {missing}.\n\n{e}")
                sys.exit(1)
        else:
            sys.exit(1)

ensure_dependencies()
import serial.tools.list_ports
from PIL import Image, ImageTk

class TypeDFlashTool(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Type D Flash Tool")
        try:
            self.iconphoto(False, tk.PhotoImage(file=DARKONE_LOGO))
        except Exception:
            pass

        self.geometry("790x750")
        self.resizable(False, False)
        self.selected_port = tk.StringVar()
        self.device_type = tk.IntVar(value=0)
        self.flash_mode = tk.StringVar(value="new")
        self.erase_flash = tk.BooleanVar(value=False)
        self.merged_file = tk.StringVar()
        self.fw_file = tk.StringVar()
        self.fatfs_file = tk.StringVar()
        self.dump_len_var = tk.StringVar(value="4")
        self.create_widgets()
        self.update_fields()
        self.populate_ports()

    def load_and_scale_logo(self, path, size=(128,128)):
        try:
            img = Image.open(path).convert("RGBA")
            img = img.resize(size, Image.LANCZOS)
            return ImageTk.PhotoImage(img)
        except Exception:
            return None

    def create_widgets(self):
        # --- Branding at top ---
        logo_frame = ttk.Frame(self)
        logo_frame.grid(row=0, column=0, columnspan=4, pady=(18, 8))
        self.type_d_logo_img = self.load_and_scale_logo(TYPE_D_LOGO)
        self.dc_logo_img = self.load_and_scale_logo(DARKONE_LOGO)
        type_d_label = ttk.Label(logo_frame, image=self.type_d_logo_img) if self.type_d_logo_img else ttk.Label(logo_frame, text="TYPE D", font=("Arial", 18, "bold"))
        dc_logo_label = ttk.Label(logo_frame, image=self.dc_logo_img) if self.dc_logo_img else ttk.Label(logo_frame, text="DARKONE CUSTOMS", font=("Arial", 14, "bold"), foreground="#fc8002")
        type_d_label.grid(row=0, column=0, padx=24)
        dc_logo_label.grid(row=0, column=1, padx=24)

        # --- Flash mode + port (top row) ---
        top_row = ttk.Frame(self)
        top_row.grid(row=1, column=0, columnspan=4, padx=32, pady=(0, 8), sticky="ew")
        # Use a 3-column grid so mode is left, spacer in center, port selector right
        top_row.columnconfigure(1, weight=1)  # center column expands to push port to right

        # Flash Mode radio group (left)
        mode_frame = ttk.LabelFrame(top_row, text="Flash Mode")
        mode_frame.grid(row=0, column=0, sticky="w")
        ttk.Radiobutton(mode_frame, text="New Flash (merged.bin only)", variable=self.flash_mode, value="new", command=self.update_fields).grid(row=0, column=0, padx=10, pady=4, sticky="w")
        ttk.Radiobutton(mode_frame, text="Upgrade (upgrade.bin/fatfs.bin)", variable=self.flash_mode, value="upgrade", command=self.update_fields).grid(row=0, column=1, padx=10, pady=4, sticky="w")

        # COM port selector (right)
        port_label = ttk.Label(top_row, text="Select COM Port:")
        port_label.grid(row=0, column=2, sticky="e", padx=(6,0))
        self.port_combo = ttk.Combobox(top_row, textvariable=self.selected_port, width=24, state="readonly")
        self.port_combo.grid(row=0, column=3, sticky="e", padx=(2,0))
        self.port_combo.bind("<Button-1>", lambda e: self.populate_ports())
        self.port_combo.bind("<<ComboboxSelected>>", lambda e: self.on_port_change())



        # --- Main middle: Device type LEFT, file chooser RIGHT ---
        mid_frame = ttk.Frame(self)
        mid_frame.grid(row=2, column=0, columnspan=4, padx=32, pady=(8, 0), sticky="ew")

        # Device type group (left)
        device_frame = ttk.LabelFrame(mid_frame, text="Device Type")
        device_frame.grid(row=0, column=0, rowspan=3, padx=(0, 20), pady=2, sticky="nw")
        for i, (name, _, _) in enumerate(DEVICE_TYPES):
            ttk.Radiobutton(
                device_frame,
                text=name,
                variable=self.device_type,
                value=i,
                command=self.update_fields
            ).grid(row=i, column=0, sticky="w", pady=3, padx=14)

        # File picker group (right)
        self.files_frame = ttk.Frame(mid_frame)
        self.files_frame.grid(row=0, column=1, sticky="ne", padx=(10,0))

        # --- Erase, dump, flash in a row ---
        action_frame = ttk.Frame(self)
        action_frame.grid(row=3, column=0, columnspan=4, padx=32, pady=(10,2), sticky="w")
        self.erase_checkbox = ttk.Checkbutton(action_frame, text="Erase flash before writing", variable=self.erase_flash)
        self.erase_checkbox.grid(row=0, column=0, padx=(0,10))
        ttk.Label(action_frame, text="Length (MB, detected):").grid(row=0, column=1)
        self.dump_len_entry = ttk.Entry(action_frame, textvariable=self.dump_len_var, width=7, state="readonly", foreground="grey")
        self.dump_len_entry.grid(row=0, column=2, padx=(0,4))
        self.dump_btn = ttk.Button(action_frame, text="Dump Firmware", command=self.dump_firmware)
        self.dump_btn.grid(row=0, column=3, padx=(0, 14))
        self.flash_btn = ttk.Button(action_frame, text="Flash Device", command=self.flash_device, width=16)
        self.flash_btn.grid(row=0, column=4)

        # --- Output box label and output box ---
        ttk.Label(self, text="Output:").grid(row=5, column=0, sticky="nw", padx=32, pady=0)
        self.output_text = tk.Text(self, height=15, width=96, state="disabled", font=("Consolas", 9))
        self.output_text.grid(row=6, column=0, columnspan=4, padx=32, pady=(0, 18), sticky="nsew")
        self.grid_rowconfigure(6, weight=1)
        self.grid_columnconfigure(0, weight=1)

    def update_fields(self):
        for widget in self.files_frame.winfo_children():
            widget.destroy()
        pad = {'padx': (0, 3), 'pady': 2}
        if self.flash_mode.get() == "new":
            ttk.Label(self.files_frame, text="merged.bin:").grid(row=0, column=0, sticky="w", **pad)
            ttk.Entry(self.files_frame, textvariable=self.merged_file, width=39).grid(row=0, column=1, sticky="w", **pad)
            ttk.Button(self.files_frame, text="Browse...", command=self.browse_merged, width=11).grid(row=0, column=2, sticky="w", **pad)
        else:
            ttk.Label(self.files_frame, text="upgrade.bin:").grid(row=0, column=0, sticky="w", **pad)
            ttk.Entry(self.files_frame, textvariable=self.fw_file, width=39).grid(row=0, column=1, sticky="w", **pad)
            ttk.Button(self.files_frame, text="Browse...", command=self.browse_fw, width=11).grid(row=0, column=2, sticky="w", **pad)
            needs_fatfs = DEVICE_TYPES[self.device_type.get()][2]
            if needs_fatfs:
                ttk.Label(self.files_frame, text="fatfs.bin:").grid(row=1, column=0, sticky="w", **pad)
                ttk.Entry(self.files_frame, textvariable=self.fatfs_file, width=39).grid(row=1, column=1, sticky="w", **pad)
                ttk.Button(self.files_frame, text="Browse...", command=self.browse_fatfs, width=11).grid(row=1, column=2, sticky="w", **pad)

    def populate_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports
        if ports:
            if self.selected_port.get() not in ports:
                self.port_combo.current(0)
            self.after(200, self.on_port_change)
        else:
            self.port_combo["values"] = ["No COM ports found"]
            self.selected_port.set("")

    def on_port_change(self, *_):
        port = self.selected_port.get()
        if port and "COM" in port:
            threading.Thread(target=self.detect_flash_size, args=(port,), daemon=True).start()

    def detect_flash_size(self, port):
        cmd = [
            "python", "-m", "esptool",
            "--port", port,
            "flash_id"
        ]
        try:
            process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            for line in process.stdout:
                if "Detected flash size:" in line:
                    try:
                        parts = line.strip().split(":")[-1].strip()
                        mb = int(parts.replace("MB", "").strip())
                        self.dump_len_var.set(str(mb))
                        self.append_output(f"[Info] Auto-set dump length to detected flash size: {mb} MB\n")
                    except Exception:
                        pass
            process.wait()
        except Exception as e:
            self.append_output(f"[Warn] Flash size detection failed: {e}\n")

    def browse_merged(self):
        fname = filedialog.askopenfilename(title="Select merged.bin", filetypes=[("BIN files", "*.bin"), ("All files", "*.*")])
        if fname:
            self.merged_file.set(fname)

    def browse_fw(self):
        fname = filedialog.askopenfilename(title="Select upgrade.bin", filetypes=[("BIN files", "*.bin"), ("All files", "*.*")])
        if fname:
            self.fw_file.set(fname)

    def browse_fatfs(self):
        fname = filedialog.askopenfilename(title="Select fatfs.bin", filetypes=[("BIN files", "*.bin"), ("All files", "*.*")])
        if fname:
            self.fatfs_file.set(fname)

    def flash_device(self):
        port = self.selected_port.get()
        if not port or port == "No COM ports found":
            messagebox.showerror("Error", "Please select a valid COM port.")
            return

        device = DEVICE_TYPES[self.device_type.get()]
        needs_fatfs = device[2]
        fatfs_addr = device[1]
        fatfs = self.fatfs_file.get() if needs_fatfs else None

        flash_sequence = []

        if self.erase_flash.get():
            erase_cmd = [
                "python", "-m", "esptool",
                "--port", port,
                "erase_flash"
            ]
            flash_sequence.append(("Erasing flash...", erase_cmd))

        if self.flash_mode.get() == "new":
            merged = self.merged_file.get()
            if not merged or not os.path.isfile(merged):
                messagebox.showerror("Error", "Please select a valid merged.bin file.")
                return
            cmd = [
                "python", "-m", "esptool",
                "--port", port,
                "--baud", "921600",
                "write_flash",
                "0x0", merged
            ]
            flash_sequence.append(("Flashing merged.bin...", cmd))
        else:
            cmds = []
            fw = self.fw_file.get()
            fatfs_present = needs_fatfs and self.fatfs_file.get() and os.path.isfile(self.fatfs_file.get())
            fw_present = fw and os.path.isfile(fw)
            if not fw_present and not fatfs_present:
                messagebox.showerror("Error", "Select at least one file to flash (upgrade.bin and/or fatfs.bin).")
                return
            if fw_present:
                cmds.append((
                    "Flashing upgrade.bin...",
                    [
                        "python", "-m", "esptool",
                        "--port", port,
                        "--baud", "921600",
                        "write_flash",
                        FIRMWARE_ADDR, fw
                    ]
                ))
            if fatfs_present:
                cmds.append((
                    "Flashing fatfs.bin...",
                    [
                        "python", "-m", "esptool",
                        "--port", port,
                        "--baud", "921600",
                        "write_flash",
                        hex(fatfs_addr), self.fatfs_file.get()
                    ]
                ))
            flash_sequence.extend(cmds)

        self.output_text.configure(state="normal")
        self.output_text.delete(1.0, tk.END)
        self.output_text.configure(state="disabled")
        threading.Thread(target=self.run_sequence, args=(flash_sequence,), daemon=True).start()

    def run_sequence(self, flash_sequence):
        for label, cmd in flash_sequence:
            self.append_output(f"{label}\n{' '.join(cmd)}\n\n")
            process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            for line in process.stdout:
                self.append_output(line)
            process.wait()
            if process.returncode != 0:
                self.append_output(f"\nFailed: Exit code {process.returncode}\n")
                return
        self.append_output("\nFlashing complete.\n")

    def dump_firmware(self):
        port = self.selected_port.get()
        if not port or port == "No COM ports found":
            messagebox.showerror("Error", "Please select a valid COM port.")
            return
        try:
            length_mb = int(self.dump_len_var.get())
            if length_mb < 1 or length_mb > 16:
                raise ValueError
        except ValueError:
            messagebox.showerror("Error", "Enter a valid dump length in MB (1-16).")
            return
        length_bytes = length_mb * 1024 * 1024
        fname = filedialog.asksaveasfilename(
            title="Save firmware dump as...",
            defaultextension=".bin",
            filetypes=[("BIN files", "*.bin"), ("All files", "*.*")]
        )
        if not fname:
            return
        dump_cmd = [
            "python", "-m", "esptool",
            "--port", port,
            "read_flash", "0x0", hex(length_bytes), fname
        ]
        self.output_text.configure(state="normal")
        self.output_text.insert(tk.END, f"\nDumping firmware: {' '.join(dump_cmd)}\n\n")
        self.output_text.configure(state="disabled")
        threading.Thread(target=self._run_dump, args=(dump_cmd,), daemon=True).start()

    def _run_dump(self, dump_cmd):
        process = subprocess.Popen(dump_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        for line in process.stdout:
            self.append_output(line)
        process.wait()
        if process.returncode == 0:
            self.append_output("\nFirmware dump completed!\n")
        else:
            self.append_output(f"\nFirmware dump failed with exit code {process.returncode}\n")

    def append_output(self, text):
        self.output_text.configure(state="normal")
        self.output_text.insert(tk.END, text)
        self.output_text.see(tk.END)
        self.output_text.configure(state="disabled")

if __name__ == "__main__":
    app = TypeDFlashTool()
    app.mainloop()
