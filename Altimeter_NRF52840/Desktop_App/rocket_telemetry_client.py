"""
Project: Altimeter_NRF52840 — Desktop BLE Client
File: desktop_app/rocket_telemetry_client_all_in_one.py
Inputs:
  - User input via stdin
  - BLE notifications (requires bleak)
Outputs:
  - Console UI and BLE commands
Notes:
  - Python >= 3.10 recommended
  - If bleak is not installed, the script prompts with install instructions
"""
import asyncio
import sys
import os

# ---- Optional vendored deps path support (desktop_app/vendor) ----
VENDOR_PATH = os.path.join(os.path.dirname(__file__), "vendor")
if os.path.isdir(VENDOR_PATH) and VENDOR_PATH not in sys.path:
    sys.path.insert(0, VENDOR_PATH)

try:
    from bleak import BleakScanner, BleakClient  # type: ignore
    from bleak.exc import BleakError  # type: ignore
except Exception:
    print("⚠️  Missing 'bleak'. Install with one of the following:")
    print("   - Windows PowerShell: desktop_app/vendor_install.ps1 (if available)")
    print("   - Pip target: python -m pip install --target desktop_app/vendor bleak==0.22.2")
    sys.exit(1)

# ==================== CONFIG ====================
DEVICE_NAME = "RocketTelemetry"
SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
DATA_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"
COMMAND_UUID = "19B10002-E8F2-537E-4F6C-D104768A1214"

SCAN_TIMEOUT = 5
CONNECTION_TIMEOUT = 5
RECONNECT_DELAY = 2

MESSAGE_TYPES = {
    "DEVICE_DISCONNECTED": "📡 Device initiated disconnect",
    "MEMORY_CLEAR_STARTED": "🧹 Memory clear process STARTED",
    "MEMORY_CLEAR_PROGRESS": "⏳ Memory clearing...",
    "MEMORY_CLEAR_COMPLETED": "✅ Memory clear COMPLETED",
    "LOG_STARTED": "🔔 Log Started",
    "LOG_STOPPED": "🔔 Log Stopped",
}

# ==================== UI ====================
class UIInterface:
    def display_menu(self):
        print("\n" + "="*50)
        print("🚀 ROCKET TELEMETRY CONTROL PANEL")
        print("="*50)
        print("1 - Start Logging")
        print("2 - Stop Logging")
        print("3 - Get Status")
        print("4 - Clear Memory")
        print("5 - Disconnect")
        print("q - Quit")

    def get_user_choice(self):
        try:
            return input("\nChoose: ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            return 'q'
        except Exception as e:
            print(f"❌ Input error: {e}")
            return ''

    def process_message(self, message: str):
        if message in MESSAGE_TYPES:
            print(f"\n{MESSAGE_TYPES[message]}")
        elif message.startswith("STATUS:"):
            print(f"\n📊 Device Status: {message[7:]}")
        else:
            print(f"\n📡 {message}")

    def confirm_memory_clear(self) -> bool:
        confirm = input("⚠️  Clear ALL memory? (type 'YES' to confirm): ")
        return confirm == 'YES'

    def prompt_retry(self, action: str) -> bool:
        retry = input(f"🔄 Retry {action}? (y/n): ").strip().lower()
        return retry == 'y'

    def show_connection_lost(self):
        print("\n🔌 Connection lost with device")

    def show_reconnect_prompt(self) -> bool:
        print("\n💡 Device disconnected unexpectedly")
        retry = input("🔄 Reconnect? (y/n): ").strip().lower()
        return retry == 'y'

# ==================== CONNECTION ====================
class ConnectionManager:
    def __init__(self, data_callback):
        self.client: BleakClient | None = None
        self.connected = False
        self.connection_lost = False
        self.data_callback = data_callback

    async def discover_device(self):
        print("🔍 Scanning for device...")
        devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT)
        for device in devices:
            try:
                name = device.name or ""
            except Exception:
                name = ""
            if name and DEVICE_NAME in name:
                print(f"✅ Found: {name} - {device.address}")
                return device
        print("❌ Device not found")
        return None

    async def connect(self, device):
        try:
            self.client = BleakClient(device.address)
            await self.client.connect()
            self.connected = True
            self.connection_lost = False
            await self.client.start_notify(DATA_UUID, self._data_received)
            print("✅ Connected & ready!")
            return True
        except Exception as e:
            print(f"❌ Connection failed: {e}")
            return False

    def _data_received(self, sender, data: bytes):
        try:
            message = data.decode('utf-8').strip()
            self.data_callback(message)
        except Exception as e:
            print(f"❌ Error processing message: {e}")

    async def is_still_connected(self) -> bool:
        if self.client:
            return self.client.is_connected
        return False

    async def disconnect(self):
        if self.client and self.connected:
            try:
                await self.client.disconnect()
            except Exception:
                pass
        self.connected = False
        self.connection_lost = False
        print("🔌 Disconnected")

# ==================== COMMANDS ====================
class CommandHandler:
    def __init__(self, connection_manager: ConnectionManager):
        self.connection_manager = connection_manager

    async def send_command(self, command: str):
        if not self.connection_manager.connected or self.connection_manager.connection_lost:
            print("❌ Not connected to device")
            return False
        try:
            await self.connection_manager.client.write_gatt_char(COMMAND_UUID, command.encode('utf-8'))
            print(f"📤 Sent: {command}")
            return True
        except BleakError as e:
            print(f"❌ Send failed: {e}")
            self.connection_manager.connection_lost = True
            return False
        except Exception as e:
            print(f"❌ Unexpected error: {e}")
            return False

    async def start_logging(self):
        return await self.send_command("START_LOG")

    async def stop_logging(self):
        return await self.send_command("STOP_LOG")

    async def get_status(self):
        return await self.send_command("STATUS")

    async def clear_memory(self):
        return await self.send_command("CLEAR_MEMORY")

# ==================== APP ====================
class RocketTelemetryClient:
    def __init__(self):
        self.ui = UIInterface()
        self.connection_manager = ConnectionManager(self.ui.process_message)
        self.command_handler = CommandHandler(self.connection_manager)

    async def run(self):
        while True:
            device = await self.connection_manager.discover_device()
            if not device:
                if not self.ui.prompt_retry("scan"):
                    break
                continue

            if not await self.connection_manager.connect(device):
                if not self.ui.prompt_retry("connection"):
                    break
                continue

            await self._command_loop()

            if self.connection_manager.connection_lost:
                if not self.ui.show_reconnect_prompt():
                    break
            else:
                break

    async def _command_loop(self):
        while True:
            is_connected = await self.connection_manager.is_still_connected()
            if not is_connected:
                self.connection_manager.connection_lost = True
                self.connection_manager.connected = False
                self.ui.show_connection_lost()
                break

            self.ui.display_menu()
            choice = self.ui.get_user_choice()

            if choice == '5' or choice == 'q':
                await self.connection_manager.disconnect()
                break

            await self._handle_command(choice)
            await asyncio.sleep(0.1)

    async def _handle_command(self, choice: str):
        if choice == '1':
            await self.command_handler.start_logging()
        elif choice == '2':
            await self.command_handler.stop_logging()
        elif choice == '3':
            await self.command_handler.get_status()
        elif choice == '4':
            if self.ui.confirm_memory_clear():
                await self.command_handler.clear_memory()
            else:
                print("❌ Cancelled")
        elif choice and choice.strip():
            print("❌ Invalid choice")

# ==================== MAIN ====================
async def main():
    client = RocketTelemetryClient()
    await client.run()

if __name__ == "__main__":
    print("🚀 Starting Rocket Telemetry Client (All-in-One)...")
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n👋 Goodbye!")
    except Exception as e:
        print(f"💥 Unexpected error: {e}")
