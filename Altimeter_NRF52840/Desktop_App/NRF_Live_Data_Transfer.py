import asyncio
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from bleak import BleakClient, BleakScanner
import struct
from collections import deque
import threading

# Bluetooth configuration
DEVICE_NAME = "NanoGyro"
SERVICE_UUID = "12345678-1234-1234-1234-123456789ABC"
GYRO_CHARACTERISTIC_UUID = "12345678-1234-1234-1234-123456789ABD"

class GyroVisualizer:
    def __init__(self, max_points=200):
        self.max_points = max_points
        self.timestamps = deque(maxlen=max_points)
        self.gyro_x = deque(maxlen=max_points)
        self.gyro_y = deque(maxlen=max_points)
        self.gyro_z = deque(maxlen=max_points)
        
        # Setup plot
        self.fig, (self.ax1, self.ax2) = plt.subplots(2, 1, figsize=(12, 8))
        self.setup_plots()
        
        self.connected = False
        self.data_received = False
        
    def setup_plots(self):
        # Time series plot
        self.ax1.set_title('Real-time Gyroscope Data - Waiting for connection...')
        self.ax1.set_ylabel('Angular Velocity (rad/s)')
        self.ax1.set_xlabel('Time (s)')
        self.ax1.grid(True)
        
        self.line_x, = self.ax1.plot([], [], 'r-', label='Gyro X', linewidth=2)
        self.line_y, = self.ax1.plot([], [], 'g-', label='Gyro Y', linewidth=2)
        self.line_z, = self.ax1.plot([], [], 'b-', label='Gyro Z', linewidth=2)
        self.ax1.legend(loc='upper right')
        
        # 3D vector plot
        self.ax2.set_title('Current Gyroscope Vector')
        self.ax2.set_xlabel('X axis')
        self.ax2.set_ylabel('Y axis')
        self.ax2.grid(True)
        self.ax2.set_xlim(-2, 2)
        self.ax2.set_ylim(-2, 2)
        
        # Add crosshairs
        self.ax2.axhline(y=0, color='k', linestyle='-', alpha=0.3)
        self.ax2.axvline(x=0, color='k', linestyle='-', alpha=0.3)
        
        self.quiver = self.ax2.quiver(0, 0, 0, 0, scale=1, color='red', 
                                     width=0.02, scale_units='xy', angles='xy')
        self.scatter = self.ax2.scatter([0], [0], c=['red'], s=100, alpha=0.7)
        
        plt.tight_layout()
    
    def gyro_data_handler(self, sender, data):
        """Handle incoming gyroscope data"""
        try:
            if len(data) == 8:  # 6 bytes gyro + 2 bytes timestamp
                # Unpack the data: 3 int16 for gyro + 1 uint16 for timestamp
                gx, gy, gz = struct.unpack('<hhh', data[:6])
                timestamp = struct.unpack('<H', data[6:8])[0]
                
                # Convert back to float (divide by 100 as in Arduino code)
                gx_float = gx / 100.0
                gy_float = gy / 100.0
                gz_float = gz / 100.0
                
                # Add to data buffers
                current_time = len(self.timestamps) * 0.05  # 20Hz
                self.timestamps.append(current_time)
                self.gyro_x.append(gx_float)
                self.gyro_y.append(gy_float)
                self.gyro_z.append(gz_float)
                
                self.data_received = True
                
                # Print current values
                print(f"Gyro - X: {gx_float:7.3f}, Y: {gy_float:7.3f}, Z: {gz_float:7.3f} rad/s")
                
        except Exception as e:
            print(f"Error parsing data: {e}")
            print(f"Data length: {len(data)}, Data: {data.hex()}")
    
    def update_plot(self, frame):
        """Update the matplotlib plots"""
        if self.data_received and len(self.timestamps) > 0:
            # Update time series
            self.line_x.set_data(self.timestamps, self.gyro_x)
            self.line_y.set_data(self.timestamps, self.gyro_y)
            self.line_z.set_data(self.timestamps, self.gyro_z)
            
            # Auto-scale time series
            self.ax1.relim()
            self.ax1.autoscale_view()
            self.ax1.set_title('Real-time Gyroscope Data - LIVE')
            
            # Update 3D vector with current values
            current_x = self.gyro_x[-1] if self.gyro_x else 0
            current_y = self.gyro_y[-1] if self.gyro_y else 0
            current_z = self.gyro_z[-1] if self.gyro_z else 0
            
            # Update quiver plot
            self.quiver.set_UVC(current_x, current_y)
            
            # Update scatter point with color based on Z value
            colors = np.array([current_z])
            self.scatter.set_offsets([[current_x, current_y]])
            self.scatter.set_array(colors)
            self.scatter.set_clim(-2, 2)  # Color limits
            
            # Update title with magnitude
            magnitude = np.sqrt(current_x**2 + current_y**2 + current_z**2)
            self.ax2.set_title(f'Current Gyro Vector - Magnitude: {magnitude:.3f}')
        
        return self.line_x, self.line_y, self.line_z, self.quiver, self.scatter
    
    async def connect_and_listen(self):
        """Connect to BLE device and start listening"""
        print("Searching for BLE device...")
        
        devices = await BleakScanner.discover()
        target_device = None
        
        for device in devices:
            print(f"Found: {device.name} - {device.address}")
            if device.name and DEVICE_NAME.lower() in device.name.lower():
                target_device = device
                print(f"✓ Target device found: {device.name}")
                break
        
        if not target_device:
            print(f"✗ Device '{DEVICE_NAME}' not found!")
            return
        
        def connection_handler(client):
            self.connected = client.is_connected
            status = "Connected" if self.connected else "Disconnected"
            print(f"Connection status: {status}")
        
        try:
            async with BleakClient(target_device.address, disconnected_callback=connection_handler) as client:
                self.connected = True
                print("Connected to device!")
                
                # Get services (new Bleak API)
                services = client.services
                print("Available services:")
                for service in services:
                    print(f"  Service: {service.uuid}")
                    for char in service.characteristics:
                        print(f"    Characteristic: {char.uuid}")
                
                # Start notifications
                await client.start_notify(GYRO_CHARACTERISTIC_UUID, self.gyro_data_handler)
                print("Started listening for gyroscope data...")
                print("Move the device to see data in the graphs!")
                print("Press Ctrl+C in the console to stop")
                
                # Keep running until interrupted
                while self.connected:
                    await asyncio.sleep(1)
                    
        except Exception as e:
            print(f"Connection error: {e}")
        finally:
            self.connected = False
            print("Disconnected")

def main():
    visualizer = GyroVisualizer()
    
    # Create animation
    ani = FuncAnimation(visualizer.fig, visualizer.update_plot, 
                       interval=50, blit=False, cache_frame_data=False)
    
    # Start Bluetooth connection and visualization
    try:
        # Run BLE connection in a separate thread
        def run_ble():
            asyncio.run(visualizer.connect_and_listen())
        
        ble_thread = threading.Thread(target=run_ble, daemon=True)
        ble_thread.start()
        
        # Show plot (this blocks until window is closed)
        print("Opening visualization window...")
        plt.show()
        
    except KeyboardInterrupt:
        print("\nExiting...")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()