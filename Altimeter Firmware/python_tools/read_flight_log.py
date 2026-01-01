import time
import serial
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
from matplotlib.widgets import Button
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

# Initial setting: start with gravity removed from acceleration plot?
REMOVE_GRAVITY_DEFAULT = True
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
    - Apply a simple first-sample altitude sanity check to drop an obvious
      startup outlier like 1600 m followed by ~170 m.
    """
    csv_text = "\n".join(lines)
    df = pd.read_csv(StringIO(csv_text))

    # Coerce numeric columns and drop bad rows (e.g. 'ovf')
    for col in ["time_s", "alt_m", "ax_ms2", "ay_ms2", "az_ms2"]:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    df = df.dropna(subset=["time_s", "alt_m", "ax_ms2", "ay_ms2", "az_ms2"]).reset_index(drop=True)

    # --- First-sample altitude sanity filter ---
    # If the first altitude is very high (e.g. > 1000 m) but the second sample
    # is near a more realistic ground value (e.g. < 500 m), treat the first
    # row as a startup outlier and drop it.
    if len(df) >= 2:
        first_alt = df.loc[0, "alt_m"]
        second_alt = df.loc[1, "alt_m"]

        if first_alt > 1000.0 and second_alt < 500.0:
            df = df.iloc[1:].reset_index(drop=True)

    return df


def save_files(df: pd.DataFrame) -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    csv_path = OUTPUT_DIR / CSV_NAME
    xlsx_path = OUTPUT_DIR / XLSX_NAME

    df.to_csv(csv_path, index=False)
    df.to_excel(xlsx_path, index=False)
    print(f"Saved CSV to {csv_path}")
    print(f"Saved Excel to {xlsx_path}")


def plot_data(df: pd.DataFrame, remove_gravity: bool = False) -> None:
    if df.empty:
        print("No data to plot.")
        return

    # Use a simple closure to allow toggling gravity in-place via a button.
    state = {"remove_gravity": remove_gravity}

    def compute_plots(remove_g: bool):
        """Compute all derived series for the given gravity setting."""
        # Raw columns (captured from outer scope)
        t   = df["time_s"]
        alt = df["alt_m"]
        ax  = df["ax_ms2"]
        ay  = df["ay_ms2"]
        az  = df["az_ms2"]

        # Baseline altitude and relative altitude
        early_mask = (t <= 1.0)
        if early_mask.sum() < 5:
            early_mask = df.index < min(len(df), 50)
        alt_baseline = alt[early_mask].median()
        alt_rel = alt - alt_baseline

        # Acceleration magnitude
        acc_mag = (ax**2 + ay**2 + az**2) ** 0.5

        if remove_g:
            g_mask = (t <= 1.0)
            if g_mask.sum() < 5:
                g_mask = df.index < min(len(df), 50)
            g0 = acc_mag[g_mask].median()
            acc_plot = acc_mag - g0
        else:
            acc_plot = acc_mag

        # Smoothing window (~0.5 s at 50 Hz)
        window = 25
        alt_s      = alt_rel.rolling(window=window, center=True).mean()
        acc_plot_s = acc_plot.rolling(window=window, center=True).mean()

        # Velocity from smoothed altitude
        dt   = t.diff()
        dalt = alt_s.diff()
        vel_from_alt = dalt / dt
        vel_from_alt[(dt <= 0) | (vel_from_alt.abs() > 500)] = pd.NA
        vel_from_alt = vel_from_alt.fillna(0.0)
        vel_s = vel_from_alt.rolling(window=window, center=True).mean()

        return t, alt_rel, alt_s, vel_from_alt, vel_s, acc_plot, acc_plot_s

    # Initial compute
    t, alt_rel, alt_s, vel_from_alt, vel_s, acc_plot, acc_plot_s = compute_plots(state["remove_gravity"])

    # (raw columns and computation moved into compute_plots)

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

    # Auto-zoom altitude so large spikes (e.g. a single bad sample) don't flatten the curve.
    finite_alt = alt_rel.replace([pd.NA, pd.NaT], pd.NA).astype(float)
    finite_alt = finite_alt[finite_alt.notna() & finite_alt.abs().lt(1e6)]
    if not finite_alt.empty:
        low_q, high_q = finite_alt.quantile([0.01, 0.99])
        # Add a small margin around the central altitude band.
        margin = max(0.5, 0.1 * (high_q - low_q))
        ax_alt.set_ylim(low_q - margin, high_q + margin)

    # Velocity subplot: from altitude derivative
    ax_vel.plot(t, vel_from_alt, color="tab:green", alpha=0.3, linewidth=0.8, label="vel raw")
    ax_vel.plot(t, vel_s,        color="tab:green", linewidth=1.8, label="vel smooth")
    ax_vel.set_ylabel("Velocity (m/s)")
    ax_vel.grid(True, which="both", linestyle="--", alpha=0.4)
    ax_vel.legend(loc="best")

    # Acceleration subplot: magnitude (optionally with gravity removed)
    if remove_gravity:
        acc_label = "|acc| - g (m/s²)"
        raw_label = "|acc| net raw"
        smooth_label = "|acc| net smooth"
    else:
        acc_label = "|acc| (m/s²)"
        raw_label = "|acc| raw"
        smooth_label = "|acc| smooth"

    ax_acc.plot(t, acc_plot,  color="tab:red", alpha=0.3, linewidth=0.8, label=raw_label)
    ax_acc.plot(t, acc_plot_s, color="tab:red", linewidth=1.8, label=smooth_label)
    ax_acc.set_ylabel(acc_label)
    ax_acc.set_xlabel("Time (s)")
    ax_acc.grid(True, which="both", linestyle="--", alpha=0.4)
    ax_acc.legend(loc="best")

    # Add a button below the plots to toggle gravity on/off in the accel subplot.
    fig.tight_layout(rect=[0, 0.08, 1, 1])
    ax_button = plt.axes([0.4, 0.01, 0.2, 0.05])  # [left, bottom, width, height] in figure coords
    label = "Acc: net (no g)" if state["remove_gravity"] else "Acc: total (with g)"
    button = Button(ax_button, label)

    def on_toggle(event):
        # Flip state
        state["remove_gravity"] = not state["remove_gravity"]
        # Recompute data
        nonlocal t, alt_rel, alt_s, vel_from_alt, vel_s, acc_plot, acc_plot_s
        t, alt_rel, alt_s, vel_from_alt, vel_s, acc_plot, acc_plot_s = compute_plots(state["remove_gravity"])

        # Update altitude
        ax_alt.lines[0].set_ydata(alt_rel)
        ax_alt.lines[1].set_ydata(alt_s)

        # Update velocity
        ax_vel.lines[0].set_ydata(vel_from_alt)
        ax_vel.lines[1].set_ydata(vel_s)

        # Update acceleration
        if state["remove_gravity"]:
            ax_acc.set_ylabel("|acc| - g (m/s²)")
            button.label.set_text("Acc: net (no g)")
        else:
            ax_acc.set_ylabel("|acc| (m/s²)")
            button.label.set_text("Acc: total (with g)")

        ax_acc.lines[0].set_ydata(acc_plot)
        ax_acc.lines[1].set_ydata(acc_plot_s)

        fig.canvas.draw_idle()

    button.on_clicked(on_toggle)

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
    plot_data(df, remove_gravity=REMOVE_GRAVITY_DEFAULT)


if __name__ == "__main__":
    main()