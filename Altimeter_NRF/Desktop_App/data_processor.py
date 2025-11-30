# data_processor.py - COMPLETE FIX
import pandas as pd
import numpy as np
from datetime import datetime, timedelta
import json
import re

class DataProcessor:
    def __init__(self):
        self.data_buffer = []
        self.flight_data = pd.DataFrame()
        self.extracted_data = pd.DataFrame()
        self.is_exporting = False
        self.export_start_time = None
        self.samples_received = 0
        self.current_export_data = []
        self.last_memory_status = {
            "total_samples": 0,
            "usage_percent": 0,
            "max_capacity": 24576,
            "is_full": False,
            "data_points": 0
        }
        print("🔄 DataProcessor initialized")
        
    def process_raw_data(self, raw_data):
        """Process raw data from Arduino - COMPLETELY FIXED"""
        try:
            print(f"🔍 RAW DATA RECEIVED: '{raw_data}'")
            
            # Handle flash data export format
            if raw_data == 'BEGIN_DATA_EXPORT':
                result = self._start_data_export()
                print("🚀 Started data export")
                return result
                
            elif raw_data == 'END_DATA_EXPORT':
                result = self._end_data_export()
                print("🏁 Ended data export")
                return result
                
            elif raw_data == 'DATA_EXPORT_COMPLETE':
                result = self._handle_export_complete()
                print("✅ Data export complete processed")
                return result
                
            elif self._is_csv_data(raw_data):
                result = self._process_csv_data(raw_data)
                if result:
                    print(f"📥 CSV data processed: {result['device_timestamp']}, {result['altitude']}, {result['acceleration']}")
                return result
                
            # Handle status and memory commands
            elif raw_data.startswith('STATUS:'):
                return self._process_status_data(raw_data)
            elif raw_data.startswith('MEMORY:'):
                return self._process_memory_data(raw_data)
            elif raw_data == 'MEMORY_CLEARED':
                return self._process_memory_cleared()
            elif raw_data.startswith('TELEMETRY:'):
                return self._process_telemetry_data(raw_data)
            elif raw_data.startswith('DEVICE_ALIVE:'):
                return self._process_heartbeat_data(raw_data)
            elif raw_data.startswith('EXPORT_PROGRESS:'):
                return self._process_export_progress(raw_data)
            elif raw_data.startswith('EXPORT_MEMORY:'):
                return self._process_export_memory_info(raw_data)
            elif raw_data == 'TEST_DATA_GENERATED':
                return self._process_test_data_generated(raw_data)
            else:
                return self._process_text_data(raw_data)
                
        except Exception as e:
            print(f"❌ Error processing data '{raw_data}': {e}")
            import traceback
            traceback.print_exc()
            return None

    def _is_csv_data(self, data):
        """Check if data is in CSV format from flash export - tolerant of extra field"""
        # Skip header row and control messages
        if (data.startswith('timestamp,') or 
            data == 'BEGIN_DATA_EXPORT' or 
            data == 'END_DATA_EXPORT' or 
            data == 'DATA_EXPORT_COMPLETE' or
            data.startswith('EXPORT_PROGRESS:') or
            data.startswith('STATUS:') or 
            data.startswith('MEMORY:') or
            data.startswith('TELEMETRY:') or
            data.startswith('DEVICE_ALIVE')):
            return False
            
        # Check for CSV-like format: at least first 3 values numeric
        if ',' in data:
            parts = [p.strip() for p in data.strip().split(',')]
            if len(parts) >= 3:
                try:
                    int(parts[0])      # timestamp (integer)
                    float(parts[1])    # altitude (float)
                    float(parts[2])    # acceleration (float)
                    return True
                except (ValueError, TypeError, IndexError):
                    pass
        return False

    def _process_csv_data(self, data):
        """Process CSV data from flash storage export - tolerant of extra field"""
        try:
            data = data.strip()
            parts = [p.strip() for p in data.split(',')]
            
            if len(parts) >= 3:  # Use first three fields
                timestamp = int(parts[0])
                altitude = float(parts[1])
                acceleration = float(parts[2])
                
                self.samples_received += 1
                
                processed_data = {
                    "timestamp": datetime.now(),
                    "device_timestamp": timestamp,
                    "altitude": altitude,
                    "acceleration": acceleration,
                    "raw_data": data,
                    "data_type": "flash_data"
                }
                
                # ALWAYS store the data - don't rely on export state
                self.current_export_data.append(processed_data)
                
                # Ensure export mode is active when receiving CSV data
                if not self.is_exporting:
                    print(f"⚠️ CSV data received - auto-activating export mode")
                    self.is_exporting = True
                    self.export_start_time = datetime.now()
                
                # Progress indicator every 50 samples
                if self.samples_received % 50 == 0:
                    stored_count = len(self.current_export_data)
                    print(f"📥 Progress: {self.samples_received} CSV samples received, {stored_count} stored")
                
                return processed_data
                
            else:
                print(f"❌ Invalid CSV format (expected at least 3 parts, got {len(parts)}): {data}")
                
        except Exception as e:
            print(f"❌ Error parsing CSV data '{data}': {e}")
            import traceback
            traceback.print_exc()
            
        return None

    def _process_export_progress(self, data):
        """Process export progress messages"""
        print(f"📊 Processing export progress: {data}")
        return {
            "timestamp": datetime.now(),
            "raw_data": data,
            "data_type": "export_progress"
        }
    
    def _process_export_memory_info(self, data):
        """Process export memory info messages"""
        print(f"💾 Processing export memory info: {data}")
        return {
            "timestamp": datetime.now(),
            "raw_data": data,
            "data_type": "export_memory"
        }
    
    def _process_test_data_generated(self, data):
        """Process test data generated confirmation"""
        print(f"🎯 Test data generated successfully")
        return {
            "timestamp": datetime.now(),
            "raw_data": data,
            "data_type": "test_generated"
        }

    def _start_data_export(self):
        """Start new data export session - FIXED"""
        print("🔄 STARTING DATA EXPORT - Clearing previous data")
        
        # Clear all previous data
        self.current_export_data = []
        self.flight_data = pd.DataFrame()
        self.samples_received = 0
        self.is_exporting = True
        self.export_start_time = datetime.now()
        
        print(f"✅ Export started: is_exporting={self.is_exporting}")
        
        return {
            "timestamp": datetime.now(),
            "raw_data": "BEGIN_DATA_EXPORT",
            "data_type": "export_start"
        }

    def _end_data_export(self):
        """Finalize data export when END_DATA_EXPORT is received"""
        print(f"📦 END_DATA_EXPORT received. Samples in buffer: {len(self.current_export_data)}")
        
        return {
            "timestamp": datetime.now(),
            "raw_data": "END_DATA_EXPORT",
            "data_type": "export_end"
        }

    def _handle_export_complete(self):
        """Handle DATA_EXPORT_COMPLETE message - FIXED"""
        print(f"✅ DATA_EXPORT_COMPLETE received. Finalizing {len(self.current_export_data)} samples...")
        self._finalize_data_export()
        
        return {
            "timestamp": datetime.now(),
            "raw_data": "DATA_EXPORT_COMPLETE",
            "data_type": "export_complete"
        }

    def _finalize_data_export(self):
        """Finalize data export and make it the primary dataset - FIXED"""
        print(f"🔚 FINALIZE: is_exporting={self.is_exporting}, samples in buffer: {len(self.current_export_data)}")
        
        # ALWAYS finalize regardless of export state
        self.is_exporting = False
        
        if self.export_start_time:
            export_duration = (datetime.now() - self.export_start_time).total_seconds()
            print(f"⏱️ Export took {export_duration:.1f} seconds")
        
        # Convert collected data to DataFrame - ALWAYS attempt this
        if self.current_export_data:
            print(f"📊 CREATING DataFrame from {len(self.current_export_data)} samples...")
            
            try:
                # Create DataFrame from collected samples
                self.flight_data = pd.DataFrame(self.current_export_data)
                print(f"✅ SUCCESS: DataFrame created with {len(self.flight_data)} rows")
                
                if not self.flight_data.empty:
                    first_sample = self.flight_data.iloc[0]
                    last_sample = self.flight_data.iloc[-1]
                    print(f"📈 DATA RANGE:")
                    print(f"   First: timestamp={first_sample['device_timestamp']}, alt={first_sample['altitude']:.2f}")
                    print(f"   Last:  timestamp={last_sample['device_timestamp']}, alt={last_sample['altitude']:.2f}")
                    
                    # Verify data integrity
                    if 'altitude' in self.flight_data.columns and 'device_timestamp' in self.flight_data.columns:
                        print(f"✅ Data integrity OK - both altitude and timestamp columns present")
                    else:
                        print(f"⚠️ Data integrity issue - missing columns: {list(self.flight_data.columns)}")
                
            except Exception as e:
                print(f"❌ ERROR creating DataFrame: {e}")
                import traceback
                traceback.print_exc()
                # Create empty DataFrame as fallback
                self.flight_data = pd.DataFrame()
                
            # Print summary
            self._print_data_summary()
                
        else:
            print(f"❌ CRITICAL: No data in current_export_data buffer!")
            print(f"📝 Debug info:")
            print(f"   - samples_received counter: {self.samples_received}")
            print(f"   - is_exporting was: {self.is_exporting}")
            print(f"   - export_start_time: {self.export_start_time}")
            
            # Create empty DataFrame
            self.flight_data = pd.DataFrame()

    def _print_data_summary(self):
        """Print summary of extracted data"""
        if self.flight_data.empty:
            print("📭 flight_data DataFrame is empty")
            return
            
        print("📊 EXTRACTED DATA SUMMARY:")
        print(f"   Total samples in DataFrame: {len(self.flight_data)}")
        
        if "device_timestamp" in self.flight_data.columns:
            timestamps = self.flight_data["device_timestamp"]
            if len(timestamps) > 1:
                duration_seconds = (timestamps.max() - timestamps.min()) / 1000.0
                print(f"   Time range: {timestamps.min()} to {timestamps.max()}")
                print(f"   Duration: {duration_seconds:.2f} seconds")
        
        if "altitude" in self.flight_data.columns:
            altitudes = self.flight_data["altitude"]
            print(f"   Altitude range: {altitudes.min():.2f} to {altitudes.max():.2f} m")

    def _process_memory_data(self, data):
        """Process memory status messages"""
        try:
            if data.startswith("MEMORY:"):
                data_str = data[7:]
                parts = data_str.split(",")
                memory_info = {}
                
                for part in parts:
                    if "=" in part:
                        key, value = part.split("=", 1)
                        key = key.strip().lower()
                        memory_info[key] = value
                
                total_samples = self._extract_memory_value(memory_info, ['totalsamples', 'totalsample'], 0)
                usage_percent_str = self._extract_memory_value(memory_info, ['usage', 'use'], '0%')
                max_capacity = self._extract_memory_value(memory_info, ['maxcapacity', 'maxcap', 'capacity'], 24576)
                full_status = self._extract_memory_value(memory_info, ['full', 'isfull'], 'false')
                
                usage_percent = int(usage_percent_str.replace('%', '')) if '%' in usage_percent_str else int(usage_percent_str)
                is_full = full_status.lower() == 'true'
                
                self.last_memory_status = {
                    "total_samples": total_samples,
                    "usage_percent": usage_percent,
                    "max_capacity": max_capacity,
                    "is_full": is_full,
                    "data_points": total_samples
                }
                
                return {
                    "timestamp": datetime.now(),
                    "raw_data": data,
                    "data_type": "memory",
                    "total_samples": total_samples,
                    "usage_percent": usage_percent,
                    "max_capacity": max_capacity,
                    "is_full": is_full
                }
                
        except Exception as e:
            print(f"❌ Error parsing memory status '{data}': {e}")
            
        return {
            "timestamp": datetime.now(),
            "raw_data": data,
            "data_type": "memory",
            "total_samples": 0,
            "usage_percent": 0,
            "max_capacity": 24576,
            "is_full": False
        }

    def _extract_memory_value(self, memory_info, keys, default):
        """Extract value from memory info with multiple possible keys"""
        for key in keys:
            if key in memory_info:
                try:
                    if isinstance(default, int):
                        return int(memory_info[key])
                    else:
                        return memory_info[key]
                except (ValueError, TypeError):
                    return default
        return default

    def _process_memory_cleared(self):
        """Process memory cleared message"""
        print("🗑️ Memory cleared message received")
        
        self.last_memory_status = {
            "total_samples": 0,
            "usage_percent": 0,
            "max_capacity": 24576,
            "is_full": False,
            "data_points": 0
        }
        
        return {
            "timestamp": datetime.now(),
            "raw_data": "MEMORY_CLEARED",
            "data_type": "memory_cleared"
        }

    def _process_status_data(self, data):
        """Process status messages"""
        return {
            "timestamp": datetime.now(),
            "raw_data": data,
            "data_type": "status"
        }

    def _process_telemetry_data(self, data):
        """Process live telemetry data"""
        try:
            data_str = data.replace('TELEMETRY:', '')
            pairs = data_str.split(',')
            processed = {
                "timestamp": datetime.now(),
                "raw_data": data,
                "data_type": "telemetry"
            }
            
            for pair in pairs:
                if '=' in pair:
                    key, value = pair.split('=', 1)
                    key = key.strip().lower()
                    
                    try:
                        if key == 'alt':
                            processed["altitude"] = float(value)
                        elif key == 'acc':
                            processed["acceleration"] = float(value)
                        elif key == 'time':
                            processed["device_time"] = int(value)
                        elif key == 'points':
                            processed["data_points"] = int(value)
                    except ValueError:
                        continue
                        
            return processed
        except Exception as e:
            print(f"❌ Error parsing telemetry: {e}")
            return None

    def _process_heartbeat_data(self, data):
        """Process heartbeat messages"""
        return {
            "timestamp": datetime.now(),
            "raw_data": data,
            "data_type": "heartbeat"
        }

    def _process_text_data(self, text):
        """Process generic text data"""
        return {
            "timestamp": datetime.now(),
            "raw_data": text,
            "data_type": "generic"
        }

    def add_data_point(self, processed_data):
        """Add processed data to appropriate buffer"""
        if not processed_data:
            return
            
        data_type = processed_data.get("data_type")
        
        if data_type == "flash_data":
            # Data is already stored in current_export_data during _process_csv_data
            pass
            
        elif data_type == "telemetry":
            self.data_buffer.append(processed_data)
            if len(self.data_buffer) > 0:
                self.flight_data = pd.DataFrame(self.data_buffer)

    def get_memory_status(self, processed_data=None):
        """Get memory status"""
        if processed_data and processed_data.get("data_type") == "memory":
            self.last_memory_status = {
                "total_samples": processed_data.get("total_samples", 0),
                "usage_percent": processed_data.get("usage_percent", 0),
                "max_capacity": processed_data.get("max_capacity", 24576),
                "is_full": processed_data.get("is_full", False),
                "data_points": processed_data.get("total_samples", 0)
            }
        
        return self.last_memory_status.copy()

    def get_flash_data_stats(self):
        """Get statistics specifically for flash-stored data"""
        if self.flight_data.empty:
            return {}
            
        stats = {}
        
        if "altitude" in self.flight_data.columns:
            stats["max_altitude"] = self.flight_data["altitude"].max()
            stats["min_altitude"] = self.flight_data["altitude"].min()
            stats["avg_altitude"] = self.flight_data["altitude"].mean()
            
        if "acceleration" in self.flight_data.columns:
            stats["max_acceleration"] = self.flight_data["acceleration"].max()
            stats["min_acceleration"] = self.flight_data["acceleration"].min()
            stats["avg_acceleration"] = self.flight_data["acceleration"].mean()
            
        stats["data_points"] = len(self.flight_data)
        
        if "device_timestamp" in self.flight_data.columns:
            timestamps = self.flight_data["device_timestamp"]
            if len(timestamps) > 1:
                stats["duration"] = (timestamps.max() - timestamps.min()) / 1000.0
            else:
                stats["duration"] = 0
        else:
            stats["duration"] = 0
            
        return stats

    def detect_flight_phases(self):
        """Detect different flight phases from flash data"""
        if self.flight_data.empty or "altitude" not in self.flight_data.columns:
            return []
            
        phases = []
        altitudes = self.flight_data["altitude"].values
        
        if len(altitudes) < 2:
            return phases
            
        peak_idx = np.argmax(altitudes)
        peak_altitude = altitudes[peak_idx]
        
        if "device_timestamp" in self.flight_data.columns:
            timestamps = self.flight_data["device_timestamp"].values
            timestamps = (timestamps - timestamps[0]) / 1000.0
            peak_time = timestamps[peak_idx]
        else:
            timestamps = np.arange(len(altitudes))
            peak_time = peak_idx
        
        lift_off_end = max(1, peak_idx // 5)
        phases.append({
            "phase": "Lift-off",
            "start_time": timestamps[0],
            "end_time": timestamps[lift_off_end],
            "start_altitude": altitudes[0],
            "end_altitude": altitudes[lift_off_end]
        })
        
        phases.append({
            "phase": "Ascent",
            "start_time": timestamps[lift_off_end],
            "end_time": peak_time,
            "start_altitude": altitudes[lift_off_end],
            "end_altitude": peak_altitude
        })
        
        phases.append({
            "phase": "Peak",
            "start_time": peak_time,
            "end_time": peak_time,
            "start_altitude": peak_altitude,
            "end_altitude": peak_altitude
        })
        
        if len(altitudes) > peak_idx + 1:
            phases.append({
                "phase": "Descent",
                "start_time": peak_time,
                "end_time": timestamps[-1],
                "start_altitude": peak_altitude,
                "end_altitude": altitudes[-1]
            })
            
        return phases

    def calculate_statistics(self):
        """Calculate flight statistics"""
        return self.get_flash_data_stats()

    def export_to_csv(self, filename):
        """Export flight data to CSV"""
        if not self.flight_data.empty:
            export_data = self.flight_data.copy()
            
            if 'raw_data' in export_data.columns:
                export_data = export_data.drop('raw_data', axis=1)
                
            preferred_order = ['timestamp', 'device_timestamp', 'altitude', 'acceleration']
            existing_columns = [col for col in preferred_order if col in export_data.columns]
            other_columns = [col for col in export_data.columns if col not in preferred_order]
            final_order = existing_columns + other_columns
            
            export_data = export_data[final_order]
                
            export_data.to_csv(filename, index=False)
            print(f"💾 Exported {len(export_data)} samples to {filename}")
            return True
            
        print("❌ No data to export")
        return False

    def clear_data(self):
        """Clear all data"""
        print("🧹 Clearing all data")
        self.data_buffer.clear()
        self.flight_data = pd.DataFrame()
        self.extracted_data = pd.DataFrame()
        self.current_export_data = []
        self.samples_received = 0
        self.is_exporting = False

    def get_flight_data(self):
        """Get current flight data"""
        print(f"📊 GET_FLIGHT_DATA: flight_data has {len(self.flight_data)} rows, export_data has {len(self.current_export_data)} items")
        
        # If flight_data is empty but we have export data, try to create DataFrame
        if self.flight_data.empty and self.current_export_data:
            print(f"⚠️ Flight data empty but export data exists - attempting recovery")
            try:
                self.flight_data = pd.DataFrame(self.current_export_data)
                print(f"✅ Recovery successful: created DataFrame with {len(self.flight_data)} rows")
            except Exception as e:
                print(f"❌ Recovery failed: {e}")
        
        return self.flight_data
    
    def debug_export_data(self):
        """Debug method to check export data buffer"""
        print(f"🔍 EXPORT DATA DEBUG:")
        print(f"   - current_export_data length: {len(self.current_export_data)}")
        print(f"   - samples_received: {self.samples_received}")
        print(f"   - is_exporting: {self.is_exporting}")
        print(f"   - flight_data length: {len(self.flight_data)}")
        
        if self.current_export_data:
            first_item = self.current_export_data[0]
            last_item = self.current_export_data[-1]
            print(f"   - First item: {first_item}")
            print(f"   - Last item: {last_item}")
        else:
            print(f"   - Export data buffer is empty")

    def is_exporting(self):
        """Check if currently exporting data"""
        return self.is_exporting

    def get_export_progress(self):
        """Get current export progress"""
        if self.is_exporting:
            return {
                "samples_received": self.samples_received,
                "export_time": (datetime.now() - self.export_start_time).total_seconds() if self.export_start_time else 0
            }
        return {"samples_received": 0, "export_time": 0}