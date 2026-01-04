# ui_dashboard.py - COMPLETE FIXED VERSION
import sys
from PyQt6.QtWidgets import QMainWindow, QWidget, QHBoxLayout, QSplitter, QMessageBox, QFileDialog
from PyQt6.QtCore import QTimer, Qt, pyqtSlot
from PyQt6.QtGui import QGuiApplication
import os
import pandas as pd
from config import CMD_MEMORY_STATUS, CMD_EXTRACT_DATA, CMD_CLEAR_MEMORY

# Add the current directory to Python path
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from ui_components.control_panel import ControlPanel
from ui_components.data_panel import DataPanel
from ui_components.ble_manager import BLEManager
from ui_components.data_manager import DataManager

class TelemetryDashboard(QMainWindow):
    def __init__(self):
        super().__init__()
        # Initialize managers first
        self.ble_manager = BLEManager()
        self.data_manager = DataManager()
        
        # Initialize UI components
        self.control_panel = ControlPanel()
        self.data_panel = DataPanel()
        
        self.setup_ui()
        self.setup_connections()
        self.setup_managers()
        
        # State variables
        self.is_connected = False
        self.auto_scroll = True
        self.is_exporting_data = False
        self.connection_verified = False
        self.exported_samples = 0
        self.expected_samples = 0
        
    def setup_ui(self):
        self.setWindowTitle("🚀 Rocket Telemetry System - Flash Storage Dashboard")
        
        # Set window size to fit screen with 16:9 aspect ratio
        screen = QGuiApplication.primaryScreen()
        geom = screen.availableGeometry() if screen else None
        if geom:
            target_width = int(geom.width() * 0.9)
            target_height = int(target_width * 9 / 16)
            if target_height > int(geom.height() * 0.9):
                target_height = int(geom.height() * 0.9)
                target_width = int(target_height * 16 / 9)
            self.resize(target_width, target_height)
            self.move(
                geom.x() + (geom.width() - target_width) // 2,
                geom.y() + (geom.height() - target_height) // 2,
            )
        else:
            self.resize(1280, 720)
        
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        main_layout = QHBoxLayout(central_widget)
        
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(self.control_panel)
        splitter.addWidget(self.data_panel)
        splitter.setSizes([400, 1000])
        
        main_layout.addWidget(splitter)
        
    def setup_connections(self):
        # Control Panel signals
        self.control_panel.scan_requested.connect(self.scan_devices)
        self.control_panel.connect_requested.connect(self.connect_device)
        self.control_panel.disconnect_requested.connect(self.disconnect_device)
        self.control_panel.memory_check_requested.connect(self.check_memory)
        self.control_panel.memory_erase_requested.connect(self.erase_memory)
        self.control_panel.extract_data_requested.connect(self.extract_data)
        self.control_panel.export_requested.connect(self.export_to_csv)
        self.control_panel.view_data_requested.connect(self.view_all_data)
        
        # Log controls are now on the left control panel
        self.control_panel.auto_scroll_changed.connect(self.set_auto_scroll)
        self.control_panel.clear_logs_requested.connect(self.clear_logs)
        self.control_panel.save_logs_requested.connect(self.save_logs)
        
        # Update timer
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self.update_display)
        self.update_timer.start(1000)
        
    def setup_managers(self):
        # BLE Manager signals
        self.ble_manager.devices_found.connect(self.on_devices_found)
        self.ble_manager.scan_error.connect(self.on_scan_error)
        self.ble_manager.connection_status.connect(self.on_connection_status)
        self.ble_manager.data_received.connect(self.on_data_received)
        self.ble_manager.start()
        
    @pyqtSlot(list)
    def on_devices_found(self, devices):
        self.control_panel.update_devices_list(devices)
        if devices:
            self.log_message(f"✅ Found {len(devices)} RocketTelemetry device(s)")
        else:
            self.log_message("❌ No RocketTelemetry devices found")
        
    @pyqtSlot(str)
    def on_scan_error(self, error_message):
        self.log_message(f"❌ {error_message}")
        QMessageBox.warning(self, "Scan Error", error_message)
        
    @pyqtSlot(str, str)
    def on_connection_status(self, status, message):
        self.control_panel.update_connection_status(status, message)
        self.log_message(f"📡 {message}")
        
        if status == "connected":
            self.is_connected = True
            self.connection_verified = True
            QTimer.singleShot(1000, self.check_memory)
        elif status == "disconnected":
            self.is_connected = False
            self.connection_verified = False
        elif status == "error":
            self.log_message(f"⚠️ BLE Error: {message}")
        
    @pyqtSlot(str)
    def on_data_received(self, data):
        """Handle all incoming data from BLE - SIMPLIFIED AND FIXED"""
        print(f"📨 DASHBOARD RECEIVED: '{data}'")
        
        # Handle export progress
        if data.startswith("EXPORT_PROGRESS:"):
            progress_parts = data.split(":")
            if len(progress_parts) > 1:
                progress_info = progress_parts[1].split("/")
                if len(progress_info) == 2:
                    current = int(progress_info[0])
                    total = int(progress_info[1])
                    self.expected_samples = total
                    progress_percent = (current / total) * 100 if total > 0 else 0
                    self.log_message(f"📊 Export progress: {current}/{total} ({progress_percent:.1f}%)")
                    self.data_panel.update_export_progress(current, total)
            return
            
        # Process memory status
        if data.startswith("MEMORY:"):
            print("🔄 Processing memory data")
            self.process_memory_data(data)
            return
            
        # Handle heartbeat
        if "DEVICE_ALIVE" in data:
            print("💓 Heartbeat received")
            if not self.is_connected or not self.connection_verified:
                self.is_connected = True
                self.connection_verified = True
                self.control_panel.update_connection_status("connected", "Device connected")
            return
            
        # Handle memory cleared
        if data == "MEMORY_CLEARED":
            print("🗑️ Memory cleared confirmation received")
            self.data_manager.process_incoming_data(data)
            memory_status = self.data_manager.get_memory_status()
            self.control_panel.update_memory_display(memory_status)
            self.data_panel.update_extracted_data_table(self.data_manager.get_flight_data())
            self.data_panel.clear_visualization()
            self.log_message("🗑️ Memory cleared on device")
            return
            
        # Handle data export control messages
        if data.startswith("t_ms,") or data.startswith("timestamp,"):
            # CSV header from firmware - ignore, processor handles data lines only
            return
        
        if data == "BEGIN_DATA_EXPORT":
            print("🚀 Data export started")
            self.is_exporting_data = True
            self.exported_samples = 0
            self.expected_samples = 0
            self.log_message("📥 Arduino started controlled data export...")
            # Clear previous data
            self.data_manager.clear_data()
            self.data_panel.update_extracted_data_table(self.data_manager.get_flight_data())
            return
            
        elif data == "END_DATA_EXPORT":
            print("🏁 Data export ended")
            self.log_message("📦 Arduino finished sending data")
            return
            
        elif data == "DATA_EXPORT_COMPLETE":
            print("✅ Data export completed")
            self.is_exporting_data = False
            self.log_message("✅ Arduino data export completed!")
            
            # Debug the export data before processing
            self.data_manager.data_processor.debug_export_data()
            
            # Force immediate processing
            self.process_data_export_complete()
            return
            
        # Handle CSV data lines - SIMPLIFIED PROCESSING
        if self._is_csv_data(data):
            self.exported_samples += 1
            if self.exported_samples % 50 == 0:
                self.log_message(f"📥 Received {self.exported_samples} samples...")
                
            # Process through data_manager - SIMPLE AND DIRECT
            self.data_manager.process_incoming_data(data)
            return
            
        # Process other data types
        self.data_manager.process_incoming_data(data)
            
        # Log other messages
        if not data.startswith("TELEMETRY:") and not data.startswith("STATUS:"):
            self.log_message(f"📨 {data}")

    def _is_csv_data(self, data):
        """Check if data is CSV format"""
        if (',' in data and 
            not data.startswith('STATUS:') and 
            not data.startswith('MEMORY:') and
            data != 'BEGIN_DATA_EXPORT' and
            data != 'END_DATA_EXPORT' and
            data != 'DATA_EXPORT_COMPLETE' and
            not data.startswith('timestamp,altitude,acceleration') and
            not data.startswith('DEVICE_ALIVE') and
            not data.startswith('TELEMETRY:') and
            not data.startswith('EXPORT_PROGRESS:')):
            
            parts = data.split(',')
            if len(parts) >= 3:
                try:
                    int(parts[0])  # timestamp
                    float(parts[1])  # altitude
                    float(parts[2])  # acceleration
                    return True
                except:
                    return False
        return False
        
    def process_memory_data(self, data):
        """Process memory status data"""
        print(f"🔄 Processing memory: {data}")
        processed_data = self.data_manager.data_processor.process_raw_data(data)
        if processed_data and processed_data.get("data_type") == "memory":
            memory_status = self.data_manager.data_processor.get_memory_status(processed_data)
            self.control_panel.update_memory_display(memory_status)
            self.log_message(f"💾 Memory: {memory_status['total_samples']} samples ({memory_status['usage_percent']}% used)")
        else:
            print(f"❌ Failed to process memory data: {data}")
        
    def process_data_export_complete(self):
        """Handle completion of data export - UPDATED WITH PROGRESS"""
        flight_data = self.data_manager.get_flight_data()
        samples_count = len(flight_data)
        
        self.log_message(f"📊 Processing {samples_count} extracted samples...")
        
        # Reset progress display
        self.data_panel.update_export_progress(0, 0)
        
        if samples_count > 0:
            # Update the extracted data table
            self.data_panel.update_extracted_data_table(flight_data)
            
            # Update statistics
            stats = self.data_manager.get_statistics()
            self.control_panel.update_statistics(stats)
            
            # Auto-plot the data
            phases = self.data_manager.get_flight_phases()
            self.data_panel.update_visualization(flight_data, phases)
            
            # Show detailed success message
            success_msg = (
                f"✅ Data Extraction Successful!\n\n"
                f"📊 Samples Loaded: {samples_count}\n"
                f"📈 Data Plotted Automatically\n"
                f"🕒 Duration: {stats.get('duration', 0):.1f}s\n"
                f"📏 Max Altitude: {stats.get('max_altitude', 0):.1f}m\n"
                f"🚀 Max Acceleration: {stats.get('max_acceleration', 0):.1f}m/s²\n\n"
                f"💾 Data available in 'Extracted Data' tab"
            )
            
            self.log_message(f"🎉 Extraction complete! {samples_count} samples loaded and plotted")
            QMessageBox.information(self, "Extraction Complete", success_msg)
            
            # Switch to extracted data tab
            self.data_panel.switch_to_extracted_tab()
            
        else:
            self.log_message("❌ No data was extracted from flash memory")
            QMessageBox.warning(self, "Extraction Failed", 
                              "No data was extracted from flash memory.\n"
                              "The device may be empty or there was a communication error.")
        
    def scan_devices(self):
        """Scan for BLE devices"""
        self.log_message("🔍 Scanning for BLE devices...")
        self.ble_manager.scan_devices()
        
    def connect_device(self, address):
        """Connect to a BLE device"""
        if not address:
            self.show_message("Please select a device first")
            return
            
        self.log_message(f"🔗 Connecting to {address}...")
        self.ble_manager.connect_to_device(address)
        
    def disconnect_device(self):
        """Disconnect from BLE device"""
        self.log_message("🔌 Disconnecting...")
        self.ble_manager.disconnect_device()
        
    def check_memory(self):
        """Check memory status"""
        if not self.ble_manager.is_connected():
            self.show_message("Not connected to any device")
            return
            
        self.log_message("📊 Checking memory status...")
        self.ble_manager.send_command(CMD_MEMORY_STATUS)
        
    def erase_memory(self):
        """Erase flash memory"""
        if not (self.ble_manager.is_connected() or self.is_connected):
            self.show_message("Not connected to any device")
            return
            
        reply = QMessageBox.question(
            self, "Confirm Erase", 
            "⚠️ This will erase ALL stored data!\nThis action cannot be undone.\n\nAre you sure?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            self.log_message("🗑️ Sending memory erase command...")
            self.ble_manager.send_command(CMD_CLEAR_MEMORY)
            # UI will be updated when MEMORY_CLEARED is received
            
    def generate_test_data(self):
        """Generate test data in Arduino flash memory"""
        if not self.ble_manager.is_connected():
            self.show_message("Not connected to any device")
            return
            
        reply = QMessageBox.question(
            self, "Generate Test Data", 
            "🎯 This will generate 100 test samples in the Arduino's flash memory.\nThis is useful for testing data extraction.\n\nContinue?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            self.ble_manager.send_command("GENERATE_TEST_DATA")
            self.log_message("🎯 Test data generation command sent")
            
    def extract_data(self):
        """Extract data from Arduino's flash memory"""
        if not (self.ble_manager.is_connected() or self.is_connected):
            self.show_message("Not connected to any device")
            return
        
        reply = QMessageBox.question(
            self, "Extract Data", 
            "This will extract all data stored in flash memory.\nThis may take a few moments.\n\nContinue?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            try:
                # Clear previous data and UI safely
                self.data_manager.clear_data()
                if hasattr(self.data_panel, 'clear_visualization'):
                    self.data_panel.clear_visualization()
                else:
                    try:
                        self.data_panel.plot_widget.clear()
                    except Exception:
                        pass
                self.data_panel.update_extracted_data_table(self.data_manager.get_flight_data())
                
                # Begin export
                self.is_exporting_data = True
                self.exported_samples = 0
                self.expected_samples = 0
                self.log_message("📥 Requesting data extraction from device...")
                self.ble_manager.send_command(CMD_EXTRACT_DATA)
                self.log_message("⏳ This may take a few seconds...")
            except Exception as e:
                self.log_message(f"❌ Error starting extraction: {e}")
            
    def plot_data(self):
        """Plot the extracted data"""
        flight_data = self.data_manager.get_flight_data()
        if flight_data.empty:
            self.show_message("No data to plot. Please extract data from flash first.")
            return
            
        phases = self.data_manager.get_flight_phases()
        self.data_panel.update_visualization(flight_data, phases)
        self.log_message("📈 Plotting extracted data...")
        
    def export_to_csv(self):
        """Export data to CSV file"""
        flight_data = self.data_manager.get_flight_data()
        if flight_data.empty:
            self.show_message("No data to export. Please extract data from flash first.")
            return
            
        filename, _ = QFileDialog.getSaveFileName(
            self, "Export CSV", "rocket_flight_data.csv", "CSV Files (*.csv)"
        )
        if filename:
            if self.data_manager.export_to_csv(filename):
                self.log_message(f"💾 Data exported to {filename}")
                self.show_message(f"Data successfully exported to {filename}")
            else:
                self.show_message("Error exporting data")
                
    def view_all_data(self):
        """View all extracted data"""
        flight_data = self.data_manager.get_flight_data()
        if flight_data.empty:
            self.show_message("No data to display. Extract data from flash first.")
            return
            
        self.data_panel.update_extracted_data_table(flight_data)
        self.data_panel.switch_to_extracted_tab()
        self.log_message(f"📋 Displaying {len(flight_data)} data samples")
        
    def set_auto_scroll(self, enabled):
        """Set auto-scroll for logs"""
        self.auto_scroll = enabled
        
    def clear_logs(self):
        """Clear communication logs"""
        self.control_panel.clear_log_view()
        self.log_message("📋 Logs cleared")
        
    def save_logs(self):
        """Save logs to file"""
        filename, _ = QFileDialog.getSaveFileName(
            self, "Save Logs", "communication_log.txt", "Text Files (*.txt);;All Files (*)"
        )
        if filename:
            try:
                log_content = self.control_panel.get_log_text()
                with open(filename, 'w', encoding='utf-8') as f:
                    f.write(log_content)
                self.log_message(f"💾 Logs saved to {filename}")
            except Exception as e:
                self.show_message(f"Error saving logs: {e}")
                
    def update_display(self):
        """Update display with current statistics"""
        stats = self.data_manager.get_statistics()
        self.control_panel.update_statistics(stats)
        
    def log_message(self, message):
        """Add message to log with timestamp"""
        from datetime import datetime
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.control_panel.add_log_message(f"[{timestamp}] {message}")
        
        if self.auto_scroll and hasattr(self.control_panel, "communication_log"):
            scrollbar = self.control_panel.communication_log.verticalScrollBar()
            scrollbar.setValue(scrollbar.maximum())
        
    def show_message(self, message):
        """Show information message box"""
        QMessageBox.information(self, "Information", message)
        
    def closeEvent(self, event):
        """Handle application close event"""
        self.log_message("🔌 Shutting down...")
        if hasattr(self, 'ble_manager'):
            self.ble_manager.disconnect_device()
            self.ble_manager.stop()
            self.ble_manager.wait(3000)
        event.accept()