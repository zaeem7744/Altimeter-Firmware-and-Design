import time
import serial
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
from pathlib import Path
from io import StringIO

# ---------------------- CONFIG ----------------------
# Set this to your Nano 33 BLE COM port
SERIAL_PORT = "COM9"  # TODO: change to your actual port, e.g. "COM4"
BAUD_RATE = 115200
TIMEOUT = 1.0  # seconds

# Where to save files (relative to this script's directory)
OUTPUT_DIR = Path(__file__).resolve().parent / "flight_logs"
CSV_NAME = "flight_log.csv"
XLSX_NAME = "flight_log.xlsx"
# ----------------------------------------------------


def open_serial(port: str, baud: int, timeout: float) -> serial.Serial:
    ser = serial.Serial(port, baudrate=baud, timeout=timeout)
    time.sleep(2.0)  # give board time after opening port
    return ser


def send_command(ser: serial.Serial, cmd: str) -> None:
    """Send a simple command (like 'A', 'B', 'C', 'D', 'S')."""
    cmd = cmd.strip()
    if len(cmd) != 1:
        raise ValueError("Firmware expects single-character commands like 'A','B','C','D','S'.")
    line = (cmd + "\n").encode("utf-8")
    ser.write(line)


def read_dump(ser: serial.Serial) -> list[str]:
    """Read CSV dump from device after sending DUMP command."""
    lines: list[str] = []
    header_seen = False
    start_time = time.time()

    while True:
        raw = ser.readline()
        if not raw:
            if time.time() - start_time > 10.0:
                print("Timeout waiting for dump data.")
                break
            continue

        text = raw.decode("utf-8", errors="replace").strip()
        print(text)  # echo for debugging

        if not header_seen:
            if text.startswith("time_s,"):
                header_seen = True
                lines.append(text)
            continue

        if text.startswith("=== END FLASH DUMP"):
            break

        lines.append(text)

    return lines


def parse_to_dataframe(lines: list[str]) -> pd.DataFrame:
    """Parse CSV lines into a cleaned DataFrame.

    New firmware format:
    time_s,alt_m,ax_ms2,ay_ms2,az_ms2

    - Coerce non-numeric values like 'ovf' to NaN
    - Drop rows with NaNs in any of the numeric columns
    """
    csv_text = "\n".join(lines)
    df = pd.read_csv(StringIO(csv_text))

    # Coerce numeric columns and drop bad rows (e.g. 'ovf')
    for col in ["time_s", "alt_m", "ax_ms2", "ay_ms2", "az_ms2"]:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    df = df.dropna(subset=["time_s", "alt_m", "ax_ms2", "ay_ms2", "az_ms2"]).reset_index(drop=True)

    return df


def save_files(df: pd.DataFrame) -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    csv_path = OUTPUT_DIR / CSV_NAME
    xlsx_path = OUTPUT_DIR / XLSX_NAME

    df.to_csv(csv_path, index=False)
    df.to_excel(xlsx_path, index=False)
    print(f"Saved CSV to {csv_path}")
    print(f"Saved Excel to {xlsx_path}")


def plot_data(df: pd.DataFrame) -> None:
    if df.empty:
        print("No data to plot.")
        return

    # Raw columns
    t   = df["time_s"]
    alt = df["alt_m"]
    ax  = df["ax_ms2"]
    ay  = df["ay_ms2"]
    az  = df["az_ms2"]

    # Choose a more robust baseline altitude than a single first sample.
    # Use median altitude over the first second (or first 50 samples) to avoid startup glitches.
    early_mask = (t <= 1.0)
    if early_mask.sum() < 5:
        # Fallback: use first 50 samples or entire series if shorter
        early_mask = df.index < min(len(df), 50)
    alt_baseline = alt[early_mask].median()

    # Relative altitude from launch point (approximate ground = baseline)
    alt_rel = alt - alt_baseline

    # Acceleration magnitude (includes gravity for now; we can remove it offline later)
    acc_mag = (ax**2 + ay**2 + az**2) ** 0.5

    # --- Smoothing ---
    # From earlier logs, rate is ~50 Hz (dt ≈ 0.02 s),
    # so a window of 25 samples ≈ 0.5 s is a good low-pass filter.
    window = 25

    alt_s    = alt_rel.rolling(window=window, center=True).mean()
    acc_mag_s = acc_mag.rolling(window=window, center=True).mean()

    # Velocity estimate from altitude (differentiate smoothed altitude)
    dt = t.diff()
    dalt = alt_s.diff()

    vel_from_alt = dalt / dt
    # Remove obviously crazy spikes due to any remaining glitches
    vel_from_alt[(dt <= 0) | (vel_from_alt.abs() > 500)] = pd.NA
    vel_from_alt = vel_from_alt.fillna(0.0)
    vel_s = vel_from_alt.rolling(window=window, center=True).mean()

    # Use a clearer style
    plt.style.use("seaborn-v0_8-darkgrid")

    # Three stacked subplots sharing the same time axis
    fig, axes = plt.subplots(3, 1, figsize=(10, 8), sharex=True)
    ax_alt, ax_vel, ax_acc = axes

    # Limit number of y-ticks on each subplot for readability
    for ax_plot in axes:
        ax_plot.yaxis.set_major_locator(MaxNLocator(nbins=6))

    # Altitude subplot: relative altitude (raw + smooth)
    ax_alt.plot(t, alt_rel, color="tab:blue", alpha=0.3, linewidth=0.8, label="alt rel raw")
    ax_alt.plot(t, alt_s,   color="tab:blue", linewidth=1.8, label="alt rel smooth")
    ax_alt.set_ylabel("Altitude rel (m)")
    ax_alt.set_title("Flight Profile (from barometric altitude)")
    ax_alt.grid(True, which="both", linestyle="--", alpha=0.4)
    ax_alt.legend(loc="best")

    # Velocity subplot: from altitude derivative
    ax_vel.plot(t, vel_from_alt, color="tab:green", alpha=0.3, linewidth=0.8, label="vel raw")
    ax_vel.plot(t, vel_s,        color="tab:green", linewidth=1.8, label="vel smooth")
    ax_vel.set_ylabel("Velocity (m/s)")
    ax_vel.grid(True, which="both", linestyle="--", alpha=0.4)
    ax_vel.legend(loc="best")

    # Acceleration subplot: magnitude of 3-axis accel
    ax_acc.plot(t, acc_mag,  color="tab:red", alpha=0.3, linewidth=0.8, label="|acc| raw")
    ax_acc.plot(t, acc_mag_s, color="tab:red", linewidth=1.8, label="|acc| smooth")
    ax_acc.set_ylabel("|acc| (m/s²)")
    ax_acc.set_xlabel("Time (s)")
    ax_acc.grid(True, which="both", linestyle="--", alpha=0.4)
    ax_acc.legend(loc="best")

    fig.tight_layout()
    plt.show()


def main() -> None:
    print(f"Opening serial on {SERIAL_PORT} @ {BAUD_RATE}...")
    with open_serial(SERIAL_PORT, BAUD_RATE, TIMEOUT) as ser:
        # Flush any initial junk from the port
        ser.reset_input_buffer()

        print("Sending D command (flash dump)...")
        send_command(ser, "D")

        print("Reading dump from device...")
        lines = read_dump(ser)
        if not lines:
            print("No data captured.")
            return

        df = parse_to_dataframe(lines)
        print(df.head())

    save_files(df)
    plot_data(df)


if __name__ == "__main__":
    main()