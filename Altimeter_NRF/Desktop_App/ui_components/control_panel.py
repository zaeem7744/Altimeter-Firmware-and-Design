# ui_components/control_panel.py - UPDATED
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QPushButton, 
                             QLabel, QComboBox, QGroupBox, QProgressBar, QFrame)
from PyQt6.QtCore import pyqtSignal
from PyQt6.QtGui import QFont

class ControlPanel(QWidget):
    # Signals
    scan_requested = pyqtSignal()
    connect_requested = pyqtSignal(str)
    disconnect_requested = pyqtSignal()
    memory_check_requested = pyqtSignal()
    memory_erase_requested = pyqtSignal()
    generate_test_data_requested = pyqtSignal()
    extract_data_requested = pyqtSignal()
    export_requested = pyqtSignal()
    view_data_requested = pyqtSignal()
    
    def __init__(self):
        super().__init__()
        self.setup_ui()
        
    def setup_ui(self):
        layout = QVBoxLayout(self)
        
        # Connection Status
        status_group = self.create_status_group()
        layout.addWidget(status_group)
        
        # Device Management
        device_group = self.create_device_group()
        layout.addWidget(device_group)
        
        # Memory Management
        memory_group = self.create_memory_group()
        layout.addWidget(memory_group)
        
        # Data Controls
        data_group = self.create_data_group()
        layout.addWidget(data_group)
        
        # Statistics
        stats_group = self.create_stats_group()
        layout.addWidget(stats_group)
        
        # Info Panel
        info_group = self.create_info_group()
        layout.addWidget(info_group)
        
        layout.addStretch()
        
    def create_status_group(self):
        group = QGroupBox("Connection Status")
        layout = QVBoxLayout(group)
        
        self.connection_status = QLabel("🔴 DISCONNECTED")
        self.connection_status.setStyleSheet("font-size: 16px; font-weight: bold; color: red;")
        layout.addWidget(self.connection_status)
        
        self.device_info = QLabel("No device connected")
        layout.addWidget(self.device_info)
        
        return group
        
    def create_device_group(self):
        group = QGroupBox("Device Management")
        layout = QVBoxLayout(group)
        
        self.device_combo = QComboBox()
        self.device_combo.setPlaceholderText("Select a device...")
        layout.addWidget(self.device_combo)
        
        btn_layout = QHBoxLayout()
        
        btn_scan = QPushButton("🔍 Scan")
        btn_scan.clicked.connect(self.scan_requested.emit)
        btn_layout.addWidget(btn_scan)
        
        btn_refresh = QPushButton("🔄 Refresh")
        btn_refresh.clicked.connect(self.scan_requested.emit)
        btn_layout.addWidget(btn_refresh)
        
        layout.addLayout(btn_layout)
        
        btn_connect = QPushButton("🔗 Connect")
        btn_connect.clicked.connect(self._on_connect_clicked)
        layout.addWidget(btn_connect)
        
        btn_disconnect = QPushButton("🔌 Disconnect")
        btn_disconnect.clicked.connect(self.disconnect_requested.emit)
        layout.addWidget(btn_disconnect)
        
        return group
        
    def create_memory_group(self):
        group = QGroupBox("Flash Memory Status")
        layout = QVBoxLayout(group)
        
        self.memory_status = QLabel("Memory: Not checked")
        self.memory_status.setStyleSheet("font-weight: bold;")
        layout.addWidget(self.memory_status)
        
        self.memory_progress = QProgressBar()
        self.memory_progress.setMaximum(100)
        layout.addWidget(self.memory_progress)
        
        info_layout = QHBoxLayout()
        info_layout.addWidget(QLabel("Capacity:"))
        self.capacity_label = QLabel(f"{24576} samples")
        info_layout.addWidget(self.capacity_label)
        info_layout.addStretch()
        layout.addLayout(info_layout)
        
        btn_layout = QHBoxLayout()
        btn_check_memory = QPushButton("📊 Check Memory")
        btn_check_memory.clicked.connect(self.memory_check_requested.emit)
        btn_layout.addWidget(btn_check_memory)
        
        btn_erase_memory = QPushButton("🗑️ Erase Memory")
        btn_erase_memory.clicked.connect(self.memory_erase_requested.emit)
        btn_erase_memory.setStyleSheet("background-color: #ff4444; color: white;")
        btn_layout.addWidget(btn_erase_memory)
        
        layout.addLayout(btn_layout)
        
        return group
        
    def create_data_group(self):
        group = QGroupBox("Flash Data Management")
        layout = QVBoxLayout(group)
        
        btn_extract_data = QPushButton("📥 Extract Data from Flash")
        btn_extract_data.clicked.connect(self.extract_data_requested.emit)
        btn_extract_data.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
        layout.addWidget(btn_extract_data)
        
        btn_export_csv = QPushButton("💾 Export to CSV")
        btn_export_csv.clicked.connect(self.export_requested.emit)
        layout.addWidget(btn_export_csv)
        
        btn_view_data = QPushButton("📋 View All Data")
        btn_view_data.clicked.connect(self.view_data_requested.emit)
        layout.addWidget(btn_view_data)
        
        return group
        
    def create_stats_group(self):
        group = QGroupBox("Flight Statistics")
        layout = QVBoxLayout(group)
        
        self.stats_display = QLabel("No data available")
        self.stats_display.setStyleSheet("font-family: monospace; background-color: #f5f5f5; padding: 8px; border-radius: 4px;")
        self.stats_display.setFont(QFont("Courier", 9))
        layout.addWidget(self.stats_display)
        
        return group
        
    def create_info_group(self):
        group = QGroupBox("System Information")
        layout = QVBoxLayout(group)
        
        info_text = """
        <b>Arduino Flash Storage System</b>
        <ul>
        <li>Total Capacity: 24,576 samples</li>
        <li>Sample Rate: 50 Hz</li>
        <li>Recording Duration: ~8.2 minutes</li>
        <li>Data: Timestamp, Altitude, Acceleration</li>
        </ul>
        """
        
        info_label = QLabel(info_text)
        info_label.setStyleSheet("color: #555; font-size: 11px;")
        info_label.setWordWrap(True)
        layout.addWidget(info_label)
        
        return group
        
    def _on_connect_clicked(self):
        if self.device_combo.currentData():
            address = self.device_combo.currentData()
            self.connect_requested.emit(address)
            
    def update_connection_status(self, status, message):
        if status == "connected":
            self.connection_status.setText("🟢 CONNECTED")
            self.connection_status.setStyleSheet("color: green; font-size: 16px; font-weight: bold;")
            self.device_info.setText(message)
        elif status == "disconnected":
            self.connection_status.setText("🔴 DISCONNECTED")
            self.connection_status.setStyleSheet("color: red; font-size: 16px; font-weight: bold;")
            self.device_info.setText("No device connected")
        elif status == "error":
            self.connection_status.setText("⚠️ ERROR")
            self.connection_status.setStyleSheet("color: orange; font-size: 16px; font-weight: bold;")
            
    def update_devices_list(self, devices):
        self.device_combo.clear()
        if not devices:
            self.device_combo.addItem("No devices found")
            return
            
        for device in devices:
            display_text = f"{device['name']} ({device['address']})"
            if device['rssi'] != 'N/A':
                display_text += f" - RSSI: {device['rssi']}"
            self.device_combo.addItem(display_text, device['address'])
            
    def update_memory_display(self, memory_status):
        """Update memory display with flash storage info"""
        total_samples = memory_status.get("total_samples", 0)
        usage_percent = memory_status.get("usage_percent", 0)
        max_capacity = memory_status.get("max_capacity", 24576)
        is_full = memory_status.get("is_full", False)
        
        status_text = f"📦 {total_samples} / {max_capacity} samples"
        if is_full:
            status_text += " 🚨 FULL"
            
        self.memory_status.setText(status_text)
        self.memory_progress.setValue(usage_percent)
        
        # Update capacity label
        self.capacity_label.setText(f"{max_capacity} samples")
        
    def update_statistics(self, stats):
        if stats:
            stats_text = (
                f"Max Altitude: {stats.get('max_altitude', 'N/A'):.2f} m\n"
                f"Max Acceleration: {stats.get('max_acceleration', 'N/A'):.2f} m/s²\n"
                f"Data Points: {stats.get('data_points', 0)}\n"
                f"Duration: {stats.get('duration', 0):.2f} s"
            )
            self.stats_display.setText(stats_text)
        else:
            self.stats_display.setText("No data available")
