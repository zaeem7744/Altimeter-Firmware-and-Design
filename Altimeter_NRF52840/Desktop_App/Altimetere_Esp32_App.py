import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, filedialog
import serial
import threading
import time
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import re
import queue
import os
from datetime import datetime

class ESP32DesktopApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32 Data Logger - Real Time")
        self.root.geometry("900x700")
        
        self.ser = None
        self.connected = False
        self.data_queue = queue.Queue()
        self.current_data = []
        self.last_stats = "No data available"
        
        self.setup_gui()
        self.setup_serial_thread()
        
    def setup_gui(self):
        # Main container
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.pack(fill="both", expand=True)
        
        # Connection section
        conn_frame = ttk.LabelFrame(main_frame, text="Connection", padding="10")
        conn_frame.pack(fill="x", pady=(0, 10))
        
        conn_subframe = ttk.Frame(conn_frame)
        conn_subframe.pack(fill="x")
        
        ttk.Label(conn_subframe, text="COM Port:").pack(side="left", padx=(0, 5))
        self.port_var = tk.StringVar(value="COM9")
        self.port_entry = ttk.Entry(conn_subframe, textvariable=self.port_var, width=12)
        self.port_entry.pack(side="left", padx=(0, 10))
        
        self.connect_btn = ttk.Button(conn_subframe, text="Connect", command=self.toggle_connection)
        self.connect_btn.pack(side="left", padx=(0, 10))
        
        self.status_var = tk.StringVar(value="🔴 Disconnected")
        self.status_label = ttk.Label(conn_subframe, textvariable=self.status_var, font=("Arial", 10, "bold"))
        self.status_label.pack(side="left")
        
        # Quick actions frame
        actions_frame = ttk.LabelFrame(main_frame, text="Quick Actions", padding="10")
        actions_frame.pack(fill="x", pady=(0, 10))
        
        buttons_frame = ttk.Frame(actions_frame)
        buttons_frame.pack(fill="x")
        
        self.stats_btn = ttk.Button(buttons_frame, text="📊 Get Stats", command=self.get_stats, state="disabled")
        self.stats_btn.pack(side="left", padx=2)
        
        self.download_btn = ttk.Button(buttons_frame, text="📥 Download Data", command=self.download_data, state="disabled")
        self.download_btn.pack(side="left", padx=2)
        
        self.plot_btn = ttk.Button(buttons_frame, text="📈 Plot Data", command=self.plot_data, state="disabled")
        self.plot_btn.pack(side="left", padx=2)
        
        self.erase_btn = ttk.Button(buttons_frame, text="🗑️ Erase Memory", command=self.erase_memory, state="disabled")
        self.erase_btn.pack(side="left", padx=2)
        
        self.disconnect_btn = ttk.Button(buttons_frame, text="🔌 Disconnect", command=self.send_disconnect, state="disabled")
        self.disconnect_btn.pack(side="left", padx=2)
        
        # Live Statistics
        stats_frame = ttk.LabelFrame(main_frame, text="Live Statistics", padding="10")
        stats_frame.pack(fill="x", pady=(0, 10))
        
        self.stats_text = tk.Text(stats_frame, height=3, width=80, font=("Consolas", 9))
        self.stats_text.pack(fill="x")
        self.stats_text.insert("1.0", "No data available - Connect to ESP32")
        self.stats_text.config(state="disabled")
        
        # Logs section
        log_frame = ttk.LabelFrame(main_frame, text="Communication Logs - Real Time", padding="10")
        log_frame.pack(fill="both", expand=True)
        
        log_controls = ttk.Frame(log_frame)
        log_controls.pack(fill="x", pady=(0, 5))
        
        ttk.Button(log_controls, text="📁 Save Logs", command=self.save_logs).pack(side="left", padx=(0, 5))
        ttk.Button(log_controls, text="🗑️ Clear Logs", command=self.clear_logs).pack(side="left", padx=(0, 5))
        ttk.Button(log_controls, text="🔄 Auto-scroll", command=self.toggle_auto_scroll).pack(side="left")
        
        self.auto_scroll = tk.BooleanVar(value=True)
        
        self.log_text = scrolledtext.ScrolledText(log_frame, height=15, width=80, font=("Consolas", 9))
        self.log_text.pack(fill="both", expand=True)
        
    def setup_serial_thread(self):
        self.serial_thread = None
        self.stop_thread = False
        
    def log_message(self, message, sender="APP"):
        timestamp = time.strftime("%H:%M:%S")
        log_entry = f"[{timestamp}] [{sender}] {message}\n"
        
        self.log_text.insert(tk.END, log_entry)
        
        if self.auto_scroll.get():
            self.log_text.see(tk.END)
            
    def update_stats(self, stats_text):
        self.last_stats = stats_text
        self.stats_text.config(state="normal")
        self.stats_text.delete("1.0", tk.END)
        
        if stats_text.startswith("STATS:"):
            samples_match = re.search(r'Samples=(\d+)', stats_text)
            used_match = re.search(r'Used=(\d+)B', stats_text)
            total_match = re.search(r'Total=(\d+)B', stats_text)
            usage_match = re.search(r'Usage=([\d.]+)%', stats_text)
            
            if all([samples_match, used_match, total_match, usage_match]):
                formatted = f"""📊 LIVE STATISTICS (Real Time)
Samples: {samples_match.group(1)}
Memory: {used_match.group(1)} / {total_match.group(1)} bytes
Usage: {usage_match.group(1)}%"""
                
                self.stats_text.insert("1.0", formatted)
                
                samples_count = int(samples_match.group(1))
                if samples_count > 0:
                    self.plot_btn.config(state="normal")
                else:
                    self.plot_btn.config(state="disabled")
                    
        else:
            self.stats_text.insert("1.0", stats_text)
            
        self.stats_text.config(state="disabled")
        
    def toggle_connection(self):
        if not self.connected:
            self.connect_to_esp32()
        else:
            self.disconnect_from_esp32()
            
    def connect_to_esp32(self):
        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("Error", "Please enter a COM port")
            return
            
        try:
            self.ser = serial.Serial(port, 115200, timeout=1)  # Reduced timeout
            self.connected = True
            self.stop_thread = False
            
            # Start monitoring thread
            self.serial_thread = threading.Thread(target=self.monitor_serial)
            self.serial_thread.daemon = True
            self.serial_thread.start()
            
            # Update UI
            self.connect_btn.config(text="Disconnect")
            self.status_var.set("🟢 Connected")
            self.update_button_states(True)
            
            self.log_message(f"Connected to {port}")
            self.log_message("Triple-press ESP32 button to start Bluetooth mode")
            
            # Auto-get stats after connection
            self.root.after(1000, self.get_stats)
            
        except Exception as e:
            self.log_message(f"Connection failed: {str(e)}", "ERROR")
            messagebox.showerror("Connection Error", f"Cannot connect to {port}")
            
    def disconnect_from_esp32(self):
        self.connected = False
        self.stop_thread = True
        
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except:
                pass
                
        self.connect_btn.config(text="Connect")
        self.status_var.set("🔴 Disconnected")
        self.update_button_states(False)
        self.update_stats("Disconnected - No data available")
        
        self.log_message("Disconnected from ESP32")
        
    def update_button_states(self, connected):
        state = "normal" if connected else "disabled"
        self.stats_btn.config(state=state)
        self.download_btn.config(state=state)
        self.erase_btn.config(state=state)
        self.disconnect_btn.config(state=state)
        
    def monitor_serial(self):
        """Real-time serial monitoring with immediate processing"""
        while self.connected and not self.stop_thread:
            try:
                if self.ser and self.ser.in_waiting > 0:
                    # Read all available data immediately
                    data = self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore')
                    lines = data.split('\n')
                    
                    for line in lines:
                        line = line.strip()
                        if line:
                            self.data_queue.put(("ESP32", line))
                            
                    # Process queue immediately
                    self.root.after(10, self.process_queue)
                    
                time.sleep(0.01)  # Small delay for CPU
                
            except Exception as e:
                if self.connected:
                    self.data_queue.put(("ERROR", f"Serial error: {str(e)}"))
                break
                
    def process_queue(self):
        """Process messages immediately"""
        try:
            while True:
                sender, message = self.data_queue.get_nowait()
                
                if sender == "ESP32":
                    self.log_message(message, "ESP32")
                    
                    # Immediate response handling
                    if message.startswith("STATS:"):
                        self.update_stats(message)
                    elif message == "DATA_START":
                        self.current_data = []
                        self.log_message("📥 Data transmission started...")
                    elif message == "DATA_END":
                        self.log_message(f"✅ Data received: {len(self.current_data)} samples")
                        if len(self.current_data) > 0:
                            self.plot_btn.config(state="normal")
                        # Auto-refresh stats
                        self.root.after(300, self.get_stats)
                    elif "ERASE_SUCCESS" in message:
                        self.log_message("✅ Memory erased successfully")
                        # Auto-refresh stats immediately
                        self.root.after(300, self.get_stats)
                    elif "DISCONNECT_ACK" in message:
                        self.log_message("ESP32 acknowledged disconnect")
                        self.root.after(500, self.disconnect_from_esp32)
                    elif message and not any(msg in message for msg in ["STATS:", "ERROR:", "DATA_", "ERASE_", "VERIFY:", "DISCONNECT_"]):
                        try:
                            value = int(message)
                            self.current_data.append(value)
                        except ValueError:
                            pass
                            
                elif sender == "ERROR":
                    self.log_message(message, "ERROR")
                    if "Connection lost" in message:
                        self.root.after(100, self.disconnect_from_esp32)
                        
        except queue.Empty:
            pass
            
    def send_command(self, command):
        if self.connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(f"{command}\n".encode())
                self.ser.flush()  # Force immediate send
                self.log_message(f"Sent: {command}", "APP")
            except Exception as e:
                self.log_message(f"Failed to send: {command}", "ERROR")
                self.root.after(100, self.disconnect_from_esp32)
                
    def get_stats(self):
        self.send_command("STATS")
        
    def download_data(self):
        self.current_data = []
        self.plot_btn.config(state="disabled")
        self.send_command("GET_DATA")
        
    def plot_data(self):
        if not self.current_data:
            messagebox.showwarning("No Data", "No data available to plot.")
            return
            
        try:
            plot_window = tk.Toplevel(self.root)
            plot_window.title(f"LDR Data - {len(self.current_data)} samples")
            plot_window.geometry("800x500")
            
            fig, ax = plt.subplots(figsize=(10, 5))
            ax.plot(self.current_data, 'b-', alpha=0.8, linewidth=1)
            ax.set_title(f"LDR Sensor Readings ({len(self.current_data)} samples)", fontsize=14)
            ax.set_xlabel("Sample Number", fontsize=12)
            ax.set_ylabel("ADC Value", fontsize=12)
            ax.grid(True, alpha=0.3)
            
            canvas = FigureCanvasTkAgg(fig, plot_window)
            canvas.draw()
            canvas.get_tk_widget().pack(fill="both", expand=True, padx=10, pady=10)
            
            from matplotlib.backends.backend_tkagg import NavigationToolbar2Tk
            toolbar = NavigationToolbar2Tk(canvas, plot_window)
            toolbar.update()
            
            self.log_message(f"📈 Plot created with {len(self.current_data)} samples")
            
        except Exception as e:
            self.log_message(f"Plot error: {str(e)}", "ERROR")
            
    def erase_memory(self):
        if messagebox.askyesno("Confirm Erase", "Delete ALL stored data?"):
            self.send_command("ERASE")
            
    def send_disconnect(self):
        self.send_command("DISCONNECT")
        
    def save_logs(self):
        try:
            filename = filedialog.asksaveasfilename(
                defaultextension=".txt",
                filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
                initialfile=f"esp32_logs_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"
            )
            if filename:
                with open(filename, 'w', encoding='utf-8') as f:
                    f.write(self.log_text.get("1.0", tk.END))
                self.log_message(f"Logs saved to: {filename}")
        except Exception as e:
            messagebox.showerror("Save Error", f"Failed to save logs: {str(e)}")
            
    def clear_logs(self):
        if messagebox.askyesno("Clear Logs", "Clear all log messages?"):
            self.log_text.delete("1.0", tk.END)
            self.log_message("Logs cleared")
            
    def toggle_auto_scroll(self):
        self.auto_scroll.set(not self.auto_scroll.get())
        state = "ON" if self.auto_scroll.get() else "OFF"
        self.log_message(f"Auto-scroll {state}")
        
    def on_closing(self):
        if self.connected:
            self.disconnect_from_esp32()
        self.root.destroy()

def main():
    root = tk.Tk()
    app = ESP32DesktopApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()

if __name__ == "__main__":
    main()