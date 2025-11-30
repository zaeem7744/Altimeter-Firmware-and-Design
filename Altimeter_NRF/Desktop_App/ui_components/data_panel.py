# ui_components/data_panel.py - FIXED CIRCULAR IMPORT
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QPushButton, 
                             QTextEdit, QCheckBox, QGroupBox, QTabWidget, 
                             QTableWidget, QTableWidgetItem, QHeaderView, QLabel, QFileDialog)
from PyQt6.QtCore import pyqtSignal, QEvent
from PyQt6.QtGui import QFont
import pyqtgraph as pg
import numpy as np

# Remove the circular import - FlightVisualization will be imported elsewhere
# from visualization import FlightVisualization

class DataPanel(QWidget):
    # Signals
    auto_scroll_changed = pyqtSignal(bool)
    clear_logs_requested = pyqtSignal()
    save_logs_requested = pyqtSignal()
    
    def __init__(self):
        super().__init__()
        self.flight_viz = None  # Will be set later
        self._last_x_max = None
        self._last_y_min = None
        self._last_y_max = None
        self.setup_ui()
        
    def setup_ui(self):
        layout = QVBoxLayout(self)
        
        # Create tabs
        self.tabs = QTabWidget()
        
        # Real-time Data Tab
        realtime_tab = self.create_realtime_tab()
        self.tabs.addTab(realtime_tab, "📊 Real-time Data")
        
        # Extracted Data Tab
        extracted_tab = self.create_extracted_tab()
        self.extracted_tab_index = self.tabs.addTab(extracted_tab, "💾 Extracted Data")
        
        layout.addWidget(self.tabs)
        
    def create_realtime_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        
        # Communication Logs
        log_group = self.create_log_group()
        layout.addWidget(log_group)
        
        # Visualization
        viz_group = self.create_viz_group()
        layout.addWidget(viz_group)
        
        return tab
        
    def create_extracted_tab(self):
        """Create tab for extracted flash data - UPDATED WITH PROGRESS"""
        tab = QWidget()
        layout = QVBoxLayout(tab)
        
        # Progress indicator
        self.export_progress_label = QLabel("No data extraction in progress")
        self.export_progress_label.setStyleSheet("color: #666; padding: 5px;")
        layout.addWidget(self.export_progress_label)
        
        # Data info
        self.extracted_info_label = QLabel("No data extracted yet. Use 'Extract Data from Flash' to load data.")
        self.extracted_info_label.setStyleSheet("color: #666; font-style: italic; padding: 10px;")
        layout.addWidget(self.extracted_info_label)
        
        # Data table for extracted data
        self.extracted_data_table = QTableWidget()
        self.extracted_data_table.setColumnCount(4)
        self.extracted_data_table.setHorizontalHeaderLabels([
            "Timestamp", "Device Time (ms)", "Altitude (m)", "Acceleration (m/s²)"
        ])
        self.extracted_data_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        layout.addWidget(self.extracted_data_table)
        
        return tab
        
    def create_log_group(self):
        group = QGroupBox("Live Communication Logs")
        layout = QVBoxLayout(group)
        
        # Log controls
        log_controls = QHBoxLayout()
        
        self.auto_scroll_check = QCheckBox("Auto Scroll")
        self.auto_scroll_check.setChecked(True)
        self.auto_scroll_check.stateChanged.connect(
            lambda state: self.auto_scroll_changed.emit(state == 2)
        )
        log_controls.addWidget(self.auto_scroll_check)
        
        btn_clear_logs = QPushButton("Clear Logs")
        btn_clear_logs.clicked.connect(self.clear_logs_requested.emit)
        log_controls.addWidget(btn_clear_logs)
        
        btn_save_logs = QPushButton("Save Logs")
        btn_save_logs.clicked.connect(self.save_logs_requested.emit)
        log_controls.addWidget(btn_save_logs)
        
        log_controls.addStretch()
        layout.addLayout(log_controls)
        
        self.communication_log = QTextEdit()
        self.communication_log.setMaximumHeight(200)
        self.communication_log.setFont(QFont("Courier", 9))
        layout.addWidget(self.communication_log)
        
        return group
        
    def create_viz_group(self):
        group = QGroupBox("Flight Data Visualization")
        layout = QVBoxLayout(group)
        
        # Controls above the plot
        controls = QHBoxLayout()
        btn_set_origin = QPushButton("Set Origin (0,0)")
        btn_set_origin.clicked.connect(self.on_set_origin)
        controls.addWidget(btn_set_origin)
        
        btn_save_graph = QPushButton("Save Graph")
        btn_save_graph.clicked.connect(self.on_save_graph)
        controls.addWidget(btn_save_graph)
        
        controls.addStretch()
        layout.addLayout(controls)
        
        self.plot_widget = pg.PlotWidget()
        
        # Setup basic plot appearance
        self.plot_widget.setBackground('w')
        self.plot_widget.setLabel('left', 'Altitude', 'm')
        self.plot_widget.setLabel('bottom', 'Time', 's')
        self.plot_widget.showGrid(x=True, y=True, alpha=0.3)
        self.plot_widget.addLegend()
        # Auto range by default
        self.plot_widget.getViewBox().enableAutoRange(x=True, y=True)
        # Auto-fit on resize
        self.plot_widget.installEventFilter(self)
        
        layout.addWidget(self.plot_widget)
        
        return group
        
    def add_log_message(self, message):
        self.communication_log.append(message)
        
    def clear_logs(self):
        self.communication_log.clear()
        
    def update_export_progress(self, current, total):
        """Update progress display during data export"""
        if total > 0:
            percent = (current / total) * 100
            self.export_progress_label.setText(
                f"🔄 Extracting data: {current}/{total} samples ({percent:.1f}%)"
            )
            self.export_progress_label.setStyleSheet("color: #0066cc; font-weight: bold; padding: 5px;")
        else:
            self.export_progress_label.setText("No data extraction in progress")
            self.export_progress_label.setStyleSheet("color: #666; padding: 5px;")
                
    def update_extracted_data_table(self, flight_data):
        """Update the extracted data table with flash data"""
        if flight_data.empty:
            self.extracted_data_table.setRowCount(0)
            self.extracted_info_label.setText("No data extracted yet. Use 'Extract Data from Flash' to load data.")
            return
            
        self.extracted_data_table.setRowCount(len(flight_data))
        self.extracted_info_label.setText(f"Displaying {len(flight_data)} samples extracted from flash memory")
        
        for row, (_, data_row) in enumerate(flight_data.iterrows()):
            # Timestamp (formatted)
            if "timestamp" in data_row:
                self.extracted_data_table.setItem(row, 0, 
                    QTableWidgetItem(data_row["timestamp"].strftime("%Y-%m-%d %H:%M:%S")))
                
            # Device timestamp (raw milliseconds)
            if "device_timestamp" in data_row:
                self.extracted_data_table.setItem(row, 1, 
                    QTableWidgetItem(str(data_row["device_timestamp"])))
                    
            # Altitude
            if "altitude" in data_row:
                self.extracted_data_table.setItem(row, 2, 
                    QTableWidgetItem(f"{data_row['altitude']:.2f}"))
                    
            # Acceleration
            if "acceleration" in data_row:
                self.extracted_data_table.setItem(row, 3, 
                    QTableWidgetItem(f"{data_row['acceleration']:.2f}"))
                
        # Auto-switch to extracted data tab
        self.switch_to_extracted_tab()
                
    def update_data_table(self, flight_data):
        # This method is kept for compatibility but we use update_extracted_data_table now
        pass
            
    def detect_phase_from_data(self, data_row):
        if "altitude" not in data_row:
            return "Unknown"
            
        alt = data_row["altitude"]
        if alt < 10:
            return "Lift-off"
        elif alt < 100:
            return "Ascent"
        else:
            return "Coasting"
            
    def update_visualization(self, flight_data, phases=None):
        """Update the plot with flight data"""
        if flight_data.empty:
            return
            
        self.plot_widget.clear()
        
        # Convert timestamps to seconds from start
        if "timestamp" in flight_data.columns:
            start_time = flight_data["timestamp"].min()
            time_seconds = [(ts - start_time).total_seconds() for ts in flight_data["timestamp"]]
            
            ymins = []
            ymaxs = []
            
            # Plot altitude (raw)
            if "altitude" in flight_data.columns:
                alt_vals = flight_data["altitude"].values
                self.plot_widget.plot(
                    time_seconds, 
                    alt_vals,
                    pen=pg.mkPen(color='blue', width=2),
                    name="Altitude"
                )
                if len(alt_vals) > 0:
                    ymins.append(float(np.nanmin(alt_vals)))
                    ymaxs.append(float(np.nanmax(alt_vals)))
                
            # Plot acceleration if available (raw)
            if "acceleration" in flight_data.columns:
                acc_vals = flight_data["acceleration"].values
                self.plot_widget.plot(
                    time_seconds,
                    acc_vals,
                    pen=pg.mkPen(color='green', width=2),
                    name="Acceleration"
                )
                if len(acc_vals) > 0:
                    ymins.append(float(np.nanmin(acc_vals)))
                    ymaxs.append(float(np.nanmax(acc_vals)))
            
            # Cache ranges for Set Origin button
            if len(time_seconds) > 0:
                self._last_x_max = float(max(time_seconds))
            if ymins and ymaxs:
                self._last_y_min = min(ymins)
                self._last_y_max = max(ymaxs)
            
            # Auto-fit to data immediately
            self.plot_widget.getViewBox().autoRange()
            
    def switch_to_extracted_tab(self):
        """Switch to the extracted data tab"""
        self.tabs.setCurrentIndex(self.extracted_tab_index)
        
    def clear_visualization(self):
        """Clear the plot and reset cached ranges"""
        self.plot_widget.clear()
        # Re-setup basic plot appearance
        self.plot_widget.setBackground('w')
        self.plot_widget.setLabel('left', 'Altitude', 'm')
        self.plot_widget.setLabel('bottom', 'Time', 's')
        self.plot_widget.showGrid(x=True, y=True, alpha=0.3)
        self.plot_widget.addLegend()
        self._last_x_max = None
        self._last_y_min = None
        self._last_y_max = None
        # Keep auto-range enabled
        self.plot_widget.getViewBox().enableAutoRange(x=True, y=True)
        
    def eventFilter(self, obj, event):
        if obj is self.plot_widget and event.type() == QEvent.Type.Resize:
            try:
                self.plot_widget.getViewBox().autoRange()
            except Exception:
                pass
        return False
        
    def on_set_origin(self):
        """Reset/align view to origin and data extents"""
        try:
            if self._last_x_max is not None and self._last_y_min is not None and self._last_y_max is not None:
                self.plot_widget.setXRange(0, self._last_x_max, padding=0.05)
                self.plot_widget.setYRange(self._last_y_min, self._last_y_max, padding=0.1)
            else:
                # Fallback to auto-range
                self.plot_widget.getViewBox().enableAutoRange(x=True, y=True)
                self.plot_widget.getViewBox().autoRange()
        except Exception:
            pass
        
    def on_save_graph(self):
        """Save the current graph as an image"""
        filename, _ = QFileDialog.getSaveFileName(self, "Save Graph", "flight_plot.png", "PNG Files (*.png);;JPEG Files (*.jpg);;All Files (*)")
        if filename:
            try:
                from pyqtgraph.exporters import ImageExporter
                exporter = ImageExporter(self.plot_widget.plotItem)
                exporter.parameters()['width'] = max(800, self.plot_widget.width())
                exporter.export(filename)
            except Exception:
                # Fallback simple grab if exporter not available
                try:
                    pix = self.plot_widget.grab()
                    pix.save(filename)
                except Exception:
                    pass
