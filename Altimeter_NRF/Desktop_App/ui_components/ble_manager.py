# ui_components/ble_manager.py - FIX ASYNC ISSUE
import asyncio
import threading
from PyQt6.QtCore import QThread, pyqtSignal, pyqtSlot
from bleak import BleakScanner, BleakClient
from bleak.exc import BleakError
from config import *

class BLEManager(QThread):
    devices_found = pyqtSignal(list)
    scan_error = pyqtSignal(str)
    connection_status = pyqtSignal(str, str)
    data_received = pyqtSignal(str)
    
    def __init__(self):
        super().__init__()
        self.client = None
        self.scanning = False
        self.connected = False
        self._stop_event = threading.Event()
        self.loop = None
        self.current_address = None
        self.last_heartbeat = None
        
    def scan_devices(self):
        self.scanning = True
        
    def stop_scan(self):
        self.scanning = False
        
    async def _scan_devices_async(self):
        try:
            print("🔍 Starting BLE scan...")
            devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT)
            device_list = []
            
            for device in devices:
                device_name = device.name or "Unknown"
                if "RocketTelemetry" in device_name:
                    rssi = getattr(device, 'rssi', 'N/A')
                    device_info = {
                        "name": device_name,
                        "address": device.address,
                        "rssi": rssi
                    }
                    device_list.append(device_info)
                    print(f"🎯 Found target device: {device_name} - {device.address}")
                        
            self.devices_found.emit(device_list)
            if not device_list:
                print("❌ No RocketTelemetry devices found")
            
        except Exception as e:
            error_msg = f"Scan failed: {e}"
            print(error_msg)
            self.scan_error.emit(error_msg)
        finally:
            self.scanning = False
            
    async def _connect_device_async(self, address):
        try:
            print(f"🔗 Connecting to {address}...")
            self.client = BleakClient(address)
            await self.client.connect(timeout=CONNECTION_TIMEOUT)
            self.connected = True
            self.current_address = address
            self.last_heartbeat = asyncio.get_event_loop().time()
            
            print("✅ Connected! Setting up notifications...")
            
            # Enable notifications for data characteristic (must support notify)
            try:
                await self.client.start_notify(DATA_UUID, self._handle_data_received)
                print("✅ Notifications enabled for data characteristic")
            except Exception as e:
                # If the data characteristic cannot notify, treat as fatal
                self.connected = False
                await self.client.disconnect()
                raise BleakError(f"DATA_UUID notify failed: {e}")
            
            # Try enabling notifications for command characteristic IF supported; ignore failures
            try:
                await self.client.start_notify(COMMAND_UUID, self._handle_data_received)
                print("ℹ️ Notifications enabled for command characteristic")
            except Exception as e:
                print(f"ℹ️ COMMAND_UUID does not support notify/indicate (proceeding): {e}")
            
            success_msg = f"Connected to {address}"
            print(f"✅ {success_msg}")
            self.connection_status.emit("connected", success_msg)
            return True
            
        except Exception as e:
            error_msg = f"Connection failed: {e}"
            print(f"❌ {error_msg}")
            self.connection_status.emit("error", error_msg)
            return False
            
    def _handle_data_received(self, sender, data):
        try:
            message = data.decode('utf-8').strip()
            
            # DEBUG: Print EVERYTHING received
            print(f"🎯 BLE RECEIVED from {sender}: '{message}'")
            
            # Update heartbeat when we receive any data
            if self.loop and self.loop.is_running():
                self.last_heartbeat = asyncio.get_event_loop().time()
            
            # Emit to main application
            self.data_received.emit(message)
            
        except Exception as e:
            print(f"❌ Data receive error: {e}")
            
    async def _disconnect_async(self):
        """Disconnect from device - FIXED ASYNC"""
        if self.client:
            try:
                if self.connected:
                    await self.client.stop_notify(DATA_UUID)
                    await self.client.stop_notify(COMMAND_UUID)
                await self.client.disconnect()
                print("🔌 Disconnected from device")
            except Exception as e:
                print(f"⚠️ Disconnect warning: {e}")
                
        self.connected = False
        self.current_address = None
        self.last_heartbeat = None
        self.connection_status.emit("disconnected", "Disconnected from device")
        
    async def _send_command_async(self, command):
        if not self.client:
            print("❌ No BLE client available")
            self.connection_status.emit("error", "No BLE client")
            return False
            
        if not self.client.is_connected:
            print("❌ Device not connected")
            self.connected = False
            self.connection_status.emit("disconnected", "Device not connected")
            return False
        
        payloads = [command + '\r\n', command + '\n']
        responses = [True, False]
        last_err = None
        for p in payloads:
            for resp in responses:
                try:
                    await self.client.write_gatt_char(COMMAND_UUID, p.encode('utf-8'), response=resp)
                    print(f"📤 Sent command: '{command}' (resp={resp}, len={len(p)})")
                    return True
                except Exception as e:
                    last_err = e
                    continue
        
        error_msg = f"Send failed after retries: {last_err}"
        print(f"❌ {error_msg}")
        self.connection_status.emit("error", error_msg)
        return False
            
    def run(self):
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)
        
        async def main_loop():
            while not self._stop_event.is_set():
                if self.scanning:
                    await self._scan_devices_async()
                    self.scanning = False
                    
                # Check connection status
                if self.client and self.connected:
                    try:
                        current_time = asyncio.get_event_loop().time()
                        
                        if self.last_heartbeat and (current_time - self.last_heartbeat) > 10:
                            print("⚠️ No data received for 10 seconds")
                            if not self.client.is_connected:
                                print("🔌 Connection lost")
                                self.connected = False
                                self.connection_status.emit("disconnected", "Connection lost")
                        
                        elif not self.client.is_connected:
                            print("🔌 Connection lost (direct check)")
                            self.connected = False
                            self.connection_status.emit("disconnected", "Device disconnected")
                            
                    except Exception as e:
                        print(f"⚠️ Connection check error: {e}")
                
                await asyncio.sleep(2)
                
        try:
            self.loop.run_until_complete(main_loop())
        except Exception as e:
            print(f"❌ BLE manager error: {e}")
        finally:
            # Properly close the loop
            if not self.loop.is_closed():
                self.loop.close()
            
    def connect_to_device(self, address):
        if self.loop and not self.loop.is_closed():
            asyncio.run_coroutine_threadsafe(self._connect_device_async(address), self.loop)
        
    def disconnect_device(self):
        """Disconnect device - FIXED to properly handle async"""
        if self.loop and not self.loop.is_closed():
            # Use run_coroutine_threadsafe and store the future
            future = asyncio.run_coroutine_threadsafe(self._disconnect_async(), self.loop)
            # Wait for the disconnect to complete with timeout
            try:
                future.result(timeout=5.0)  # 5 second timeout
            except Exception as e:
                print(f"⚠️ Disconnect timeout or error: {e}")
        
    def send_command(self, command):
        if self.loop and not self.loop.is_closed():
            asyncio.run_coroutine_threadsafe(self._send_command_async(command), self.loop)
        else:
            print("❌ BLE loop not available")
        
    def is_connected(self):
        return self.connected
        
    def stop(self):
        """Stop the BLE manager - FIXED"""
        self._stop_event.set()
        # Disconnect before stopping
        self.disconnect_device()
        if self.loop and self.loop.is_running():
            self.loop.call_soon_threadsafe(self.loop.stop)