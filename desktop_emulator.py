import sys
import os
import subprocess
import time
import socket
import struct
import tkinter as tk
from tkinter import ttk
from PIL import Image, ImageTk

BACKEND_PORT = 50555
PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))

class BulletOSEmulatorApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Bullet OS v0.2.1 - ESP32 Hardware Emulator")
        self.root.geometry("580x780")
        self.root.configure(bg="#080c14")
        self.root.resizable(False, False)

        # State
        self.backend_proc = None
        self.sock = None
        self.knob_angle = 0
        self.current_mode = 1 # 240x240
        self.photo_img = None

        # Start backend
        self.start_backend()

        # Build UI
        self.create_widgets()

        # Connect to backend
        self.connect_backend()

        # Keyboard bindings
        self.bind_events()

        # Start render loop
        self.root.after(20, self.update_frame)

    def start_backend(self):
        js_path = os.path.join(PROJECT_DIR, "emulator_backend.js")
        self.backend_proc = subprocess.Popen(
            ["node", js_path],
            cwd=PROJECT_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        time.sleep(0.6)

    def connect_backend(self):
        for _ in range(20):
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect(("127.0.0.1", BACKEND_PORT))
                self.sock.settimeout(0.1)
                print("Connected to Bullet OS Backend Engine!")
                return
            except Exception:
                time.sleep(0.1)
        print("Failed to connect to backend engine.")

    def create_widgets(self):
        # 1. Top Status Header
        hdr_frame = tk.Frame(self.root, bg="#0d1420", height=40)
        hdr_frame.pack(fill=tk.X, padx=10, pady=(10, 5))

        title_lbl = tk.Label(
            hdr_frame, text="⚡ BULLET OS v0.2.1 (ESP32-S3 HAL)",
            font=("Consolas", 11, "bold"), fg="#70d6ff", bg="#0d1420"
        )
        title_lbl.pack(side=tk.LEFT, padx=10, pady=8)

        led_lbl = tk.Label(
            hdr_frame, text="● HARDWARE ACTIVE",
            font=("Consolas", 9, "bold"), fg="#00ffaa", bg="#0d1420"
        )
        led_lbl.pack(side=tk.RIGHT, padx=10)

        # 2. Display Bezel & Screen Canvas
        bezel_frame = tk.Frame(self.root, bg="#141e2e", bd=3, relief=tk.RIDGE)
        bezel_frame.pack(pady=10, padx=20)

        self.canvas_w = 480
        self.canvas_h = 480
        self.canvas = tk.Canvas(
            bezel_frame, width=self.canvas_w, height=self.canvas_h,
            bg="#000000", highlightthickness=0
        )
        self.canvas.pack(padx=6, pady=6)

        # 3. Hardware Controls Panel
        ctrl_panel = tk.Frame(self.root, bg="#0d1420", bd=1, relief=tk.SOLID)
        ctrl_panel.pack(fill=tk.X, padx=20, pady=5)

        # Rotary Knob Unit
        knob_box = tk.Frame(ctrl_panel, bg="#0d1420")
        knob_box.pack(side=tk.LEFT, padx=15, pady=8)

        lbl_knob = tk.Label(knob_box, text="ROTARY ENCODER", font=("Consolas", 8, "bold"), fg="#64748b", bg="#0d1420")
        lbl_knob.pack()

        knob_btn_frame = tk.Frame(knob_box, bg="#0d1420")
        knob_btn_frame.pack(pady=4)

        btn_left = tk.Button(
            knob_btn_frame, text="◄ LEFT (A)", font=("Consolas", 9, "bold"),
            bg="#1b2838", fg="#70d6ff", activebackground="#70d6ff", activeforeground="#000",
            command=lambda: self.send_knob(0), width=10, relief=tk.FLAT
        )
        btn_left.pack(side=tk.LEFT, padx=4)

        btn_right = tk.Button(
            knob_btn_frame, text="RIGHT (D) ►", font=("Consolas", 9, "bold"),
            bg="#1b2838", fg="#70d6ff", activebackground="#70d6ff", activeforeground="#000",
            command=lambda: self.send_knob(1), width=10, relief=tk.FLAT
        )
        btn_right.pack(side=tk.LEFT, padx=4)

        # Push Button Unit
        btn_box = tk.Frame(ctrl_panel, bg="#0d1420")
        btn_box.pack(side=tk.RIGHT, padx=15, pady=8)

        lbl_btn = tk.Label(btn_box, text="HARDWARE BUTTON", font=("Consolas", 8, "bold"), fg="#64748b", bg="#0d1420")
        lbl_btn.pack()

        btn_row = tk.Frame(btn_box, bg="#0d1420")
        btn_row.pack(pady=4)

        btn_click = tk.Button(
            btn_row, text="CLICK (ENTER)", font=("Consolas", 9, "bold"),
            bg="#00ffaa", fg="#000000", activebackground="#70d6ff",
            command=lambda: self.send_btn(0), width=14, relief=tk.FLAT
        )
        btn_click.pack(side=tk.LEFT, padx=4)

        btn_back = tk.Button(
            btn_row, text="BACK (ESC)", font=("Consolas", 9, "bold"),
            bg="#ff5577", fg="#ffffff", activebackground="#ff7799",
            command=lambda: self.send_btn(2), width=11, relief=tk.FLAT
        )
        btn_back.pack(side=tk.LEFT, padx=4)

        # 4. Display Mode Switcher
        mode_frame = tk.Frame(self.root, bg="#080c14")
        mode_frame.pack(fill=tk.X, padx=20, pady=4)

        modes = [
            ("OLED 128x64", 0),
            ("IPS 240x240", 1),
            ("CYD 320x240", 2),
            ("HVGA 480x320", 3)
        ]
        for text, m_id in modes:
            b = tk.Button(
                mode_frame, text=text, font=("Consolas", 8),
                bg="#141e2e", fg="#94a3b8", activebackground="#70d6ff", activeforeground="#000",
                command=lambda m=m_id: self.send_mode(m), relief=tk.FLAT
            )
            b.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=2)

        # 5. Terminal Direct Text Input Bar
        input_frame = tk.Frame(self.root, bg="#0d1420", bd=1, relief=tk.SOLID)
        input_frame.pack(fill=tk.X, padx=20, pady=(4, 10))

        self.cmd_entry = tk.Entry(
            input_frame, font=("Consolas", 10), bg="#06090e", fg="#ffffff",
            insertbackground="#00ffaa", relief=tk.FLAT
        )
        self.cmd_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=8, pady=6)
        self.cmd_entry.bind("<Return>", self.on_text_submit)

        send_btn = tk.Button(
            input_frame, text="SEND CLI", font=("Consolas", 8, "bold"),
            bg="#70d6ff", fg="#000", command=self.on_text_submit, relief=tk.FLAT, padx=10
        )
        send_btn.pack(side=tk.RIGHT, padx=6, pady=6)

    def bind_events(self):
        self.root.bind("<Left>", lambda e: self.send_knob(0))
        self.root.bind("<Right>", lambda e: self.send_knob(1))
        self.root.bind("<Up>", lambda e: self.send_knob(1))
        self.root.bind("<Down>", lambda e: self.send_knob(0))
        self.root.bind("<a>", lambda e: self.send_knob(0) if self.cmd_entry != self.root.focus_get() else None)
        self.root.bind("<d>", lambda e: self.send_knob(1) if self.cmd_entry != self.root.focus_get() else None)
        self.root.bind("<w>", lambda e: self.send_knob(1) if self.cmd_entry != self.root.focus_get() else None)
        self.root.bind("<s>", lambda e: self.send_knob(0) if self.cmd_entry != self.root.focus_get() else None)
        self.root.bind("<space>", lambda e: self.send_btn(0) if self.cmd_entry != self.root.focus_get() else None)
        self.root.bind("<Return>", lambda e: self.send_btn(0) if self.cmd_entry != self.root.focus_get() else None)
        self.root.bind("<Escape>", lambda e: self.send_btn(2))
        self.root.bind("<BackSpace>", lambda e: self.send_btn(2) if self.cmd_entry != self.root.focus_get() else None)

    def send_knob(self, dir_val):
        if self.sock:
            try:
                self.sock.sendall(f"KNOB {dir_val}\n".encode())
            except Exception:
                pass

    def send_btn(self, action_val):
        if self.sock:
            try:
                self.sock.sendall(f"BTN {action_val}\n".encode())
            except Exception:
                pass

    def send_mode(self, mode_val):
        self.current_mode = mode_val
        if self.sock:
            try:
                self.sock.sendall(f"MODE {mode_val}\n".encode())
            except Exception:
                pass

    def on_text_submit(self, event=None):
        text = self.cmd_entry.get().strip()
        if not text:
            return
        self.cmd_entry.delete(0, tk.END)
        if self.sock:
            try:
                for char in text:
                    self.sock.sendall(f"CHAR {ord(char)}\n".encode())
                    time.sleep(0.01)
                self.sock.sendall(b"CHAR 10\n") # Enter
            except Exception:
                pass

    def update_frame(self):
        if self.sock:
            try:
                self.sock.sendall(b"RENDER\n")
                hdr = self.recv_exact(8)
                if hdr:
                    w, h = struct.unpack("<II", hdr)
                    if 0 < w <= 480 and 0 < h <= 320:
                        raw_data = self.recv_exact(w * h * 4)
                        if raw_data:
                            img = Image.frombytes("RGBA", (w, h), raw_data)
                            # Scale crisp to fit canvas
                            scale_factor = min(self.canvas_w // w, self.canvas_h // h)
                            if scale_factor < 1:
                                scale_factor = 1
                            scaled_img = img.resize((w * scale_factor, h * scale_factor), Image.NEAREST)

                            # Center on canvas
                            final_canvas_img = Image.new("RGBA", (self.canvas_w, self.canvas_h), (0, 0, 0, 255))
                            offset_x = (self.canvas_w - (w * scale_factor)) // 2
                            offset_y = (self.canvas_h - (h * scale_factor)) // 2
                            final_canvas_img.paste(scaled_img, (offset_x, offset_y))

                            self.photo_img = ImageTk.PhotoImage(final_canvas_img)
                            self.canvas.create_image(0, 0, anchor=tk.NW, image=self.photo_img)
            except Exception as e:
                pass

        self.root.after(16, self.update_frame) # ~60 FPS

    def recv_exact(self, num_bytes):
        data = bytearray()
        while len(data) < num_bytes:
            packet = self.sock.recv(num_bytes - len(data))
            if not packet:
                return None
            data.extend(packet)
        return bytes(data)

    def on_close(self):
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
        if self.backend_proc:
            try:
                self.backend_proc.terminate()
            except Exception:
                pass
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = BulletOSEmulatorApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()
