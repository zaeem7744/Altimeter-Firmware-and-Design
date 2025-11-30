# config.py - UPDATED
# BLE Configuration
DEVICE_NAME = "RocketTelemetry"
SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
DATA_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"
COMMAND_UUID = "19B10002-E8F2-537E-4F6C-D104768A1214"

# Connection settings
SCAN_TIMEOUT = 5
CONNECTION_TIMEOUT = 5
RECONNECT_DELAY = 2

# Data processing
MAX_DATA_POINTS = 1000
SAMPLING_RATE = 10  # Hz

# Flash storage configuration (matches Arduino)
TOTAL_SAMPLES = 24576  # 96 sectors * 256 samples
SAMPLE_RATE_HZ = 50

# Commands (matches Arduino)
CMD_STATUS = "STATUS"
CMD_MEMORY_STATUS = "MEMORY_STATUS"
CMD_EXTRACT_DATA = "EXTRACT_DATA"
CMD_CLEAR_MEMORY = "CLEAR_MEMORY"

# Flight phases colors
PHASE_COLORS = {
    "Lift-off": "#00FF00",      # Green
    "Ascent": "#00FF00",        # Green  
    "Peak": "#FFFF00",          # Yellow
    "Descent": "#FF0000",       # Red
    "Ejection": "#FFA500",      # Orange
    "Touchdown": "#800080"      # Purple
}