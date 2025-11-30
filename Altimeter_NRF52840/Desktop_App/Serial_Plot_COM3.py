import serial
import matplotlib.pyplot as plt
import numpy as np
from collections import deque
import time

class SmoothSensorVisualizer:
    def __init__(self, port='COM9', baudrate=115200, max_points=80):
        self.ser = serial.Serial(port, baudrate, timeout=0.1)
        time.sleep(2)
        self.ser.reset_input_buffer()
        
        # Figure setup
        self.fig, (self.ax1, self.ax2, self.ax3) = plt.subplots(3, 1, figsize=(10, 8))
        
        # Smaller buffer for more responsive feel
        self.max_points = max_points
        self.time_data = deque(maxlen=max_points)
        
        # Sensor data with software filtering
        self.accel_x = deque(maxlen=max_points)
        self.accel_y = deque(maxlen=max_points)
        self.accel_z = deque(maxlen=max_points)
        
        self.gyro_x = deque(maxlen=max_points)
        self.gyro_y = deque(maxlen=max_points)
        self.gyro_z = deque(maxlen=max_points)
        
        self.mag_x = deque(maxlen=max_points)
        self.mag_y = deque(maxlen=max_points)
        self.mag_z = deque(maxlen=max_points)
        
        # For software filtering
        self.filter_window = 3
        self.accel_filter_buf = deque(maxlen=self.filter_window)
        self.gyro_filter_buf = deque(maxlen=self.filter_window)
        self.mag_filter_buf = deque(maxlen=self.filter_window)
        
        self.start_time = time.time()
        self.data_count = 0
        
        self.setup_plots()
        plt.tight_layout()
    
    def setup_plots(self):
        """Setup clean, simple plots"""
        colors = ['red', 'green', 'blue']
        
        # Accelerometer
        self.accel_lines = []
        for i, color in enumerate(colors):
            line, = self.ax1.plot([], [], color=color, label=['X', 'Y', 'Z'][i], linewidth=1.5)
            self.accel_lines.append(line)
        self.ax1.set_title('Accelerometer (g)')
        self.ax1.legend(loc='upper right')
        self.ax1.grid(True, alpha=0.3)
        self.ax1.set_ylim(-2, 2)
        
        # Gyroscope
        self.gyro_lines = []
        for i, color in enumerate(colors):
            line, = self.ax2.plot([], [], color=color, label=['X', 'Y', 'Z'][i], linewidth=1.5)
            self.gyro_lines.append(line)
        self.ax2.set_title('Gyroscope (deg/s)')
        self.ax2.legend(loc='upper right')
        self.ax2.grid(True, alpha=0.3)
        self.ax2.set_ylim(-5, 5)
        
        # Magnetometer
        self.mag_lines = []
        for i, color in enumerate(colors):
            line, = self.ax3.plot([], [], color=color, label=['X', 'Y', 'Z'][i], linewidth=1.5)
            self.mag_lines.append(line)
        self.ax3.set_title('Magnetometer (μT)')
        self.ax3.legend(loc='upper right')
        self.ax3.grid(True, alpha=0.3)
        self.ax3.set_ylim(-100, 100)
        self.ax3.set_xlabel('Time (seconds)')
    
    def simple_filter(self, values):
        """Simple moving average filter in Python"""
        if len(self.accel_filter_buf) < self.filter_window:
            self.accel_filter_buf.append(values[:3])
            self.gyro_filter_buf.append(values[3:6])
            self.mag_filter_buf.append(values[6:9])
            return values
        
        # Calculate averages
        accel_avg = np.mean(list(self.accel_filter_buf), axis=0)
        gyro_avg = np.mean(list(self.gyro_filter_buf), axis=0)
        mag_avg = np.mean(list(self.mag_filter_buf), axis=0)
        
        # Update buffers
        self.accel_filter_buf.append(values[:3])
        self.gyro_filter_buf.append(values[3:6])
        self.mag_filter_buf.append(values[6:9])
        
        return np.concatenate([accel_avg, gyro_avg, mag_avg])
    
    def read_sensor_data(self):
        """Read and filter sensor data"""
        try:
            # Read all available data but use only the latest
            lines = []
            while self.ser.in_waiting > 0:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    lines.append(line)
            
            if lines:
                latest_line = lines[-1]
                
                if ',' in latest_line:
                    values = latest_line.split(',')
                    if len(values) == 9:
                        # Parse data
                        sensor_data = [float(v) for v in values]
                        
                        # Apply software filtering
                        filtered_data = self.simple_filter(sensor_data)
                        
                        current_time = time.time() - self.start_time
                        self.time_data.append(current_time)
                        
                        # Store filtered data
                        self.accel_x.append(filtered_data[0])
                        self.accel_y.append(filtered_data[1])
                        self.accel_z.append(filtered_data[2])
                        
                        self.gyro_x.append(filtered_data[3])
                        self.gyro_y.append(filtered_data[4])
                        self.gyro_z.append(filtered_data[5])
                        
                        self.mag_x.append(filtered_data[6])
                        self.mag_y.append(filtered_data[7])
                        self.mag_z.append(filtered_data[8])
                        
                        self.data_count += 1
                        return True
            
            return False
            
        except Exception as e:
            if self.data_count % 100 == 0:
                print(f"Data error: {e}")
            return False
    
    def update_plot(self, frame):
        """Update plots"""
        self.read_sensor_data()
        
        if len(self.time_data) > 1:
            time_array = list(self.time_data)
            
            # Update all plots
            self.accel_lines[0].set_data(time_array, list(self.accel_x))
            self.accel_lines[1].set_data(time_array, list(self.accel_y))
            self.accel_lines[2].set_data(time_array, list(self.accel_z))
            self.ax1.set_xlim(time_array[0], time_array[-1])
            
            self.gyro_lines[0].set_data(time_array, list(self.gyro_x))
            self.gyro_lines[1].set_data(time_array, list(self.gyro_y))
            self.gyro_lines[2].set_data(time_array, list(self.gyro_z))
            self.ax2.set_xlim(time_array[0], time_array[-1])
            
            self.mag_lines[0].set_data(time_array, list(self.mag_x))
            self.mag_lines[1].set_data(time_array, list(self.mag_y))
            self.mag_lines[2].set_data(time_array, list(self.mag_z))
            self.ax3.set_xlim(time_array[0], time_array[-1])
        
        return (self.accel_lines + self.gyro_lines + self.mag_lines)
    
    def start_visualization(self):
        """Start the visualization"""
        print("Starting SMOOTH sensor visualization...")
        print("Data should be much cleaner now!")
        print("Move the sensor and watch the real-time response")
        
        from matplotlib.animation import FuncAnimation
        ani = FuncAnimation(self.fig, self.update_plot, interval=33, blit=True, cache_frame_data=False)
        plt.show()

# Usage
if __name__ == "__main__":
    visualizer = SmoothSensorVisualizer(port='COM9', baudrate=115200)
    visualizer.start_visualization()