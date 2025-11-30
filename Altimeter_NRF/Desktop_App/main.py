# main.py
import sys
import asyncio
import os
import logging
from PyQt6.QtWidgets import QApplication
from PyQt6.QtCore import QTimer
import qasync

# Add current directory to path to import local modules
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from ui_dashboard import TelemetryDashboard

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('rocket_telemetry.log'),
        logging.StreamHandler(sys.stdout)
    ]
)

class RocketTelemetryApp:
    def __init__(self):
        self.app = None
        self.dashboard = None
        self.loop = None
        
    def setup_application(self):
        """Setup the Qt application and event loop"""
        print("🚀 Starting Rocket Telemetry System - Professional Dashboard...")
        
        # Create Qt Application
        self.app = QApplication(sys.argv)
        self.app.setApplicationName("Rocket Telemetry System")
        self.app.setApplicationVersion("1.0.0")
        
        try:
            # Set up asyncio event loop for Qt
            self.loop = qasync.QEventLoop(self.app)
            asyncio.set_event_loop(self.loop)
            print("✅ Asyncio event loop configured")
        except Exception as e:
            print(f"⚠️  Asyncio setup warning: {e}")
            print("📱 Running in synchronous mode")
            self.loop = None
        
        # Create and show main window
        self.dashboard = TelemetryDashboard()
        self.dashboard.show()
        
        # Setup cleanup on exit
        self.app.aboutToQuit.connect(self.cleanup)
        
        print("✅ Dashboard initialized successfully!")
        print("=" * 60)
        print("🎯 System Ready for Operation")
        print("📊 Features Available:")
        print("   • BLE Device Connection & Management")
        print("   • Real-time Data Visualization")
        print("   • Memory Status Monitoring")
        print("   • Flight Phase Detection")
        print("   • Data Export (CSV, Logs)")
        print("   • Communication Logging")
        print("=" * 60)
        
        return self.app.exec()
    
    def cleanup(self):
        """Cleanup resources before exit"""
        print("\n🔌 Shutting down Rocket Telemetry System...")
        if self.dashboard and hasattr(self.dashboard, 'ble_client'):
            try:
                # Ensure proper disconnection
                if self.loop and self.loop.is_running():
                    async def disconnect():
                        await self.dashboard.ble_client.disconnect()
                    
                    # Run disconnect coroutine
                    if self.dashboard.is_connected:
                        asyncio.ensure_future(disconnect())
            except Exception as e:
                print(f"⚠️  Cleanup warning: {e}")
        
        print("👋 Goodbye! Rocket Telemetry System shut down successfully.")
    
    def run(self):
        """Main application entry point"""
        try:
            if self.loop:
                # Run with asyncio event loop
                with self.loop:
                    return self.loop.run_until_complete(self._async_run())
            else:
                # Run without asyncio
                return self.setup_application()
        except KeyboardInterrupt:
            print("\n🛑 Application interrupted by user")
            return 0
        except Exception as e:
            print(f"💥 Critical error: {e}")
            logging.exception("Application crash")
            return 1
    
    async def _async_run(self):
        """Async version of run method"""
        try:
            return self.setup_application()
        except Exception as e:
            print(f"💥 Async error: {e}")
            return 1

def check_dependencies():
    """Check if all required dependencies are available.
    In a frozen (packaged) app, skip interactive checks to avoid stdin issues.
    """
    # If running as a packaged executable, assume bundled deps and skip checks
    if getattr(sys, 'frozen', False):
        return True

    required_packages = {
        'PyQt6': 'PyQt6',
        'bleak': 'bleak',
        'pandas': 'pandas', 
        'numpy': 'numpy',
        'pyqtgraph': 'pyqtgraph',
        'qasync': 'qasync',
        'pyserial': 'serial'
    }
    
    missing = []
    for package, import_name in required_packages.items():
        try:
            if package == 'pyserial':
                __import__('serial')
            else:
                __import__(import_name)
            print(f"✅ {package} - OK")
        except ImportError:
            missing.append(package)
            print(f"❌ {package} - MISSING")
    
    if missing:
        print(f"\n⚠️  Missing packages: {', '.join(missing)}")
        print("💡 Install with: pip install " + " ".join(missing))
        return False
    return True

def main():
    """Main application entry point"""
    print("🔍 Checking dependencies...")
    
    if not check_dependencies():
        print("\n❌ Some dependencies are missing. Please install them first.")
        response = input("🚀 Attempt to install missing packages? (y/n): ")
        if response.lower() == 'y':
            try:
                import subprocess
                subprocess.check_call([sys.executable, "-m", "pip", "install", 
                                      "PyQt6", "bleak", "pandas", "numpy", 
                                      "pyqtgraph", "qasync", "pyserial"])
                print("✅ Dependencies installed successfully!")
            except Exception as e:
                print(f"❌ Failed to install dependencies: {e}")
                return 1
        else:
            return 1
    
    # Create and run application
    app = RocketTelemetryApp()
    return app.run()

if __name__ == "__main__":
    # Set working directory to script location
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    
    # Run the application
    exit_code = main()
    
    # In a GUI packaged app, do not wait for stdin (no console attached)
    sys.exit(exit_code)
