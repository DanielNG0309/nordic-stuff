import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.patches import Circle
import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import queue
import time
import itertools
import random
import re
import json
import os
import csv
from scipy.linalg import block_diag
import seaborn as sns
from matplotlib.widgets import Button

# Set up modern, professional plotting style
plt.style.use("seaborn-v0_8")  # Modern, clean aesthetic
sns.set_palette("colorblind")  # Accessible color palette

# Configure matplotlib for better typography and appearance
plt.rcParams.update(
    {
        "font.family": "DejaVu Sans",
        "font.size": 10,
        "axes.titlesize": 12,
        "axes.labelsize": 10,
        "xtick.labelsize": 9,
        "ytick.labelsize": 9,
        "legend.fontsize": 9,
        "figure.titlesize": 14,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "axes.grid": True,
        "grid.alpha": 0.3,
        "grid.linewidth": 0.8,
        "lines.linewidth": 2,
        "lines.markersize": 8,
        "axes.linewidth": 1.2,
        "xtick.major.size": 5,
        "ytick.major.size": 5,
        "xtick.minor.size": 3,
        "ytick.minor.size": 3,
    }
)


class ConfigurationScreen:
    """Startup configuration screen for selecting mode and parameters"""

    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Multilateration Configuration")
        self.root.geometry("1200x900")

        # Configuration variables
        self.mode = tk.StringVar(value="simulation")
        # Initialize False, on_mode_change will set default based on initial mode
        self.use_accelerometer = tk.BooleanVar(value=False)
        self.use_kalman_filter = tk.BooleanVar(value=True)
        self.num_anchors = tk.IntVar(value=4)

        # Parameter configuration (Simulation and Real-time)
        self.use_default_params = tk.BooleanVar(value=True)
        self.update_interval = tk.IntVar(value=200)
        self.movement_speed = tk.DoubleVar(value=1.0)
        self.noise_std_dev = tk.DoubleVar(value=0.10)
        self.outlier_prob = tk.DoubleVar(value=0.10)
        self.outlier_magnitude = tk.DoubleVar(value=0.3)
        # Tweak 2: Q-parameter available for both modes
        self.kf_q_parameter = tk.DoubleVar(value=20.0)
        # RANSAC threshold for outlier filtering (more aggressive = smaller value)
        self.ransac_threshold = tk.DoubleVar(value=0.15)

        # >>>>> NEW: Advanced Enhancement Parameters
        # Preprocessing & Filtering
        self.enable_median_filter = tk.BooleanVar(value=True)
        self.median_filter_window = tk.IntVar(value=5)
        self.data_staleness_threshold = tk.DoubleVar(value=1.0)

        # Weighted Least Squares (WLS)
        self.enable_wls = tk.BooleanVar(value=True)
        self.rssi_min = tk.DoubleVar(value=-90.0)
        self.rssi_max = tk.DoubleVar(value=-40.0)
        self.samples_min = tk.IntVar(value=10)
        self.samples_max = tk.IntVar(value=100)

        # Measurement Gating
        self.enable_measurement_gating = tk.BooleanVar(value=True)
        self.gating_threshold = tk.DoubleVar(value=9.21)

        # Asymmetric R Adjustment
        self.enable_asymmetric_r = tk.BooleanVar(value=True)
        self.r_towards_factor = tk.DoubleVar(value=1.0)
        self.r_away_factor = tk.DoubleVar(value=3.0)
        self.velocity_threshold = tk.DoubleVar(value=0.1)

        # Dynamic R Bounds
        self.enable_dynamic_r = tk.BooleanVar(value=True)
        self.min_r_variance = tk.DoubleVar(value=0.0025)  # (0.05)^2
        self.max_r_variance = tk.DoubleVar(value=2.25)  # (1.5)^2

        # Anchor Health Monitoring
        self.enable_anchor_health = tk.BooleanVar(value=True)
        self.min_inlier_rate = tk.DoubleVar(value=0.5)
        self.health_window_size = tk.IntVar(value=50)

        # Display Options
        self.show_ransac_estimate = tk.BooleanVar(value=True)
        self.show_kf_estimate = tk.BooleanVar(value=True)
        self.show_coordinates = tk.BooleanVar(value=True)
        # <<<<< End NEW

        # Anchor configuration
        self.anchor_coords = {}
        self.com_port_vars = {}

        # Results
        self.config_result = None

        # Configuration file path
        self.config_file = os.path.join(os.path.dirname(__file__), "config.json")

        self.create_widgets()

        # Load saved configuration
        self.load_configuration()

    def save_configuration(self):
        """Save current configuration to JSON file"""
        try:
            config_data = {
                "mode": self.mode.get(),
                "use_accelerometer": self.use_accelerometer.get(),
                "use_kalman_filter": self.use_kalman_filter.get(),
                "num_anchors": self.num_anchors.get(),
                "use_default_params": self.use_default_params.get(),
                "update_interval": self.update_interval.get(),
                "movement_speed": self.movement_speed.get(),
                "noise_std_dev": self.noise_std_dev.get(),
                "outlier_prob": self.outlier_prob.get(),
                "outlier_magnitude": self.outlier_magnitude.get(),
                "kf_q_parameter": self.kf_q_parameter.get(),
                "ransac_threshold": self.ransac_threshold.get(),
                # >>>>> NEW: Advanced Enhancement Parameters
                # Preprocessing & Filtering
                "enable_median_filter": self.enable_median_filter.get(),
                "median_filter_window": self.median_filter_window.get(),
                "data_staleness_threshold": self.data_staleness_threshold.get(),
                # Weighted Least Squares (WLS)
                "enable_wls": self.enable_wls.get(),
                "rssi_min": self.rssi_min.get(),
                "rssi_max": self.rssi_max.get(),
                "samples_min": self.samples_min.get(),
                "samples_max": self.samples_max.get(),
                # Measurement Gating
                "enable_measurement_gating": self.enable_measurement_gating.get(),
                "gating_threshold": self.gating_threshold.get(),
                # Asymmetric R Adjustment
                "enable_asymmetric_r": self.enable_asymmetric_r.get(),
                "r_towards_factor": self.r_towards_factor.get(),
                "r_away_factor": self.r_away_factor.get(),
                "velocity_threshold": self.velocity_threshold.get(),
                # Dynamic R Bounds
                "enable_dynamic_r": self.enable_dynamic_r.get(),
                "min_r_variance": self.min_r_variance.get(),
                "max_r_variance": self.max_r_variance.get(),
                # Anchor Health Monitoring
                "enable_anchor_health": self.enable_anchor_health.get(),
                "min_inlier_rate": self.min_inlier_rate.get(),
                "health_window_size": self.health_window_size.get(),
                # Display Options
                "show_ransac_estimate": self.show_ransac_estimate.get(),
                "show_kf_estimate": self.show_kf_estimate.get(),
                "show_coordinates": self.show_coordinates.get(),
                # <<<<< End NEW
                "anchor_coords": {},
                "com_ports": {},
            }

            # Save anchor coordinates
            for anchor_name, (x_var, y_var) in self.anchor_coords.items():
                config_data["anchor_coords"][anchor_name] = {
                    "x": x_var.get(),
                    "y": y_var.get(),
                }

            # Save COM port assignments
            for anchor_name, port_var in self.com_port_vars.items():
                config_data["com_ports"][anchor_name] = port_var.get()

            with open(self.config_file, "w") as f:
                json.dump(config_data, f, indent=2)

            print(f"✅ Configuration saved to {self.config_file}")

            # Update status in UI if available
            if hasattr(self, "status_label"):
                self.status_label.config(
                    text="✅ Configuration saved successfully", foreground="#008000"
                )
                # Reset status after 3 seconds
                self.root.after(
                    3000,
                    lambda: self.status_label.config(
                        text="📝 Configuration will be saved automatically when you start the system",
                        foreground="#666666",
                    ),
                )

        except Exception as e:
            print(f"❌ Error saving configuration: {e}")

    def load_configuration(self):
        """Load configuration from JSON file if it exists"""
        try:
            if not os.path.exists(self.config_file):
                print("📝 No saved configuration found - using defaults")
                return

            with open(self.config_file, "r") as f:
                config_data = json.load(f)

            # Load basic settings
            self.mode.set(config_data.get("mode", "simulation"))
            self.use_accelerometer.set(config_data.get("use_accelerometer", False))
            self.use_kalman_filter.set(config_data.get("use_kalman_filter", True))
            self.num_anchors.set(config_data.get("num_anchors", 4))
            self.use_default_params.set(config_data.get("use_default_params", True))
            self.update_interval.set(config_data.get("update_interval", 200))
            self.movement_speed.set(config_data.get("movement_speed", 1.0))
            self.noise_std_dev.set(config_data.get("noise_std_dev", 0.10))
            self.outlier_prob.set(config_data.get("outlier_prob", 0.10))
            self.outlier_magnitude.set(config_data.get("outlier_magnitude", 0.3))
            self.kf_q_parameter.set(config_data.get("kf_q_parameter", 20.0))
            self.ransac_threshold.set(config_data.get("ransac_threshold", 0.15))

            # >>>>> NEW: Load Advanced Enhancement Parameters
            # Preprocessing & Filtering
            self.enable_median_filter.set(config_data.get("enable_median_filter", True))
            self.median_filter_window.set(config_data.get("median_filter_window", 5))
            self.data_staleness_threshold.set(
                config_data.get("data_staleness_threshold", 1.0)
            )

            # Weighted Least Squares (WLS)
            self.enable_wls.set(config_data.get("enable_wls", True))
            self.rssi_min.set(config_data.get("rssi_min", -90.0))
            self.rssi_max.set(config_data.get("rssi_max", -40.0))
            self.samples_min.set(config_data.get("samples_min", 10))
            self.samples_max.set(config_data.get("samples_max", 100))

            # Measurement Gating
            self.enable_measurement_gating.set(
                config_data.get("enable_measurement_gating", True)
            )
            self.gating_threshold.set(config_data.get("gating_threshold", 9.21))

            # Asymmetric R Adjustment
            self.enable_asymmetric_r.set(config_data.get("enable_asymmetric_r", True))
            self.r_towards_factor.set(config_data.get("r_towards_factor", 1.0))
            self.r_away_factor.set(config_data.get("r_away_factor", 3.0))
            self.velocity_threshold.set(config_data.get("velocity_threshold", 0.1))

            # Dynamic R Bounds
            self.enable_dynamic_r.set(config_data.get("enable_dynamic_r", True))
            self.min_r_variance.set(config_data.get("min_r_variance", 0.0025))
            self.max_r_variance.set(config_data.get("max_r_variance", 2.25))

            # Anchor Health Monitoring
            self.enable_anchor_health.set(config_data.get("enable_anchor_health", True))
            self.min_inlier_rate.set(config_data.get("min_inlier_rate", 0.5))
            self.health_window_size.set(config_data.get("health_window_size", 50))

            # Display Options
            self.show_ransac_estimate.set(config_data.get("show_ransac_estimate", True))
            self.show_kf_estimate.set(config_data.get("show_kf_estimate", True))
            self.show_coordinates.set(config_data.get("show_coordinates", True))
            # <<<<< End NEW

            # Update anchor configuration based on saved number of anchors
            self.update_anchor_config()

            # Load anchor coordinates
            saved_anchors = config_data.get("anchor_coords", {})
            for anchor_name, coords in saved_anchors.items():
                if anchor_name in self.anchor_coords:
                    x_var, y_var = self.anchor_coords[anchor_name]
                    x_var.set(coords.get("x", 0.0))
                    y_var.set(coords.get("y", 0.0))

            # Load COM port assignments
            saved_com_ports = config_data.get("com_ports", {})
            for anchor_name, port in saved_com_ports.items():
                if anchor_name in self.com_port_vars:
                    self.com_port_vars[anchor_name].set(port)

            print(f"✅ Configuration loaded from {self.config_file}")

            # Update status in UI if available
            if hasattr(self, "status_label"):
                self.status_label.config(
                    text="✅ Previous configuration loaded", foreground="#008000"
                )
                # Reset status after 3 seconds
                self.root.after(
                    3000,
                    lambda: self.status_label.config(
                        text="📝 Configuration will be saved automatically when you start the system",
                        foreground="#666666",
                    ),
                )

        except Exception as e:
            print(f"❌ Error loading configuration: {e}")
            print("📝 Using default configuration")

    def reset_to_defaults(self):
        """Reset all configuration to default values"""
        try:
            # Reset basic settings to defaults
            self.mode.set("simulation")
            self.use_accelerometer.set(False)
            self.use_kalman_filter.set(True)
            self.num_anchors.set(4)
            self.use_default_params.set(True)
            self.update_interval.set(200)
            self.movement_speed.set(1.0)
            self.noise_std_dev.set(0.10)
            self.outlier_prob.set(0.10)
            self.outlier_magnitude.set(0.3)
            self.kf_q_parameter.set(20.0)
            self.ransac_threshold.set(0.15)

            # >>>>> NEW: Reset Advanced Enhancement Parameters to defaults
            # Preprocessing & Filtering
            self.enable_median_filter.set(True)
            self.median_filter_window.set(5)
            self.data_staleness_threshold.set(1.0)

            # Weighted Least Squares (WLS)
            self.enable_wls.set(True)
            self.rssi_min.set(-90.0)
            self.rssi_max.set(-40.0)
            self.samples_min.set(10)
            self.samples_max.set(100)

            # Measurement Gating
            self.enable_measurement_gating.set(True)
            self.gating_threshold.set(9.21)

            # Asymmetric R Adjustment
            self.enable_asymmetric_r.set(True)
            self.r_towards_factor.set(1.0)
            self.r_away_factor.set(3.0)
            self.velocity_threshold.set(0.1)

            # Dynamic R Bounds
            self.enable_dynamic_r.set(True)
            self.min_r_variance.set(0.0025)
            self.max_r_variance.set(2.25)

            # Anchor Health Monitoring
            self.enable_anchor_health.set(True)
            self.min_inlier_rate.set(0.5)
            self.health_window_size.set(50)

            # Display Options
            self.show_ransac_estimate.set(True)
            self.show_kf_estimate.set(True)
            self.show_coordinates.set(True)
            # <<<<< End NEW

            # Update anchor configuration to default number
            self.update_anchor_config()

            # Set default anchor coordinates (square formation)
            anchor_names = [f"Anchor{i + 1}" for i in range(4)]
            default_coords = [(0, 0), (5, 0), (5, 5), (0, 5)]

            for i, (anchor_name, (x, y)) in enumerate(
                zip(anchor_names, default_coords)
            ):
                if anchor_name in self.anchor_coords:
                    x_var, y_var = self.anchor_coords[anchor_name]
                    x_var.set(x)
                    y_var.set(y)

            # Clear COM port assignments
            for anchor_name, port_var in self.com_port_vars.items():
                port_var.set("")

            # Update mode-specific settings
            self.on_mode_change()
            self.on_param_preset_change()

            # Update status
            self.status_label.config(
                text="✨ Configuration reset to defaults", foreground="#008000"
            )

            # Reset status after 3 seconds
            self.root.after(
                3000,
                lambda: self.status_label.config(
                    text="📝 Configuration will be saved automatically when you start the system",
                    foreground="#666666",
                ),
            )

            print("✨ Configuration reset to defaults")

        except Exception as e:
            print(f"❌ Error resetting configuration: {e}")

        self.create_widgets()

    def create_widgets(self):
        """Create the configuration UI widgets"""
        # Configure root window
        self.root.configure(bg="#f0f0f0")
        self.root.resizable(False, False)

        # Create main frame with modern styling
        main_frame = ttk.Frame(self.root, padding="15")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Configure style for modern look
        style = ttk.Style()
        style.theme_use("clam")
        style.configure(
            "Title.TLabel", font=("Segoe UI", 14, "bold"), foreground="#2c3e50"
        )
        style.configure(
            "Heading.TLabel", font=("Segoe UI", 10, "bold"), foreground="#34495e"
        )
        # Use standard LabelFrame styling instead of custom
        style.configure(
            "TLabelFrame.Label", font=("Segoe UI", 9, "bold"), foreground="#2980b9"
        )

        # Title
        title_label = ttk.Label(
            main_frame,
            text="🎯 Multilateration System Configuration",
            style="Title.TLabel",
        )
        title_label.pack(pady=(0, 15))

        # Create two-column layout
        top_frame = ttk.Frame(main_frame)
        top_frame.pack(fill=tk.X, pady=(0, 10))

        left_column = ttk.Frame(top_frame)
        left_column.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 10))

        right_column = ttk.Frame(top_frame)
        right_column.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)

        # LEFT COLUMN

        # Mode Selection (compact)
        mode_frame = ttk.LabelFrame(left_column, text="📡 Mode Selection", padding="8")
        mode_frame.pack(fill=tk.X, pady=(0, 10))

        mode_buttons_frame = ttk.Frame(mode_frame)
        mode_buttons_frame.pack()

        ttk.Radiobutton(
            mode_buttons_frame,
            text="🔬 Simulation",
            variable=self.mode,
            value="simulation",
            command=self.on_mode_change,
        ).pack(side=tk.LEFT, padx=(0, 15))
        ttk.Radiobutton(
            mode_buttons_frame,
            text="⚡ Real-time",
            variable=self.mode,
            value="realtime",
            command=self.on_mode_change,
        ).pack(side=tk.LEFT)

        # Processing Options (compact)
        options_frame = ttk.LabelFrame(
            left_column, text="⚙️ Processing Options", padding="8"
        )
        options_frame.pack(fill=tk.X, pady=(0, 10))

        self.accel_check = ttk.Checkbutton(
            options_frame,
            text="📱 Accelerometer (Sim Only)",
            variable=self.use_accelerometer,
        )
        self.accel_check.pack(anchor=tk.W)

        ttk.Checkbutton(
            options_frame, text="🎯 Kalman Filter", variable=self.use_kalman_filter
        ).pack(anchor=tk.W)

        # System Parameters (compact)
        self.params_frame = ttk.LabelFrame(
            left_column, text="🔧 System Parameters", padding="8"
        )
        self.params_frame.pack(fill=tk.BOTH, expand=True)

        # Parameter preset selection (horizontal)
        preset_frame = ttk.Frame(self.params_frame)
        preset_frame.pack(fill=tk.X, pady=(0, 8))

        ttk.Radiobutton(
            preset_frame,
            text="📋 Defaults",
            variable=self.use_default_params,
            value=True,
            command=self.on_param_preset_change,
        ).pack(side=tk.LEFT, padx=(0, 15))
        ttk.Radiobutton(
            preset_frame,
            text="🎛️ Custom",
            variable=self.use_default_params,
            value=False,
            command=self.on_param_preset_change,
        ).pack(side=tk.LEFT)

        # Custom parameters frame (compact grid)
        self.custom_params_frame = ttk.Frame(self.params_frame)
        self.custom_params_frame.pack(fill=tk.BOTH, expand=True)

        # RIGHT COLUMN

        # Anchor Configuration (compact)
        anchor_frame = ttk.LabelFrame(
            right_column, text="⚓ Anchor Configuration", padding="8"
        )
        anchor_frame.pack(fill=tk.X, pady=(0, 10))

        # Number of anchors (inline)
        anchor_count_frame = ttk.Frame(anchor_frame)
        anchor_count_frame.pack(fill=tk.X, pady=(0, 8))

        ttk.Label(anchor_count_frame, text="Count:", font=("Segoe UI", 9)).pack(
            side=tk.LEFT
        )
        anchor_spin = ttk.Spinbox(
            anchor_count_frame,
            from_=3,
            to=6,
            width=4,
            textvariable=self.num_anchors,
            command=self.update_anchor_config,
        )
        anchor_spin.pack(side=tk.LEFT, padx=(5, 0))

        # Anchor details frame (compact table)
        self.anchor_details_frame = ttk.Frame(anchor_frame)
        self.anchor_details_frame.pack(fill=tk.X)

        # COM Port frame (compact)
        self.com_frame = ttk.LabelFrame(
            right_column, text="🔌 COM Port Assignment", padding="8"
        )
        self.com_frame.pack(fill=tk.BOTH, expand=True)

        # Create parameter input widgets
        self.create_parameter_widgets()

        # Bottom buttons with modern styling
        button_frame = ttk.Frame(main_frame)
        button_frame.pack(fill=tk.X, pady=(15, 0))

        # Style buttons
        style.configure("Action.TButton", font=("Segoe UI", 10, "bold"))

        button_container = ttk.Frame(button_frame)
        button_container.pack()

        start_btn = ttk.Button(
            button_container,
            text="🚀 Start System",
            command=self.start_system,
            style="Action.TButton",
        )
        start_btn.pack(side=tk.LEFT, padx=(0, 10), ipadx=10, ipady=5)

        reset_btn = ttk.Button(
            button_container,
            text="🔄 Reset to Defaults",
            command=self.reset_to_defaults,
        )
        reset_btn.pack(side=tk.LEFT, padx=(0, 10), ipadx=10, ipady=5)

        cancel_btn = ttk.Button(
            button_container, text="❌ Cancel", command=self.root.quit
        )
        cancel_btn.pack(side=tk.LEFT, ipadx=10, ipady=5)

        # Status label
        self.status_label = ttk.Label(
            button_frame,
            text="📝 Configuration will be saved automatically when you start the system",
            font=("Segoe UI", 9),
            foreground="#666666",
        )
        self.status_label.pack(pady=(10, 0))

        # Initialize configuration
        self.update_anchor_config()
        self.on_mode_change()
        self.on_param_preset_change()

    def create_parameter_widgets(self):
        """Create parameter input widgets with tabbed interface for advanced settings"""

        # Store simulation-specific widgets for enabling/disabling
        self.sim_params_widgets = []

        # Create notebook for tabbed parameters
        notebook = ttk.Notebook(self.custom_params_frame)
        notebook.pack(fill=tk.BOTH, expand=True, pady=5)

        # ============ Basic Parameters Tab ============
        basic_frame = ttk.Frame(notebook)
        notebook.add(basic_frame, text="📋 Basic")

        basic_grid = ttk.Frame(basic_frame)
        basic_grid.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        # General Parameters (always visible)
        general_params = [
            (
                "⏱️ Update Interval (ms):",
                self.update_interval,
                50,
                1000,
                1,
                "How often the system updates (lower = more responsive)",
            ),
            (
                "🎛️ Kalman Filter Q:",
                self.kf_q_parameter,
                0.1,
                200.0,
                1.0,
                "Process noise (higher = more responsive to changes)",
            ),
            (
                "🎯 RANSAC Threshold (m):",
                self.ransac_threshold,
                0.05,
                1.0,
                0.01,
                "Outlier rejection threshold (lower = more aggressive filtering)",
            ),
        ]

        # Simulation Parameters (only for simulation mode)
        sim_params = [
            (
                "🏃 Movement Speed (m/s):",
                self.movement_speed,
                0.1,
                5.0,
                0.1,
                "Speed of simulated object movement",
            ),
            (
                "📊 Noise Std Dev (m):",
                self.noise_std_dev,
                0.01,
                1.0,
                0.01,
                "Standard deviation of measurement noise",
            ),
            (
                "⚠️ Outlier Probability:",
                self.outlier_prob,
                0.0,
                0.5,
                0.01,
                "Probability of generating outlier measurements",
            ),
            (
                "💥 Outlier Magnitude:",
                self.outlier_magnitude,
                0.1,
                2.0,
                0.1,
                "Size of outlier errors relative to workspace",
            ),
        ]

        # Create general parameters
        row = 0
        ttk.Label(
            basic_grid, text="🔧 General Parameters", font=("Segoe UI", 10, "bold")
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(0, 10))
        row += 1

        for label_text, var, min_val, max_val, increment, tooltip in general_params:
            self._create_param_row(
                basic_grid, row, label_text, var, min_val, max_val, increment, tooltip
            )
            row += 1

        # Add separator for simulation parameters
        separator = ttk.Separator(basic_grid, orient=tk.HORIZONTAL)
        separator.grid(row=row, column=0, columnspan=3, sticky=tk.EW, pady=(15, 10))
        row += 1

        sim_label = ttk.Label(
            basic_grid,
            text="🔬 Simulation Only",
            font=("Segoe UI", 10, "bold"),
            foreground="#7f8c8d",
        )
        sim_label.grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(0, 10))
        self.sim_params_widgets.append(sim_label)
        row += 1

        # Create simulation parameters
        for label_text, var, min_val, max_val, increment, tooltip in sim_params:
            label, entry, help_label = self._create_param_row(
                basic_grid,
                row,
                label_text,
                var,
                min_val,
                max_val,
                increment,
                tooltip,
                sim_only=True,
            )
            self.sim_params_widgets.extend([label, entry, help_label])
            row += 1

        # ============ Advanced Filtering Tab ============
        filter_frame = ttk.Frame(notebook)
        notebook.add(filter_frame, text="🔍 Filtering")

        filter_grid = ttk.Frame(filter_frame)
        filter_grid.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        row = 0

        # Median Filter Section
        ttk.Label(
            filter_grid, text="📊 Median Filtering", font=("Segoe UI", 10, "bold")
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(0, 10))
        row += 1

        self._create_toggle_row(
            filter_grid,
            row,
            "🔲 Enable Median Filter:",
            self.enable_median_filter,
            "Applies median filter to remove spikes and jumps",
        )
        row += 1
        self._create_param_row(
            filter_grid,
            row,
            "🪟 Filter Window Size:",
            self.median_filter_window,
            3,
            15,
            1,
            "Number of recent measurements to use for median (larger = more smoothing)",
        )
        row += 1
        self._create_param_row(
            filter_grid,
            row,
            "⏰ Data Staleness (s):",
            self.data_staleness_threshold,
            0.1,
            5.0,
            0.1,
            "Maximum age of measurements before rejection",
        )
        row += 1

        # Measurement Gating Section
        separator = ttk.Separator(filter_grid, orient=tk.HORIZONTAL)
        separator.grid(row=row, column=0, columnspan=3, sticky=tk.EW, pady=(15, 10))
        row += 1

        ttk.Label(
            filter_grid, text="🚪 Measurement Gating", font=("Segoe UI", 10, "bold")
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(0, 10))
        row += 1

        self._create_toggle_row(
            filter_grid,
            row,
            "🔲 Enable Gating:",
            self.enable_measurement_gating,
            "Rejects measurements that are statistically unlikely",
        )
        row += 1
        self._create_param_row(
            filter_grid,
            row,
            "🎯 Gating Threshold:",
            self.gating_threshold,
            1.0,
            20.0,
            0.1,
            "Chi-squared threshold for rejection (9.21 = 99% confidence)",
        )
        row += 1

        # ============ Weighted Least Squares Tab ============
        wls_frame = ttk.Frame(notebook)
        notebook.add(wls_frame, text="⚖️ WLS")

        wls_grid = ttk.Frame(wls_frame)
        wls_grid.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        row = 0

        ttk.Label(
            wls_grid, text="⚖️ Weighted Least Squares", font=("Segoe UI", 10, "bold")
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(0, 10))
        row += 1

        self._create_toggle_row(
            wls_grid,
            row,
            "🔲 Enable WLS:",
            self.enable_wls,
            "Uses signal quality (RSSI, samples) to weight measurements",
        )
        row += 1

        ttk.Label(
            wls_grid, text="📶 RSSI Range (dBm):", font=("Segoe UI", 9, "bold")
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(15, 5))
        row += 1
        self._create_param_row(
            wls_grid,
            row,
            "📉 Min RSSI:",
            self.rssi_min,
            -120.0,
            -30.0,
            1.0,
            "Weakest expected RSSI value",
        )
        row += 1
        self._create_param_row(
            wls_grid,
            row,
            "📈 Max RSSI:",
            self.rssi_max,
            -60.0,
            -20.0,
            1.0,
            "Strongest expected RSSI value",
        )
        row += 1

        ttk.Label(
            wls_grid, text="📊 Sample Count Range:", font=("Segoe UI", 9, "bold")
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(15, 5))
        row += 1
        self._create_param_row(
            wls_grid,
            row,
            "📉 Min Samples:",
            self.samples_min,
            1,
            50,
            1,
            "Minimum expected sample count",
        )
        row += 1
        self._create_param_row(
            wls_grid,
            row,
            "📈 Max Samples:",
            self.samples_max,
            50,
            200,
            1,
            "Maximum expected sample count",
        )
        row += 1

        # ============ Dynamic R Tab ============
        dynamic_r_frame = ttk.Frame(notebook)
        notebook.add(dynamic_r_frame, text="📊 Dynamic R")

        dynamic_r_grid = ttk.Frame(dynamic_r_frame)
        dynamic_r_grid.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        row = 0

        ttk.Label(
            dynamic_r_grid,
            text="📊 Dynamic R Configuration",
            font=("Segoe UI", 10, "bold"),
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(0, 10))
        row += 1

        self._create_toggle_row(
            dynamic_r_grid,
            row,
            "🔲 Enable Dynamic R:",
            self.enable_dynamic_r,
            "Use measurement quality-based adaptive noise covariance",
        )
        row += 1

        ttk.Label(
            dynamic_r_grid, text="📏 Variance Bounds", font=("Segoe UI", 9, "bold")
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(10, 5))
        row += 1

        self._create_param_row(
            dynamic_r_grid,
            row,
            "📉 Min R Variance:",
            self.min_r_variance,
            0.0001,
            0.1,
            0.0001,
            "Minimum measurement noise variance (avoid overconfidence)",
        )
        row += 1
        self._create_param_row(
            dynamic_r_grid,
            row,
            "📈 Max R Variance:",
            self.max_r_variance,
            0.1,
            10.0,
            0.1,
            "Maximum measurement noise variance (avoid excessive lag)",
        )
        row += 1

        # ============ Asymmetric Adjustment Tab ============
        asym_frame = ttk.Frame(notebook)
        notebook.add(asym_frame, text="🔄 Asymmetric")

        asym_grid = ttk.Frame(asym_frame)
        asym_grid.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        row = 0

        ttk.Label(
            asym_grid, text="🔄 Asymmetric R Adjustment", font=("Segoe UI", 10, "bold")
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(0, 10))
        row += 1

        self._create_toggle_row(
            asym_grid,
            row,
            "🔲 Enable Asymmetric R:",
            self.enable_asymmetric_r,
            "Compensates for directional measurement biases",
        )
        row += 1
        self._create_param_row(
            asym_grid,
            row,
            "➡️ Towards Factor:",
            self.r_towards_factor,
            0.1,
            5.0,
            0.1,
            "R multiplier when moving towards anchors (1.0 = normal trust)",
        )
        row += 1
        self._create_param_row(
            asym_grid,
            row,
            "⬅️ Away Factor:",
            self.r_away_factor,
            0.1,
            10.0,
            0.1,
            "R multiplier when moving away from anchors (>1 = less trust)",
        )
        row += 1
        self._create_param_row(
            asym_grid,
            row,
            "🚶 Velocity Threshold:",
            self.velocity_threshold,
            0.01,
            1.0,
            0.01,
            "Minimum velocity to apply asymmetric adjustment",
        )
        row += 1

        # ============ Anchor Health Tab ============
        health_frame = ttk.Frame(notebook)
        notebook.add(health_frame, text="🏥 Health")

        health_grid = ttk.Frame(health_frame)
        health_grid.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        row = 0

        ttk.Label(
            health_grid,
            text="🏥 Anchor Health Monitoring",
            font=("Segoe UI", 10, "bold"),
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(0, 10))
        row += 1

        self._create_toggle_row(
            health_grid,
            row,
            "🔲 Enable Health Monitor:",
            self.enable_anchor_health,
            "Tracks and excludes consistently poor anchors",
        )
        row += 1
        self._create_param_row(
            health_grid,
            row,
            "✅ Min Inlier Rate:",
            self.min_inlier_rate,
            0.1,
            1.0,
            0.05,
            "Minimum acceptable inlier rate for healthy anchors",
        )
        row += 1
        self._create_param_row(
            health_grid,
            row,
            "🪟 Health Window Size:",
            self.health_window_size,
            10,
            200,
            5,
            "Number of measurements to track for health assessment",
        )
        row += 1

        # ============ Display Options Tab ============
        display_frame = ttk.Frame(notebook)
        notebook.add(display_frame, text="👁️ Display")

        display_grid = ttk.Frame(display_frame)
        display_grid.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        row = 0

        ttk.Label(
            display_grid,
            text="👁️ Display Options",
            font=("Segoe UI", 10, "bold"),
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(0, 10))
        row += 1

        self._create_toggle_row(
            display_grid,
            row,
            "🎯 Show RANSAC Estimate:",
            self.show_ransac_estimate,
            "Display RANSAC position estimate on the plot",
        )
        row += 1

        self._create_toggle_row(
            display_grid,
            row,
            "🧠 Show Kalman Filter:",
            self.show_kf_estimate,
            "Display Kalman Filter position estimate on the plot",
        )
        row += 1

        self._create_toggle_row(
            display_grid,
            row,
            "📍 Show Coordinates:",
            self.show_coordinates,
            "Display numerical coordinates for visible estimates",
        )
        row += 1

    def _create_param_row(
        self,
        parent,
        row,
        label_text,
        var,
        min_val,
        max_val,
        increment,
        tooltip,
        sim_only=False,
    ):
        """Helper to create a parameter row with label, input, and help text"""
        color = "#7f8c8d" if sim_only else "black"

        label = ttk.Label(
            parent, text=label_text, font=("Segoe UI", 9), foreground=color
        )
        label.grid(row=row, column=0, sticky=tk.W, padx=(0, 10), pady=2)

        if isinstance(var, tk.IntVar):
            entry = ttk.Spinbox(
                parent, from_=min_val, to=max_val, width=10, textvariable=var
            )
        else:
            entry = ttk.Spinbox(
                parent,
                from_=min_val,
                to=max_val,
                width=10,
                textvariable=var,
                increment=increment,
            )
        entry.grid(row=row, column=1, sticky=tk.W, padx=(0, 10), pady=2)

        help_label = ttk.Label(
            parent, text=f"ℹ️ {tooltip}", font=("Segoe UI", 8), foreground="#666666"
        )
        help_label.grid(row=row, column=2, sticky=tk.W, pady=2)

        return label, entry, help_label

    def _create_toggle_row(self, parent, row, label_text, var, tooltip):
        """Helper to create a toggle row with checkbox and help text"""
        checkbox = ttk.Checkbutton(parent, text=label_text, variable=var)
        checkbox.grid(row=row, column=0, columnspan=2, sticky=tk.W, pady=2)

        help_label = ttk.Label(
            parent, text=f"ℹ️ {tooltip}", font=("Segoe UI", 8), foreground="#666666"
        )
        help_label.grid(row=row, column=2, sticky=tk.W, pady=2)

        return checkbox, help_label

    def on_param_preset_change(self):
        """Handle parameter preset change"""
        state = "disabled" if self.use_default_params.get() else "normal"

        # Enable/disable all widgets in the custom parameters frame
        for widget in self.custom_params_frame.winfo_children():
            if isinstance(widget, (ttk.Spinbox, ttk.Entry)):
                widget.configure(state=state)

        # If in real-time mode, simulation-specific parameters should remain disabled
        if self.mode.get() == "realtime":
            self.disable_simulation_parameters()

    def disable_simulation_parameters(self):
        """Disable simulation-specific parameter widgets"""
        for widget in self.sim_params_widgets:
            try:
                if isinstance(widget, (ttk.Spinbox, ttk.Entry)):
                    widget.configure(state="disabled")
                elif isinstance(widget, ttk.Label):
                    widget.configure(
                        foreground="#bdc3c7"
                    )  # Light gray for disabled look
            except tk.TclError:
                # Widget might have been destroyed
                pass

    def on_mode_change(self):
        """Handle mode change between simulation and real-time"""
        if self.mode.get() == "realtime":
            # Tweak 3: Set accelerometer to False by default for real-time mode
            self.use_accelerometer.set(False)
            # Disable the checkbox as it's simulation only
            self.accel_check.configure(state="disabled")
            self.disable_simulation_parameters()
        else:
            # Enable accelerometer checkbox for simulation mode
            self.accel_check.configure(state="normal")
            # Re-evaluate widget states based on the preset selection
            self.on_param_preset_change()

        self.update_com_ports()

    def update_anchor_config(self):
        """Update anchor configuration with compact layout"""
        for widget in self.anchor_details_frame.winfo_children():
            widget.destroy()

        try:
            num_anchors = self.num_anchors.get()
        except tk.TclError:
            return

        self.anchor_coords.clear()
        default_positions = [
            (0.0, 0.0),
            (0.0, 2.0),
            (2.0, 2.0),
            (2.0, 0.0),
            (1.0, 3.0),
            (1.0, -1.0),
        ]

        # Create compact header
        header_frame = ttk.Frame(self.anchor_details_frame)
        header_frame.pack(fill=tk.X, pady=(0, 5))

        ttk.Label(header_frame, text="ID", font=("Segoe UI", 9, "bold"), width=4).pack(
            side=tk.LEFT
        )
        ttk.Label(
            header_frame, text="X (m)", font=("Segoe UI", 9, "bold"), width=8
        ).pack(side=tk.LEFT, padx=(5, 0))
        ttk.Label(
            header_frame, text="Y (m)", font=("Segoe UI", 9, "bold"), width=8
        ).pack(side=tk.LEFT, padx=(5, 0))

        # Create anchor entries in compact rows
        for i in range(num_anchors):
            anchor_name = f"A{i}"

            row_frame = ttk.Frame(self.anchor_details_frame)
            row_frame.pack(fill=tk.X, pady=1)

            ttk.Label(row_frame, text=anchor_name, font=("Segoe UI", 9), width=4).pack(
                side=tk.LEFT
            )

            default_x = default_positions[i][0] if i < len(default_positions) else 0.0
            x_var = tk.DoubleVar(value=default_x)
            x_entry = ttk.Entry(
                row_frame, textvariable=x_var, width=6, font=("Segoe UI", 9)
            )
            x_entry.pack(side=tk.LEFT, padx=(5, 0))

            default_y = default_positions[i][1] if i < len(default_positions) else 0.0
            y_var = tk.DoubleVar(value=default_y)
            y_entry = ttk.Entry(
                row_frame, textvariable=y_var, width=6, font=("Segoe UI", 9)
            )
            y_entry.pack(side=tk.LEFT, padx=(5, 0))

            self.anchor_coords[anchor_name] = (x_var, y_var)

        self.update_com_ports()

    def update_com_ports(self):
        """Update COM port selection with compact layout"""
        for widget in self.com_frame.winfo_children():
            widget.destroy()

        if self.mode.get() != "realtime":
            # Hide COM frame in simulation mode
            self.com_frame.pack_forget()
            return
        else:
            # Show COM frame in real-time mode
            self.com_frame.pack(fill=tk.BOTH, expand=True)

        available_ports = [port.device for port in serial.tools.list_ports.comports()]

        if not available_ports:
            no_ports_label = ttk.Label(
                self.com_frame,
                text="⚠️ No COM ports available",
                foreground="#e74c3c",
                font=("Segoe UI", 9),
            )
            no_ports_label.pack(pady=10)
            return

        try:
            num_anchors = self.num_anchors.get()
        except tk.TclError:
            return

        self.com_port_vars.clear()

        # Create compact COM port assignments
        for i in range(num_anchors):
            anchor_name = f"A{i}"

            port_frame = ttk.Frame(self.com_frame)
            port_frame.pack(fill=tk.X, pady=2)

            ttk.Label(
                port_frame, text=f"{anchor_name}:", font=("Segoe UI", 9), width=4
            ).pack(side=tk.LEFT)

            port_var = tk.StringVar()
            if i < len(available_ports):
                port_var.set(available_ports[i])

            port_combo = ttk.Combobox(
                port_frame,
                textvariable=port_var,
                values=available_ports,
                width=12,
                state="readonly",
                font=("Segoe UI", 9),
            )
            port_combo.pack(side=tk.LEFT, padx=(5, 0))

            self.com_port_vars[anchor_name] = port_var

    def get_default_params(self):
        return {
            "update_interval": 200,
            "movement_speed": 1.0,
            "noise_std_dev": 0.10,
            "outlier_prob": 0.10,
            "outlier_magnitude": 0.3,
            "kf_q_parameter": 20.0,
            "ransac_threshold": 0.15,
        }

    def start_system(self):
        """Collect configuration and start the multilateration system"""
        try:
            # Collect anchor coordinates
            anchors = {}
            for anchor_name, (x_var, y_var) in self.anchor_coords.items():
                anchors[anchor_name] = np.array([x_var.get(), y_var.get()])

            # Collect COM ports
            com_ports = {}
            if self.mode.get() == "realtime":
                for anchor_name, port_var in self.com_port_vars.items():
                    port = port_var.get()
                    # Allow empty ports (user might connect later)
                    com_ports[anchor_name] = port

            # Collect System parameters
            system_params = self.get_default_params()
            system_params["use_defaults"] = self.use_default_params.get()

            if not self.use_default_params.get():
                system_params["update_interval"] = self.update_interval.get()
                system_params["kf_q_parameter"] = self.kf_q_parameter.get()
                system_params["ransac_threshold"] = self.ransac_threshold.get()

                # >>>>> NEW: Advanced Enhancement Parameters
                # Preprocessing & Filtering
                system_params["enable_median_filter"] = self.enable_median_filter.get()
                system_params["median_filter_window"] = self.median_filter_window.get()
                system_params["data_staleness_threshold"] = (
                    self.data_staleness_threshold.get()
                )

                # Weighted Least Squares (WLS)
                system_params["enable_wls"] = self.enable_wls.get()
                system_params["rssi_min"] = self.rssi_min.get()
                system_params["rssi_max"] = self.rssi_max.get()
                system_params["samples_min"] = self.samples_min.get()
                system_params["samples_max"] = self.samples_max.get()

                # Measurement Gating
                system_params["enable_measurement_gating"] = (
                    self.enable_measurement_gating.get()
                )
                system_params["gating_threshold"] = self.gating_threshold.get()

                # Asymmetric R Adjustment
                system_params["enable_asymmetric_r"] = self.enable_asymmetric_r.get()
                system_params["r_towards_factor"] = self.r_towards_factor.get()
                system_params["r_away_factor"] = self.r_away_factor.get()
                system_params["velocity_threshold"] = self.velocity_threshold.get()

                # Dynamic R Bounds
                system_params["enable_dynamic_r"] = self.enable_dynamic_r.get()
                system_params["min_r_variance"] = self.min_r_variance.get()
                system_params["max_r_variance"] = self.max_r_variance.get()

                # Anchor Health Monitoring
                system_params["enable_anchor_health"] = self.enable_anchor_health.get()
                system_params["min_inlier_rate"] = self.min_inlier_rate.get()
                system_params["health_window_size"] = self.health_window_size.get()

                # Display Options
                system_params["show_ransac_estimate"] = self.show_ransac_estimate.get()
                system_params["show_kf_estimate"] = self.show_kf_estimate.get()
                system_params["show_coordinates"] = self.show_coordinates.get()
                # <<<<< End NEW

                if self.mode.get() == "simulation":
                    system_params["movement_speed"] = self.movement_speed.get()
                    system_params["noise_std_dev"] = self.noise_std_dev.get()
                    system_params["outlier_prob"] = self.outlier_prob.get()
                    system_params["outlier_magnitude"] = self.outlier_magnitude.get()

            # Create configuration dictionary
            self.config_result = {
                "mode": self.mode.get(),
                # Ensure use_accelerometer is only True if mode is simulation
                "use_accelerometer": self.use_accelerometer.get()
                if self.mode.get() == "simulation"
                else False,
                "use_kalman_filter": self.use_kalman_filter.get(),
                "anchors": anchors,
                "com_ports": com_ports if self.mode.get() == "realtime" else None,
                "system_params": system_params,
            }

            # Save configuration before starting
            self.save_configuration()

            self.root.quit()

        except Exception as e:
            messagebox.showerror("Configuration Error", f"An error occurred: {e}")

    def get_configuration(self):
        """Run the configuration screen and return the configuration"""
        self.root.mainloop()
        try:
            self.root.destroy()
        except tk.TclError:
            # Window was already destroyed (user closed it)
            pass
        return self.config_result


class SerialDataReader:
    """Handles serial communication with nRF54L15 DK devices"""

    def __init__(self, com_ports, data_queue):
        self.com_ports = com_ports
        self.data_queue = data_queue
        self.serial_connections = {}
        self.running = False
        self.threads = []

    def start(self):
        """Start serial communication threads"""
        self.running = True

        # Open serial connections
        for anchor_name, com_port in self.com_ports.items():
            try:
                ser = serial.Serial(com_port, 115200, timeout=1)
                self.serial_connections[anchor_name] = ser

                # Start reading thread for this anchor
                thread = threading.Thread(
                    target=self._read_serial_data, args=(anchor_name, ser)
                )
                thread.daemon = True
                thread.start()
                self.threads.append(thread)

            except serial.SerialException as e:
                print(f"Failed to open {com_port} for {anchor_name}: {e}")

    def stop(self):
        """Stop serial communication"""
        self.running = False

        # Close serial connections
        for ser in self.serial_connections.values():
            try:
                ser.close()
            except Exception:
                pass

    def _read_serial_data(self, anchor_name, ser):
        """Read data from serial port and parse it"""
        while self.running:
            try:
                line = ser.readline().decode("utf-8").strip()
                if line:
                    parsed_data = self._parse_serial_line(line, anchor_name)
                    if parsed_data:
                        self.data_queue.put(parsed_data)

            except serial.SerialException as e:
                print(f"Error reading from {anchor_name}: {e}")
                # Add a small delay to prevent busy-looping on error
                time.sleep(0.1)
            except UnicodeDecodeError:
                # Handle cases where we get non-utf8 data
                pass
            except Exception as e:
                print(
                    f"An unexpected error occurred while reading from {anchor_name}: {e}"
                )
                time.sleep(0.1)

    def _parse_serial_line(self, line, anchor_name):
        """Parse the C output: DIST:%.3f,AP:%d,SAMPLES:%d[,RSSI:%d[,RSSI_DIST:%.3f]]

        RSSI and RSSI_DIST are optional — the ipt_swap variant emits CS-only lines
        (DIST,AP,SAMPLES). When RSSI is absent, the measurement gets full weight.
        """
        try:
            # Use regex to parse the line; RSSI / RSSI_DIST optional.
            pattern = (
                r"DIST:([\d.-]+),AP:(\d+),SAMPLES:(\d+)"
                r"(?:,RSSI:([\d.-]+))?(?:,RSSI_DIST:([\d.-]+))?"
            )
            match = re.match(pattern, line)

            if match:
                distance = float(match.group(1))
                ap_id = int(match.group(2))
                samples = int(match.group(3))
                # Absent RSSI -> 0.0, which normalises to full weight (no penalty).
                rssi = float(match.group(4)) if match.group(4) is not None else 0.0

                return {
                    "anchor_name": anchor_name,
                    "distance": distance,
                    "ap_id": ap_id,
                    "samples": samples,
                    "rssi": rssi,
                    "timestamp": time.time(),
                }

        except Exception as e:
            print(f"Error parsing line '{line}': {e}")

        return None


class KalmanFilterCAB:
    """Kalman Filter for position, velocity, acceleration and bias estimation"""

    def __init__(
        self,
        dt,
        std_jerk,
        std_bias_walk,
        std_meas_pos,
        std_meas_accel=None,
        use_accelerometer=True,
    ):
        self.dt = dt
        self.use_accelerometer = use_accelerometer
        self.std_jerk = std_jerk
        self.std_bias_walk = std_bias_walk
        self.std_meas_pos = std_meas_pos
        self.std_meas_accel = std_meas_accel

        self.initialize_matrices()

    def initialize_matrices(self):
        """Initialize state vector, transition, measurement, and noise matrices"""
        if self.use_accelerometer:
            # State vector: [px, py, vx, vy, ax, ay, bias_x, bias_y]
            self.state_dim = 8
            self.x = np.zeros((8, 1))
            self.H = np.zeros((4, 8))
            self.H[0, 0] = 1
            self.H[1, 1] = 1
            self.H[2, 4] = 1
            self.H[3, 5] = 1
            self.H[2, 6] = 1
            self.H[3, 7] = 1
            self.R = np.diag(
                [
                    self.std_meas_pos**2,
                    self.std_meas_pos**2,
                    self.std_meas_accel**2,
                    self.std_meas_accel**2,
                ]
            )
            self.P = np.eye(8) * 500
        else:
            # State vector: [px, py, vx, vy, ax, ay]
            self.state_dim = 6
            self.x = np.zeros((6, 1))
            self.H = np.zeros((2, 6))
            self.H[0, 0] = 1
            self.H[1, 1] = 1
            self.R = np.diag([self.std_meas_pos**2, self.std_meas_pos**2])
            self.P = np.eye(6) * 500

        self.F = np.eye(self.state_dim)
        self.update_dt(self.dt)  # Initialize F and Q based on dt

    def calculate_Q(self):
        """Calculate the process noise covariance matrix Q based on current std_jerk and dt"""
        dt = self.dt
        # Q matrix for the motion model (first 6 states)
        q_jerk = (
            np.array(
                [
                    [dt**6 / 36, 0, dt**5 / 12, 0, dt**4 / 6, 0],
                    [0, dt**6 / 36, 0, dt**5 / 12, 0, dt**4 / 6],
                    [dt**5 / 12, 0, dt**4 / 4, 0, dt**3 / 2, 0],
                    [0, dt**5 / 12, 0, dt**4 / 4, 0, dt**3 / 2],
                    [dt**4 / 6, 0, dt**3 / 2, 0, dt**2, 0],
                    [0, dt**4 / 6, 0, dt**3 / 2, 0, dt**2],
                ]
            )
            * self.std_jerk**2
        )

        if self.state_dim == 8:
            q_bias = np.eye(2) * (self.std_bias_walk**2) * dt
            return block_diag(q_jerk, q_bias)
        else:
            return q_jerk

    def update_dt(self, dt):
        """Update the time step and recalculate F and Q matrices"""
        self.dt = dt
        # Update F matrix
        self.F[0, 2] = dt
        self.F[1, 3] = dt
        self.F[0, 4] = 0.5 * dt**2
        self.F[1, 5] = 0.5 * dt**2
        self.F[2, 4] = dt
        self.F[3, 5] = dt
        # Update Q matrix
        self.Q = self.calculate_Q()

    def set_Q_parameter(self, std_jerk):
        """Update the std_jerk (Q parameter) and recalculate Q matrix (Tweak 2)"""
        self.std_jerk = std_jerk
        self.Q = self.calculate_Q()

    def predict(self):
        """Prediction step"""
        self.x = self.F @ self.x
        self.P = self.F @ self.P @ self.F.T + self.Q

    # def update(self, z): # Old signature
    def update(self, z, dynamic_R=None):  # New signature
        """Update step with measurement z and optional dynamic R for position."""

        if self.use_accelerometer and len(z) == 4:
            # Position and acceleration measurement
            z = z.reshape(4, 1)
            H = self.H
            # Handle Dynamic R
            if dynamic_R is not None:
                # Combine dynamic position R with fixed acceleration R
                R_accel = self.R[2:, 2:]
                R = block_diag(dynamic_R, R_accel)
            else:
                R = self.R

        else:
            # Position only measurement
            z = z[:2].reshape(2, 1)
            if self.use_accelerometer:
                H = self.H[:2, :]
                R = dynamic_R if dynamic_R is not None else self.R[:2, :2]
            else:
                H = self.H
                R = dynamic_R if dynamic_R is not None else self.R

        S = H @ self.P @ H.T + R

        try:
            K = self.P @ H.T @ np.linalg.inv(S)
        except np.linalg.LinAlgError:
            # Handle singular matrix S
            print(
                "Warning: Matrix S is singular in Kalman update, using pseudo-inverse."
            )
            K = self.P @ H.T @ np.linalg.pinv(S)

        y = z - H @ self.x
        self.x = self.x + K @ y
        identity_matrix = np.eye(self.state_dim)
        self.P = (identity_matrix - K @ H) @ self.P


class MultilaterationSystem:
    """Main multilateration system"""

    def __init__(self, config):
        self.config = config
        self.anchors = config["anchors"]
        self.anchor_array = np.vstack(list(self.anchors.values()))
        self.use_accelerometer = config["use_accelerometer"]
        self.use_kalman_filter = config["use_kalman_filter"]

        # System parameters - Define boundaries dynamically
        self.X_MIN = min([pos[0] for pos in self.anchors.values()]) - 2.0
        self.X_MAX = max([pos[0] for pos in self.anchors.values()]) + 2.0
        self.Y_MIN = min([pos[1] for pos in self.anchors.values()]) - 2.0
        self.Y_MAX = max([pos[1] for pos in self.anchors.values()]) + 2.0
        self.X_DIM = self.X_MAX - self.X_MIN
        self.Y_DIM = self.Y_MAX - self.Y_MIN

        # Load system parameters
        sys_params = config.get("system_params", {})
        self.UPDATE_INTERVAL_MS = sys_params.get("update_interval", 200)
        self.dt = self.UPDATE_INTERVAL_MS / 1000.0

        # Simulation parameters
        self.MOVEMENT_SPEED = sys_params.get("movement_speed", 1.0)
        self.MIN_PAUSE_S = 2.0
        self.MAX_PAUSE_S = 5.0
        self.NOISE_STD_DEV = sys_params.get("noise_std_dev", 0.10)
        self.OUTLIER_PROB = sys_params.get("outlier_prob", 0.10)
        avg_dim = (self.X_DIM + self.Y_DIM) / 2
        self.OUTLIER_MAGNITUDE = sys_params.get("outlier_magnitude", 0.3) * avg_dim

        # Kalman Filter Parameters
        self.KF_Q_PARAMETER = sys_params.get("kf_q_parameter", 20.0)

        # RANSAC Parameters
        self.RANSAC_ITERATIONS = 100
        self.RANSAC_THRESHOLD = sys_params.get("ransac_threshold", 0.15)  # meters

        # Accelerometer parameters
        self.ACCEL_NOISE_STD_DEV = 0.05
        self.ACCEL_BIAS_STD = 0.3
        self.ACCEL_BIAS_WALK_STD = 0.02

        # Data structures (Omitted for brevity, same as original)
        self.frames = []
        self.ransac_errors = []
        self.kf_errors = []
        self.true_biases_x = []
        self.true_biases_y = []
        self.est_biases_x = []
        self.est_biases_y = []

        # Real-time data
        self.data_queue = queue.Queue()
        self.serial_reader = None
        self.latest_distances = {}
        # >>>>> NEW: Store quality metrics and define filter parameters
        self.latest_quality = {}  # Stores (rssi, samples)
        self.ENABLE_MEDIAN_FILTER = sys_params.get("enable_median_filter", True)
        self.MEDIAN_FILTER_WINDOW = sys_params.get(
            "median_filter_window", 5
        )  # Tune this window size (e.g., 3, 5, 7)
        self.DATA_STALENESS_THRESHOLD = sys_params.get(
            "data_staleness_threshold", 1.0
        )  # Seconds
        # <<<<< End NEW
        self.anchor_data_history = {
            name: {"distances": [], "samples": [], "rssi": [], "timestamps": []}
            for name in self.anchors.keys()
        }

        # >>>>> NEW: Anchor Health Monitoring
        self.anchor_stats = {
            name: {"total_measurements": 0, "inlier_count": 0}
            for name in self.anchors.keys()
        }
        self.ENABLE_ANCHOR_HEALTH = sys_params.get("enable_anchor_health", True)
        self.MIN_INLIER_RATE = sys_params.get(
            "min_inlier_rate", 0.5
        )  # Minimum acceptable inlier rate (Tune this)
        self.HEALTH_WINDOW_SIZE = sys_params.get(
            "health_window_size", 50
        )  # Window size for health check
        self.unhealthy_anchors = set()

        # WLS Parameters
        self.ENABLE_WLS = sys_params.get("enable_wls", True)
        self.RSSI_MIN = sys_params.get("rssi_min", -90.0)
        self.RSSI_MAX = sys_params.get("rssi_max", -40.0)
        self.SAMPLES_MIN = sys_params.get("samples_min", 10)
        self.SAMPLES_MAX = sys_params.get("samples_max", 100)

        # Measurement Gating Parameters
        self.ENABLE_MEASUREMENT_GATING = sys_params.get(
            "enable_measurement_gating", True
        )
        self.GATING_THRESHOLD = sys_params.get("gating_threshold", 9.21)

        # Asymmetric R Adjustment Parameters
        self.ENABLE_ASYMMETRIC_R = sys_params.get("enable_asymmetric_r", True)
        self.R_TOWARDS_FACTOR = sys_params.get("r_towards_factor", 1.0)
        self.R_AWAY_FACTOR = sys_params.get("r_away_factor", 3.0)
        self.VELOCITY_THRESHOLD = sys_params.get("velocity_threshold", 0.1)

        # Dynamic R Bounds
        self.ENABLE_DYNAMIC_R = sys_params.get("enable_dynamic_r", True)
        self.MIN_R_VARIANCE = sys_params.get("min_r_variance", 0.0025)  # (0.05)^2
        self.MAX_R_VARIANCE = sys_params.get("max_r_variance", 2.25)  # (1.5)^2

        # Display Options
        self.SHOW_RANSAC_ESTIMATE = sys_params.get("show_ransac_estimate", True)
        self.SHOW_KF_ESTIMATE = sys_params.get("show_kf_estimate", True)
        self.SHOW_COORDINATES = sys_params.get("show_coordinates", True)
        # <<<<< End NEW

        # Simulation state initialization
        self.current_pos = np.array(
            [(self.X_MIN + self.X_MAX) / 2, (self.Y_MIN + self.Y_MAX) / 2]
        )
        self.last_pos_sim = np.copy(self.current_pos)
        self.last_vel_sim = np.zeros(2)
        self.true_bias = (
            np.random.normal(0, self.ACCEL_BIAS_STD, 2)
            if self.use_accelerometer
            else np.zeros(2)
        )
        self.target_pos = np.copy(self.current_pos)
        self.is_moving = False
        self.wait_timer = 0

        # Initialize last update time for dynamic dt calculation
        self.last_update_time = time.time()

        # Initialize restart flag for back to configuration functionality
        self.restart_config = False

        # Initialize logging functionality
        self.is_logging = False
        self.logging_anchors = []  # List of anchor names to log
        self.real_coordinates = [0.0, 0.0]  # Real X, Y coordinates
        self.log_file = None
        self.log_writer = None
        self.log_start_time = None
        self.log_duration_seconds = None  # None for unlimited duration
        self.logged_data = []  # Buffer for logging data
        self.error_stats = {}  # Store error statistics for summary

        # Initialize logging preferences (default to all enabled)
        self.log_include_rssi = True
        self.log_include_samples = True
        self.log_include_real_distances = True
        self.log_include_errors = True

        # Initialize Kalman filter
        if self.use_kalman_filter:
            self.kf = KalmanFilterCAB(
                dt=self.dt,
                std_jerk=self.KF_Q_PARAMETER,
                std_bias_walk=self.ACCEL_BIAS_WALK_STD,
                std_meas_pos=0.2,  # Fixed measurement noise standard deviation (was sqrt(R_BASE))
                std_meas_accel=self.ACCEL_NOISE_STD_DEV
                if self.use_accelerometer
                else None,
                use_accelerometer=self.use_accelerometer,
            )
            self.kf.x[:2] = self.current_pos.reshape(2, 1)
        else:
            self.kf = None

        self.setup_plots()

        if config["mode"] == "realtime":
            self.setup_real_time_mode()

    def setup_real_time_mode(self):
        """Setup real-time data reading"""
        if self.config["com_ports"]:
            self.serial_reader = SerialDataReader(
                self.config["com_ports"], self.data_queue
            )
            self.serial_reader.start()

    def setup_plots(self):
        """Setup matplotlib plots"""
        # (Figure creation and layout logic omitted for brevity, similar to original but adjusts for slider)

        # Adjust figure size and layout to accommodate the slider at the bottom
        fig_height = 9.5 if self.use_accelerometer else 7.5
        self.fig = plt.figure(figsize=(16, fig_height), facecolor="white")

        # Layout definition (using subplot2grid)
        if self.config["mode"] == "simulation":
            if self.use_accelerometer:
                self.ax_main = plt.subplot2grid((2, 2), (0, 0), rowspan=2)
                self.ax_error = plt.subplot2grid((2, 2), (0, 1))
                self.ax_bias = plt.subplot2grid((2, 2), (1, 1))
            else:
                self.ax_main = plt.subplot2grid((1, 2), (0, 0))
                self.ax_error = plt.subplot2grid((1, 2), (0, 1))
                self.ax_bias = None
            self.ax_anchor_data = None
        else:
            # Real-time mode layout
            if self.use_accelerometer:  # Should be false in current design for realtime
                self.ax_main = plt.subplot2grid((2, 2), (0, 0), rowspan=2)
                self.ax_anchor_data = plt.subplot2grid((2, 2), (0, 1))
                self.ax_bias = plt.subplot2grid((2, 2), (1, 1))
            else:
                self.ax_main = plt.subplot2grid((1, 2), (0, 0))
                self.ax_anchor_data = plt.subplot2grid((1, 2), (0, 1))
                self.ax_bias = None
            self.ax_error = None

        # Apply tight layout, reserving space for the slider
        plt.tight_layout(pad=3.0, rect=(0, 0.10, 1, 1))

        # Main plot setup
        self.ax_main.set_title(
            "Live Position Tracking", fontsize=13, fontweight="bold", pad=15
        )
        self.ax_main.set_xlabel("X Position (m)", fontsize=11, fontweight="medium")
        self.ax_main.set_ylabel("Y Position (m)", fontsize=11, fontweight="medium")
        self.ax_main.set_xlim(self.X_MIN, self.X_MAX)
        self.ax_main.set_ylim(self.Y_MIN, self.Y_MAX)
        self.ax_main.set_aspect("equal")

        # Enhanced grid styling
        self.ax_main.grid(True, alpha=0.3, linestyle="--", linewidth=0.8)
        self.ax_main.set_axisbelow(True)

        # Define modern, accessible colors
        colors = sns.color_palette("colorblind", n_colors=6)
        true_color = colors[2]  # Green
        ransac_color = colors[3]  # Red/Orange
        kf_color = colors[0]  # Blue
        anchor_color = colors[4]  # Purple/Dark

        # Plot elements with enhanced visual appeal
        if self.config["mode"] == "simulation":
            (self.true_pos_plot,) = self.ax_main.plot(
                [],
                [],
                marker="*",
                color=true_color,
                ms=18,
                markeredgewidth=1.5,
                markeredgecolor="white",
                label="✓ True Position",
                linewidth=0,
                markerfacecolor=true_color,
                alpha=0.9,
            )
        else:
            # Initialize as None for real-time mode
            self.true_pos_plot = None

        (self.ransac_pos_plot,) = self.ax_main.plot(
            [],
            [],
            marker="X",
            color=ransac_color,
            ms=14,
            markeredgewidth=2.5,
            label="RANSAC Estimate",
            linewidth=0,
            alpha=0.85,
            visible=self.SHOW_RANSAC_ESTIMATE,
        )

        if self.use_kalman_filter:
            (self.kf_pos_plot,) = self.ax_main.plot(
                [],
                [],
                marker="o",
                color=kf_color,
                ms=12,
                markeredgewidth=2,
                markeredgecolor="white",
                label="Kalman Filter",
                linewidth=0,
                markerfacecolor=kf_color,
                alpha=0.9,
                visible=self.SHOW_KF_ESTIMATE,
            )
        else:
            self.kf_pos_plot = None

        # Enhanced anchor visualization
        for key, pos in self.anchors.items():
            self.ax_main.scatter(
                pos[0],
                pos[1],
                color=anchor_color,
                s=200,
                marker="s",
                edgecolors="white",
                linewidths=2,
                alpha=0.8,
                zorder=10,
            )
            self.ax_main.annotate(
                f"⚓ {key}",
                xy=(pos[0], pos[1]),
                xytext=(5, 5),
                textcoords="offset points",
                fontsize=9,
                fontweight="bold",
                bbox=dict(
                    boxstyle="round,pad=0.3",
                    facecolor="white",
                    edgecolor=anchor_color,
                    alpha=0.8,
                ),
            )

        # Initialize distance circles for each anchor
        # Generate visually pleasing colors for the circles
        circle_colors = sns.color_palette("husl", n_colors=len(self.anchors))
        self.distance_circles = {}
        self.current_distances = {}

        for i, (anchor_name, anchor_pos) in enumerate(self.anchors.items()):
            # Create a circle with radius 0 (will be updated with actual distances)
            # Main circle outline
            circle = Circle(
                anchor_pos,
                0,
                fill=False,
                color=circle_colors[i],
                linewidth=2.5,
                alpha=0.8,
                linestyle="--",
                zorder=5,
            )
            self.ax_main.add_patch(circle)

            # Add a subtle filled circle for better visual effect
            filled_circle = Circle(
                anchor_pos, 0, fill=True, color=circle_colors[i], alpha=0.1, zorder=4
            )
            self.ax_main.add_patch(filled_circle)

            # Store both circles for updates
            self.distance_circles[anchor_name] = (circle, filled_circle)
            self.current_distances[anchor_name] = 0

        # Add a legend entry for distance circles
        from matplotlib.lines import Line2D

        circle_legend_line = Line2D(
            [0],
            [0],
            color="gray",
            linewidth=2.5,
            linestyle="--",
            alpha=0.7,
            label="📏 Distance Estimates",
        )

        # Enhanced legend
        # Create handles list for legend
        legend_handles = []
        if (
            self.config["mode"] == "simulation"
            and hasattr(self, "true_pos_plot")
            and self.true_pos_plot
        ):
            legend_handles.append(self.true_pos_plot)
        legend_handles.append(self.ransac_pos_plot)
        if self.use_kalman_filter and hasattr(self, "kf_pos_plot") and self.kf_pos_plot:
            legend_handles.append(self.kf_pos_plot)

        # Add a legend entry for distance circles
        from matplotlib.lines import Line2D

        circle_legend_line = Line2D(
            [0],
            [0],
            color="gray",
            linewidth=2.5,
            linestyle="--",
            alpha=0.7,
            label="📏 Distance Estimates",
        )
        legend_handles.append(circle_legend_line)

        legend = self.ax_main.legend(
            handles=legend_handles,
            loc="upper left",
            frameon=True,
            fancybox=True,
            shadow=True,
            framealpha=0.9,
            edgecolor="gray",
        )
        legend.get_frame().set_facecolor("white")

        # Setup error analysis plot (simulation mode only)
        if self.ax_error:
            self.ax_error.set_title(
                "📊 Live Error Analysis", fontsize=13, fontweight="bold", pad=15
            )
            self.ax_error.set_xlabel("Time Step", fontsize=11, fontweight="medium")
            self.ax_error.set_ylabel(
                "Position Error (m)", fontsize=11, fontweight="medium"
            )

            # Enhanced grid
            self.ax_error.grid(True, alpha=0.3, linestyle="--", linewidth=0.8)
            self.ax_error.set_axisbelow(True)

            # Enhanced error lines with better colors and styling
            (self.ransac_err_line,) = self.ax_error.plot(
                [],
                [],
                color=ransac_color,
                alpha=0.8,
                linewidth=2.5,
                label="📍 RANSAC Error",
                linestyle="-",
                marker="o",
                markersize=4,
                markevery=5,
            )
            if self.use_kalman_filter:
                (self.kf_err_line,) = self.ax_error.plot(
                    [],
                    [],
                    color=kf_color,
                    linewidth=3,
                    label="🎯 Kalman Filter Error",
                    linestyle="-",
                    alpha=0.9,
                )

            # Enhanced legend
            error_legend = self.ax_error.legend(
                loc="upper left",
                frameon=True,
                fancybox=True,
                shadow=True,
                framealpha=0.9,
                edgecolor="gray",
            )
            error_legend.get_frame().set_facecolor("white")

            # Enhanced RMSE text box
            self.rmse_text = self.ax_error.text(
                0.98,
                0.98,
                "",
                transform=self.ax_error.transAxes,
                ha="right",
                va="top",
                fontsize=9,
                fontweight="medium",
                bbox=dict(
                    boxstyle="round,pad=0.5",
                    facecolor="lightblue",
                    edgecolor="steelblue",
                    alpha=0.9,
                    linewidth=1.5,
                ),
            )
        else:
            # Initialize attributes for modes that don't have error plots
            self.ransac_err_line = None
            self.kf_err_line = None
            self.rmse_text = None

        # Setup anchor data plot (real-time mode only)
        if self.ax_anchor_data:
            self.setup_anchor_data_plot()
        else:
            # Initialize attributes for modes that don't have anchor data plots
            self.anchor_distance_lines = {}
            self.anchor_data_text = None

        # Bias estimation plot (only if using accelerometer) with enhanced styling
        if self.use_accelerometer and self.ax_bias:
            self.ax_bias.set_title(
                "🔧 Live Accelerometer Bias Estimation",
                fontsize=13,
                fontweight="bold",
                pad=15,
            )
            self.ax_bias.set_xlabel("Time Step", fontsize=11, fontweight="medium")
            self.ax_bias.set_ylabel("Bias (m/s²)", fontsize=11, fontweight="medium")

            # Enhanced grid
            self.ax_bias.grid(True, alpha=0.3, linestyle="--", linewidth=0.8)
            self.ax_bias.set_axisbelow(True)

            # Additional colors for bias plots
            bias_colors = sns.color_palette("husl", n_colors=4)

            if self.config["mode"] == "simulation":
                (self.true_bias_x_line,) = self.ax_bias.plot(
                    [],
                    [],
                    color=bias_colors[0],
                    alpha=0.7,
                    linewidth=2,
                    linestyle="--",
                    label="📏 True Bias X",
                )
                (self.true_bias_y_line,) = self.ax_bias.plot(
                    [],
                    [],
                    color=bias_colors[1],
                    alpha=0.7,
                    linewidth=2,
                    linestyle="--",
                    label="📏 True Bias Y",
                )

            if self.use_kalman_filter:
                (self.est_bias_x_line,) = self.ax_bias.plot(
                    [],
                    [],
                    color=bias_colors[2],
                    linewidth=3,
                    linestyle="-",
                    alpha=0.9,
                    label="🔍 Estimated Bias X",
                )
                (self.est_bias_y_line,) = self.ax_bias.plot(
                    [],
                    [],
                    color=bias_colors[3],
                    linewidth=3,
                    linestyle="-",
                    alpha=0.9,
                    label="🔍 Estimated Bias Y",
                )

            # Enhanced legend
            bias_legend = self.ax_bias.legend(
                loc="upper left",
                frameon=True,
                fancybox=True,
                shadow=True,
                framealpha=0.9,
                edgecolor="gray",
            )
            bias_legend.get_frame().set_facecolor("white")

        # Setup coordinate text display
        self.setup_coordinate_display()

        # Tweak 2: Add Sliders for dynamic parameter tuning
        self.setup_settings_button()
        self.setup_back_button()
        self.setup_logging_button()

    def setup_coordinate_display(self):
        """Setup coordinate text display."""
        if self.SHOW_COORDINATES:
            # Add coordinate text display in the upper right corner of the main plot
            self.coordinate_text = self.ax_main.text(
                0.98,
                0.98,
                "",
                transform=self.ax_main.transAxes,
                fontsize=9,
                verticalalignment="top",
                horizontalalignment="right",
                bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8),
                fontfamily="monospace",
                zorder=10,
            )
        else:
            self.coordinate_text = None

    def update_coordinate_display(self, ransac_estimate, kf_estimate):
        """Update coordinate text display with current estimates."""
        if (
            not self.SHOW_COORDINATES
            or not hasattr(self, "coordinate_text")
            or self.coordinate_text is None
        ):
            return

        text_lines = []

        # Add RANSAC coordinates if enabled and available
        if self.SHOW_RANSAC_ESTIMATE and ransac_estimate is not None:
            text_lines.append(
                f"🎯 RANSAC: ({ransac_estimate[0]:.3f}, {ransac_estimate[1]:.3f})"
            )

        # Add Kalman Filter coordinates if enabled and available
        if self.SHOW_KF_ESTIMATE and self.use_kalman_filter and kf_estimate is not None:
            text_lines.append(
                f"🧠 Kalman: ({kf_estimate[0]:.3f}, {kf_estimate[1]:.3f})"
            )

        # Update the text display
        if text_lines:
            self.coordinate_text.set_text("\n".join(text_lines))
            self.coordinate_text.set_visible(True)
        else:
            self.coordinate_text.set_visible(False)

    def setup_settings_button(self):
        """Setup the button to open the settings dialog."""
        ax_button = plt.axes((0.8, 0.01, 0.15, 0.04))
        self.settings_button = Button(
            ax_button, "Settings", color="lightgoldenrodyellow", hovercolor="0.975"
        )
        self.settings_button.on_clicked(self.open_settings_dialog)

    def setup_back_button(self):
        """Setup the button to go back to configuration."""
        ax_back_button = plt.axes((0.63, 0.01, 0.15, 0.04))
        self.back_button = Button(
            ax_back_button, "Back to Config", color="lightblue", hovercolor="0.975"
        )
        self.back_button.on_clicked(self.go_back_to_config)

    def setup_logging_button(self):
        """Setup the button to start/stop logging."""
        ax_logging_button = plt.axes((0.46, 0.01, 0.15, 0.04))
        self.logging_button = Button(
            ax_logging_button, "Start Logging", color="lightgreen", hovercolor="0.975"
        )
        self.logging_button.on_clicked(self.toggle_logging)

    def go_back_to_config(self, event):
        """Go back to configuration screen."""
        # Set a flag to indicate we want to go back to configuration
        self.restart_config = True

        # Close the current matplotlib window
        plt.close(self.fig)

        # Stop the animation if it's running
        if hasattr(self, "ani") and self.ani:
            self.ani.event_source.stop()

        # Stop serial reader if running
        if hasattr(self, "serial_reader") and self.serial_reader:
            self.serial_reader.stop()

    def toggle_logging(self, event):
        """Toggle logging on/off."""
        if not self.is_logging:
            # Start logging - open configuration dialog first
            self.open_logging_config_dialog()
        else:
            # Stop logging
            self.stop_logging()

    def open_logging_config_dialog(self):
        """Open a dialog to configure logging settings."""
        # Create logging config dialog
        self.logging_dialog = tk.Toplevel()
        self.logging_dialog.title("Configure Data Logging")
        self.logging_dialog.geometry("520x750")
        self.logging_dialog.configure(bg="#f0f0f0")
        self.logging_dialog.resizable(False, False)

        # Make dialog modal and bring to front
        self.logging_dialog.transient()
        self.logging_dialog.grab_set()
        self.logging_dialog.focus_set()

        main_frame = ttk.Frame(self.logging_dialog, padding="15")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Title
        title_label = ttk.Label(
            main_frame,
            text="📊 Data Logging Configuration",
            font=("Segoe UI", 14, "bold"),
            foreground="#2c3e50",
        )
        title_label.pack(pady=(0, 20))

        # Real coordinates input
        coords_frame = ttk.LabelFrame(
            main_frame, text="📍 Real Coordinates", padding="10"
        )
        coords_frame.pack(fill=tk.X, pady=(0, 15))

        coords_grid = ttk.Frame(coords_frame)
        coords_grid.pack(fill=tk.X)

        ttk.Label(coords_grid, text="Real X:").grid(
            row=0, column=0, sticky=tk.W, padx=(0, 5)
        )
        self.real_x_var = tk.DoubleVar(value=0.0)
        real_x_entry = ttk.Entry(coords_grid, textvariable=self.real_x_var, width=15)
        real_x_entry.grid(row=0, column=1, padx=(0, 20))

        ttk.Label(coords_grid, text="Real Y:").grid(
            row=0, column=2, sticky=tk.W, padx=(0, 5)
        )
        self.real_y_var = tk.DoubleVar(value=0.0)
        real_y_entry = ttk.Entry(coords_grid, textvariable=self.real_y_var, width=15)
        real_y_entry.grid(row=0, column=3)

        # Anchor selection
        anchor_frame = ttk.LabelFrame(
            main_frame, text="⚓ Anchor Selection", padding="10"
        )
        anchor_frame.pack(fill=tk.BOTH, expand=True, pady=(0, 15))

        ttk.Label(
            anchor_frame, text="Select anchors to log data from:", font=("Segoe UI", 9)
        ).pack(anchor=tk.W, pady=(0, 10))

        # Checkbox for each anchor
        self.anchor_vars = {}
        anchor_checkboxes_frame = ttk.Frame(anchor_frame)
        anchor_checkboxes_frame.pack(fill=tk.BOTH, expand=True)

        for i, anchor_name in enumerate(self.anchors.keys()):
            var = tk.BooleanVar(value=True)  # Default to all selected
            self.anchor_vars[anchor_name] = var

            cb = ttk.Checkbutton(
                anchor_checkboxes_frame, text=f"{anchor_name}", variable=var
            )
            cb.grid(row=i // 2, column=i % 2, sticky=tk.W, padx=(0, 20), pady=2)

        # Select/Deselect all buttons
        select_buttons_frame = ttk.Frame(anchor_frame)
        select_buttons_frame.pack(fill=tk.X, pady=(10, 0))

        ttk.Button(
            select_buttons_frame, text="Select All", command=self.select_all_anchors
        ).pack(side=tk.LEFT, padx=(0, 5))
        ttk.Button(
            select_buttons_frame, text="Deselect All", command=self.deselect_all_anchors
        ).pack(side=tk.LEFT)

        # Data inclusion options
        data_options_frame = ttk.LabelFrame(
            main_frame, text="📋 Data to Include", padding="10"
        )
        data_options_frame.pack(fill=tk.X, pady=(0, 15))

        # Always include basic distance measurements
        ttk.Label(
            data_options_frame,
            text="✓ Distance measurements (always included)",
            font=("Segoe UI", 9),
            foreground="green",
        ).pack(anchor=tk.W, pady=(0, 5))

        # Optional data checkboxes
        self.include_rssi = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            data_options_frame,
            text="📶 RSSI data (signal strength)",
            variable=self.include_rssi,
        ).pack(anchor=tk.W, pady=1)

        self.include_samples = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            data_options_frame,
            text="🔢 Sample count data (measurement quality)",
            variable=self.include_samples,
        ).pack(anchor=tk.W, pady=1)

        self.include_real_distances = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            data_options_frame,
            text="📏 Real distances (calculated from coordinates)",
            variable=self.include_real_distances,
        ).pack(anchor=tk.W, pady=1)

        self.include_errors = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            data_options_frame,
            text="📊 Error calculations (estimated vs real)",
            variable=self.include_errors,
        ).pack(anchor=tk.W, pady=1)

        # Data option buttons
        data_buttons_frame = ttk.Frame(data_options_frame)
        data_buttons_frame.pack(fill=tk.X, pady=(10, 0))

        ttk.Button(
            data_buttons_frame, text="All Data", command=self.select_all_data_options
        ).pack(side=tk.LEFT, padx=(0, 5))
        ttk.Button(
            data_buttons_frame, text="Basic Only", command=self.select_basic_data_only
        ).pack(side=tk.LEFT)

        # Duration settings
        duration_frame = ttk.LabelFrame(
            main_frame, text="⏱️ Duration Settings", padding="10"
        )
        duration_frame.pack(fill=tk.X, pady=(0, 15))

        self.duration_unlimited = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            duration_frame,
            text="Log indefinitely (until Stop pressed)",
            variable=self.duration_unlimited,
            command=self.on_duration_mode_change,
        ).pack(anchor=tk.W)

        self.duration_frame_timed = ttk.Frame(duration_frame)
        self.duration_frame_timed.pack(fill=tk.X, pady=(5, 0))

        ttk.Label(self.duration_frame_timed, text="Log for:").pack(side=tk.LEFT)
        self.log_duration_var = tk.IntVar(value=60)
        duration_spinbox = ttk.Spinbox(
            self.duration_frame_timed,
            from_=10,
            to=3600,
            increment=10,
            textvariable=self.log_duration_var,
            width=8,
            state="disabled",
        )
        duration_spinbox.pack(side=tk.LEFT, padx=(5, 0))
        ttk.Label(self.duration_frame_timed, text="seconds").pack(
            side=tk.LEFT, padx=(5, 0)
        )

        # Store reference to spinbox for enabling/disabling
        self.duration_spinbox = duration_spinbox

        # Button frame
        button_frame = ttk.Frame(main_frame)
        button_frame.pack(fill=tk.X, pady=(15, 0))

        ttk.Button(
            button_frame,
            text="🚀 Start Logging",
            command=self.start_logging,
            style="Action.TButton",
        ).pack(side=tk.LEFT, padx=(0, 10), ipadx=10, ipady=5)

        ttk.Button(
            button_frame, text="❌ Cancel", command=self.logging_dialog.destroy
        ).pack(side=tk.LEFT, ipadx=10, ipady=5)

    def select_all_anchors(self):
        """Select all anchors for logging."""
        for var in self.anchor_vars.values():
            var.set(True)

    def deselect_all_anchors(self):
        """Deselect all anchors for logging."""
        for var in self.anchor_vars.values():
            var.set(False)

    def on_duration_mode_change(self):
        """Handle duration mode change."""
        if self.duration_unlimited.get():
            self.duration_spinbox.configure(state="disabled")
        else:
            self.duration_spinbox.configure(state="normal")

    def select_all_data_options(self):
        """Select all data options for comprehensive logging."""
        self.include_rssi.set(True)
        self.include_samples.set(True)
        self.include_real_distances.set(True)
        self.include_errors.set(True)

    def select_basic_data_only(self):
        """Select only basic distance measurements."""
        self.include_rssi.set(False)
        self.include_samples.set(False)
        self.include_real_distances.set(False)
        self.include_errors.set(False)

    def start_logging(self):
        """Start the logging process."""
        try:
            # Get selected anchors
            selected_anchors = [
                name for name, var in self.anchor_vars.items() if var.get()
            ]

            if not selected_anchors:
                messagebox.showerror(
                    "No Anchors Selected",
                    "Please select at least one anchor for logging.",
                )
                return

            # Get real coordinates
            self.real_coordinates = [self.real_x_var.get(), self.real_y_var.get()]

            # Get duration
            if self.duration_unlimited.get():
                self.log_duration_seconds = None
            else:
                self.log_duration_seconds = self.log_duration_var.get()

            # Get data inclusion preferences
            self.log_include_rssi = self.include_rssi.get()
            self.log_include_samples = self.include_samples.get()
            self.log_include_real_distances = self.include_real_distances.get()
            self.log_include_errors = self.include_errors.get()

            # Set up logging
            self.logging_anchors = selected_anchors
            self.log_start_time = time.time()

            # Create log file with timestamp
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            log_filename = f"distance_log_{timestamp}.csv"

            self.log_file = open(log_filename, "w", newline="")
            self.log_writer = csv.writer(self.log_file)

            # Write CSV header with real coordinate and distance information
            self.log_writer.writerow(["# Distance Logging Session"])
            self.log_writer.writerow(
                [f"# Started: {time.strftime('%Y-%m-%d %H:%M:%S')}"]
            )
            self.log_writer.writerow(["# Real Coordinates and Distances:"])
            self.log_writer.writerow(
                [
                    f"# Reflector Position: ({self.real_coordinates[0]:.4f}, {self.real_coordinates[1]:.4f})"
                ]
            )
            self.log_writer.writerow(["#"])

            # Calculate and log real distances for each selected anchor
            reflector_pos = np.array(self.real_coordinates)
            for anchor in selected_anchors:
                if anchor in self.anchors:
                    anchor_pos = self.anchors[anchor]
                    real_distance = np.linalg.norm(reflector_pos - anchor_pos)
                    self.log_writer.writerow(
                        [
                            f"# {anchor} Position: ({anchor_pos[0]:.4f}, {anchor_pos[1]:.4f}), Real Distance: {real_distance:.4f}m"
                        ]
                    )

            self.log_writer.writerow(["#"])
            self.log_writer.writerow(["# Data columns below:"])

            # Write actual data header (conditional based on user preferences)
            header = ["timestamp", "elapsed_seconds", "real_x", "real_y"]
            for anchor in selected_anchors:
                # Always include distance
                anchor_columns = [f"{anchor}_distance"]

                # Add optional columns based on user preferences
                if self.log_include_rssi:
                    anchor_columns.append(f"{anchor}_rssi")
                if self.log_include_samples:
                    anchor_columns.append(f"{anchor}_samples")
                if self.log_include_real_distances:
                    anchor_columns.append(f"{anchor}_real_distance")
                if self.log_include_errors:
                    anchor_columns.append(f"{anchor}_error")

                header.extend(anchor_columns)
            self.log_writer.writerow(header)

            self.is_logging = True

            # Initialize error statistics tracking
            self.error_stats = {anchor: [] for anchor in selected_anchors}

            # Update button appearance
            self.logging_button.label.set_text("Stop Logging")
            self.logging_button.color = "lightcoral"

            # Close dialog
            self.logging_dialog.destroy()

            print(f"Started logging to {log_filename}")
            print(f"Logging anchors: {', '.join(selected_anchors)}")
            print(
                f"Real coordinates: ({self.real_coordinates[0]:.2f}, {self.real_coordinates[1]:.2f})"
            )

            # Show which data types are being logged
            data_types = ["distances"]
            if self.log_include_rssi:
                data_types.append("RSSI")
            if self.log_include_samples:
                data_types.append("samples")
            if self.log_include_real_distances:
                data_types.append("real distances")
            if self.log_include_errors:
                data_types.append("errors")
            print(f"Data types: {', '.join(data_types)}")

            if self.log_duration_seconds:
                print(f"Logging duration: {self.log_duration_seconds} seconds")
            else:
                print("Logging duration: unlimited")

        except Exception as e:
            messagebox.showerror("Logging Error", f"Failed to start logging: {str(e)}")

    def stop_logging(self):
        """Stop the logging process."""
        try:
            if self.log_file and self.log_writer:
                # Write summary statistics
                self.log_writer.writerow(["#"])
                self.log_writer.writerow(["# LOGGING SESSION SUMMARY"])
                self.log_writer.writerow(
                    [f"# Session ended: {time.strftime('%Y-%m-%d %H:%M:%S')}"]
                )

                if self.log_start_time:
                    total_duration = time.time() - self.log_start_time
                    self.log_writer.writerow(
                        [f"# Total duration: {total_duration:.1f} seconds"]
                    )

                # Only write accuracy statistics if error calculation was enabled
                if hasattr(self, "log_include_errors") and self.log_include_errors:
                    self.log_writer.writerow(["# Distance Measurement Accuracy:"])

                    # Calculate and write statistics for each anchor
                    for anchor, errors in self.error_stats.items():
                        if errors:  # Only if we have error data
                            errors_array = np.array(errors)
                            mean_error = np.mean(errors_array)
                            std_error = np.std(errors_array)
                            rmse = np.sqrt(np.mean(errors_array**2))
                            min_error = np.min(errors_array)
                            max_error = np.max(errors_array)

                            self.log_writer.writerow(
                                [
                                    f"# {anchor}: Mean={mean_error:.4f}m, Std={std_error:.4f}m, RMSE={rmse:.4f}m, Min={min_error:.4f}m, Max={max_error:.4f}m"
                                ]
                            )
                        else:
                            self.log_writer.writerow(
                                [f"# {anchor}: No valid measurements recorded"]
                            )

                    # Calculate overall statistics across all anchors
                    all_errors = []
                    for errors in self.error_stats.values():
                        all_errors.extend(errors)

                    if all_errors:
                        all_errors_array = np.array(all_errors)
                        overall_mean = np.mean(all_errors_array)
                        overall_std = np.std(all_errors_array)
                        overall_rmse = np.sqrt(np.mean(all_errors_array**2))

                        self.log_writer.writerow(["#"])
                        self.log_writer.writerow(
                            [
                                f"# OVERALL: Mean={overall_mean:.4f}m, Std={overall_std:.4f}m, RMSE={overall_rmse:.4f}m"
                            ]
                        )
                        self.log_writer.writerow(
                            [f"# Total measurements: {len(all_errors)}"]
                        )

            if self.log_file:
                self.log_file.close()
                self.log_file = None
                self.log_writer = None

            self.is_logging = False
            self.log_start_time = None
            self.error_stats = {}

            # Update button appearance
            self.logging_button.label.set_text("Start Logging")
            self.logging_button.color = "lightgreen"

            print("Logging stopped.")

        except Exception as e:
            print(f"Error stopping logging: {str(e)}")

    def log_data_point(self):
        """Log current data point if logging is active."""
        if not self.is_logging or not self.log_writer:
            return

        try:
            current_time = time.time()
            elapsed_seconds = current_time - self.log_start_time

            # Check if duration has been exceeded
            if (
                self.log_duration_seconds is not None
                and elapsed_seconds >= self.log_duration_seconds
            ):
                self.stop_logging()
                return

            # Prepare data row
            timestamp_str = time.strftime(
                "%Y-%m-%d %H:%M:%S", time.localtime(current_time)
            )
            row = [
                timestamp_str,
                f"{elapsed_seconds:.2f}",
                f"{self.real_coordinates[0]:.4f}",
                f"{self.real_coordinates[1]:.4f}",
            ]

            # Add anchor data
            reflector_pos = np.array(self.real_coordinates)

            for anchor in self.logging_anchors:
                if anchor in self.latest_distances:
                    distance = self.latest_distances[anchor]
                    # Get quality data if available
                    if anchor in self.latest_quality:
                        rssi, samples = self.latest_quality[anchor]
                    else:
                        rssi, samples = -999, 0  # Indicate missing data

                    # Calculate real distance and error
                    if anchor in self.anchors:
                        anchor_pos = self.anchors[anchor]
                        real_distance = np.linalg.norm(reflector_pos - anchor_pos)
                        error = (
                            distance - real_distance
                        )  # Positive = overestimate, Negative = underestimate

                        # Collect error statistics if error calculation is enabled
                        if (
                            self.log_include_errors
                            and anchor in self.error_stats
                            and not np.isnan(error)
                        ):
                            self.error_stats[anchor].append(error)

                        # Build row data based on user preferences
                        anchor_data = [f"{distance:.4f}"]  # Always include distance

                        if self.log_include_rssi:
                            anchor_data.append(f"{rssi:.1f}")
                        if self.log_include_samples:
                            anchor_data.append(f"{samples}")
                        if self.log_include_real_distances:
                            anchor_data.append(f"{real_distance:.4f}")
                        if self.log_include_errors:
                            anchor_data.append(f"{error:.4f}")

                        row.extend(anchor_data)
                    else:
                        # No anchor position data available
                        anchor_data = [f"{distance:.4f}"]  # Always include distance

                        if self.log_include_rssi:
                            anchor_data.append(f"{rssi:.1f}")
                        if self.log_include_samples:
                            anchor_data.append(f"{samples}")
                        if self.log_include_real_distances:
                            anchor_data.append("N/A")
                        if self.log_include_errors:
                            anchor_data.append("N/A")

                        row.extend(anchor_data)
                else:
                    # No distance data available for this anchor
                    anchor_data = ["N/A"]  # Distance

                    if self.log_include_rssi:
                        anchor_data.append("N/A")
                    if self.log_include_samples:
                        anchor_data.append("N/A")
                    if self.log_include_real_distances:
                        anchor_data.append("N/A")
                    if self.log_include_errors:
                        anchor_data.append("N/A")

                    row.extend(anchor_data)

            self.log_writer.writerow(row)
            self.log_file.flush()  # Ensure data is written to disk

        except Exception as e:
            print(f"Error logging data point: {str(e)}")

    def open_settings_dialog(self, event):
        """Open a dialog to configure settings."""
        # Create a new Toplevel window. It will be managed by the existing Tkinter instance if one exists,
        # or it will create its own.
        self.settings_dialog = tk.Toplevel()
        self.settings_dialog.title("Multilateration Settings")
        self.settings_dialog.geometry("300x200")
        self.settings_dialog.configure(bg="#f0f0f0")

        # Store initial values for reset
        self.initial_q = self.KF_Q_PARAMETER
        self.initial_ransac = self.RANSAC_THRESHOLD

        # Create variables for the settings
        self.q_param_var = tk.DoubleVar(value=self.KF_Q_PARAMETER)
        self.ransac_thresh_var = tk.DoubleVar(value=self.RANSAC_THRESHOLD)

        # Create the widgets with better layout
        main_frame = ttk.Frame(self.settings_dialog, padding="10")
        main_frame.pack(fill=tk.BOTH, expand=True)

        ttk.Label(main_frame, text="Kalman Filter Q:").grid(
            row=0, column=0, sticky=tk.W, pady=5
        )
        ttk.Entry(main_frame, textvariable=self.q_param_var, width=15).grid(
            row=0, column=1, sticky=tk.W, pady=5
        )

        ttk.Label(main_frame, text="RANSAC Threshold:").grid(
            row=1, column=0, sticky=tk.W, pady=5
        )
        ttk.Entry(main_frame, textvariable=self.ransac_thresh_var, width=15).grid(
            row=1, column=1, sticky=tk.W, pady=5
        )

        # Buttons
        button_frame = ttk.Frame(main_frame)
        button_frame.grid(row=2, column=0, columnspan=2, pady=20)

        ttk.Button(button_frame, text="Apply", command=self.apply_settings).pack(
            side=tk.LEFT, padx=5
        )
        ttk.Button(button_frame, text="Reset", command=self.reset_settings).pack(
            side=tk.LEFT, padx=5
        )
        ttk.Button(
            button_frame, text="Cancel", command=self.settings_dialog.destroy
        ).pack(side=tk.LEFT, padx=5)

    def apply_settings(self):
        """Apply the new settings."""
        try:
            self.KF_Q_PARAMETER = self.q_param_var.get()
            self.RANSAC_THRESHOLD = self.ransac_thresh_var.get()
            if self.kf:
                self.kf.set_Q_parameter(self.KF_Q_PARAMETER)
            print(
                f"Applied new settings: Q={self.KF_Q_PARAMETER}, RANSAC Thresh={self.RANSAC_THRESHOLD}"
            )
            self.settings_dialog.destroy()
        except tk.TclError as e:
            messagebox.showerror(
                "Invalid Input",
                f"Please enter valid numbers.\n{e}",
                parent=self.settings_dialog,
            )

    def reset_settings(self):
        """Reset settings to their initial values from when the dialog was opened."""
        if hasattr(self, "initial_q") and hasattr(self, "initial_ransac"):
            self.q_param_var.set(self.initial_q)
            self.ransac_thresh_var.set(self.initial_ransac)

    def setup_anchor_data_plot(self):
        """Setup the anchor data visualization for real-time mode"""
        self.ax_anchor_data.set_title(
            "📡 Live Anchor Data Streams", fontsize=13, fontweight="bold", pad=15
        )
        self.ax_anchor_data.set_xlabel("Time Step", fontsize=11, fontweight="medium")
        self.ax_anchor_data.set_ylabel("Distance (m)", fontsize=11, fontweight="medium")

        # Set y-axis limits for reasonable distance range (0-10 meters)
        self.ax_anchor_data.set_ylim(0, 10)

        # Enhanced grid
        self.ax_anchor_data.grid(True, alpha=0.3, linestyle="--", linewidth=0.8)
        self.ax_anchor_data.set_axisbelow(True)

        # Create lines for each anchor's distance data
        anchor_colors = sns.color_palette("husl", n_colors=len(self.anchors))
        self.anchor_distance_lines = {}

        for i, anchor_name in enumerate(self.anchors.keys()):
            (line,) = self.ax_anchor_data.plot(
                [],
                [],
                color=anchor_colors[i],
                alpha=0.8,
                linewidth=2.5,
                label=f"📍 {anchor_name}",
                marker="o",
                markersize=4,
                markevery=3,
            )
            self.anchor_distance_lines[anchor_name] = line

        # Enhanced legend
        anchor_legend = self.ax_anchor_data.legend(
            loc="upper left",
            frameon=True,
            fancybox=True,
            shadow=True,
            framealpha=0.9,
            edgecolor="gray",
        )
        anchor_legend.get_frame().set_facecolor("white")

        # Data quality text box
        self.anchor_data_text = self.ax_anchor_data.text(
            0.98,
            0.98,
            "",
            transform=self.ax_anchor_data.transAxes,
            ha="right",
            va="top",
            fontsize=9,
            fontweight="medium",
            bbox=dict(
                boxstyle="round,pad=0.5",
                facecolor="lightgreen",
                edgecolor="forestgreen",
                alpha=0.9,
                linewidth=1.5,
            ),
        )

    def update_anchor_data_plot(self):
        """Update the anchor data visualization with latest data"""
        if not hasattr(self, "anchor_distance_lines"):
            return

        # Update distance lines for each anchor
        for anchor_name, line in self.anchor_distance_lines.items():
            history = self.anchor_data_history.get(anchor_name, {})
            distances = history.get("distances", [])

            if distances:
                # Use index as x-axis (time steps)
                x_data = list(range(len(distances)))
                line.set_data(x_data, distances)

        # Update axis limits but preserve y-axis cap
        self.ax_anchor_data.relim()
        self.ax_anchor_data.autoscale_view(
            scalex=True, scaley=False
        )  # Only autoscale x-axis

        # Ensure y-axis stays within reasonable range
        self.ax_anchor_data.set_ylim(0, 10)

        # Update data quality information
        anchor_info = []
        for anchor_name in self.anchors.keys():
            history = self.anchor_data_history.get(anchor_name, {})
            latest_distance = (
                history.get("distances", [None])[-1]
                if history.get("distances")
                else None
            )
            latest_samples = (
                history.get("samples", [None])[-1] if history.get("samples") else None
            )
            latest_rssi = (
                history.get("rssi", [None])[-1] if history.get("rssi") else None
            )

            if latest_distance is not None:
                info_line = f"📍 {anchor_name}: {latest_distance:.3f}m"
                if latest_samples is not None:
                    info_line += f" | 📊 {latest_samples} samples"
                if latest_rssi is not None:
                    info_line += f" | 📶 {latest_rssi} dBm"
                anchor_info.append(info_line)
            else:
                anchor_info.append(f"📍 {anchor_name}: No data")

        # Format the information text
        if anchor_info:
            data_text = (
                "📡 Live Anchor Status\n" + "━" * 22 + "\n" + "\n".join(anchor_info)
            )
        else:
            data_text = "📡 Waiting for data..."

        self.anchor_data_text.set_text(data_text)

    # --- Geometry and Trilateration Helpers (Optimized RANSAC) ---

    def calculate_weights(self, rssi_values, samples_values):
        """Convert RSSI and Samples into weights (higher is better)."""
        # Normalize RSSI using configured ranges
        rssi_norm = np.clip(
            (rssi_values - self.RSSI_MIN) / (self.RSSI_MAX - self.RSSI_MIN), 0.1, 1.0
        )

        # Normalize Samples using configured ranges
        samples_values = np.maximum(samples_values, 1)  # Handle missing samples
        samples_norm = np.clip(
            (samples_values - self.SAMPLES_MIN) / (self.SAMPLES_MAX - self.SAMPLES_MIN),
            0.1,
            1.0,
        )

        # Combine weights (product emphasizes measurements good in both aspects)
        weights = rssi_norm * samples_norm
        return weights

    def trilaterate_wls(self, anchors, distances, weights):
        """Perform trilateration using Weighted Least Squares (WLS)."""
        if len(anchors) < 3 or len(weights) != len(anchors):
            return self.trilaterate_lstsq(anchors, distances)  # Fallback

        P0 = anchors[0]
        r0_sq = distances[0] ** 2
        w0 = weights[0]
        A = []
        b = []
        W_eq_list = []

        for i in range(1, len(anchors)):
            Pi = anchors[i]
            ri_sq = distances[i] ** 2
            wi = weights[i]
            A.append(2 * (Pi - P0))
            b.append(r0_sq - ri_sq - np.linalg.norm(P0) ** 2 + np.linalg.norm(Pi) ** 2)

            # Weight of the equation (Heuristic: Harmonic mean)
            if w0 > 0 and wi > 0:
                W_eq_list.append((w0 * wi) / (w0 + wi))
            else:
                W_eq_list.append(min(w0, wi) if min(w0, wi) > 0 else 1e-6)

        A = np.array(A)
        b = np.array(b)

        # Normalize weights for numerical stability
        if np.sum(W_eq_list) == 0:
            W_eq = np.eye(len(W_eq_list))
        else:
            W_eq = np.diag(W_eq_list / np.sum(W_eq_list) * len(W_eq_list))

        try:
            # Solve WLS using the Normal Equations: x = (A.T @ W_eq @ A)^-1 @ A.T @ W_eq @ b
            # Use np.linalg.lstsq for robust solution
            ATW = A.T @ W_eq
            ATWA = ATW @ A
            ATWb = ATW @ b
            position, _, _, _ = np.linalg.lstsq(ATWA, ATWb, rcond=None)
            return position
        except np.linalg.LinAlgError:
            return self.trilaterate_lstsq(anchors, distances)  # Fallback

    def trilaterate_lstsq(self, anchors, distances):
        """
        Perform trilateration using Linear Least Squares (LSTSQ). Robust method.
        Assumes 2D localization (z=0).
        """
        if len(anchors) < 3:
            return None

        # LSTSQ formulation: Ax = b
        # Based on the difference of squared distance equations.

        P0 = anchors[0]
        r0_sq = distances[0] ** 2

        A = []
        b = []

        for i in range(1, len(anchors)):
            Pi = anchors[i]
            ri_sq = distances[i] ** 2

            # 2 * (Pi - P0)
            A.append(2 * (Pi - P0))

            # r0^2 - ri^2 - ||P0||^2 + ||Pi||^2
            b.append(r0_sq - ri_sq - np.linalg.norm(P0) ** 2 + np.linalg.norm(Pi) ** 2)

        A = np.array(A)
        b = np.array(b)

        try:
            # Solve Ax = b
            position, residuals, rank, s = np.linalg.lstsq(A, b, rcond=None)
            return position
        except np.linalg.LinAlgError:
            return None

    def count_inliers(self, p, anchor_indices, distances, threshold):
        """Count inliers for RANSAC algorithm against all available measurements"""
        relevant_anchors = self.anchor_array[anchor_indices]
        predicted_distances = np.linalg.norm(relevant_anchors - p, axis=1)
        errors = np.abs(predicted_distances - distances)
        inliers = np.sum(errors <= threshold)
        return inliers, errors

    def get_real_time_distances(self):
        """Get latest distance measurements, apply median filter, and retrieve quality metrics."""
        current_time = time.time()

        # Process all queued data
        while not self.data_queue.empty():
            try:
                data = self.data_queue.get_nowait()
                anchor_name = data["anchor_name"]
                raw_distance = data["distance"]
                rssi = data.get("rssi", 0)
                samples = data.get("samples", 0)

                # Store historical data (used for plotting AND median filtering)
                if anchor_name in self.anchor_data_history:
                    history = self.anchor_data_history[anchor_name]
                    history["distances"].append(raw_distance)
                    history["samples"].append(samples)
                    history["rssi"].append(rssi)
                    history["timestamps"].append(current_time)

                    # Limit history
                    max_points = 100
                    history_len = max(max_points, self.MEDIAN_FILTER_WINDOW)
                    if len(history["distances"]) > history_len:
                        for key in history.keys():
                            history[key] = history[key][-history_len:]

                # Store latest quality metrics
                self.latest_quality[anchor_name] = (rssi, samples)

            except queue.Empty:
                break

        # Apply Median Filter and prepare data for localization
        anchor_name_to_index = {name: i for i, name in enumerate(self.anchors.keys())}
        indices = []
        dist_values = []
        rssi_values = []
        samples_values = []

        for anchor_name in self.anchors.keys():
            history = self.anchor_data_history.get(anchor_name)
            if history and len(history["distances"]) > 0:
                # Check for data staleness
                if (
                    current_time - history["timestamps"][-1]
                    > self.DATA_STALENESS_THRESHOLD
                ):
                    continue

                # Apply median filter if enabled
                if self.ENABLE_MEDIAN_FILTER:
                    window_size = min(
                        len(history["distances"]), self.MEDIAN_FILTER_WINDOW
                    )
                    if window_size > 0:
                        recent_distances = history["distances"][-window_size:]
                        filtered_distance = np.median(recent_distances)
                    else:
                        filtered_distance = history["distances"][-1]
                else:
                    filtered_distance = history["distances"][-1]

                indices.append(anchor_name_to_index[anchor_name])
                dist_values.append(filtered_distance)

                # Retrieve quality metrics corresponding to the latest measurement
                latest_rssi, latest_samples = self.latest_quality.get(
                    anchor_name, (0, 0)
                )
                rssi_values.append(latest_rssi)
                samples_values.append(latest_samples)

        if len(dist_values) >= 3:
            # Return filtered distances and quality metrics
            return (
                np.array(indices),
                np.array(dist_values),
                np.array(rssi_values),
                np.array(samples_values),
            )
        else:
            return None, None, None, None

    def simulate_movement_and_measurements(self):
        """Simulate object movement and distance measurements (simulation mode only)"""
        if self.config["mode"] != "simulation":
            return None, None, None

        # Update movement state within boundaries
        if self.is_moving:
            direction = self.target_pos - self.current_pos
            dist = np.linalg.norm(direction)

            if dist < self.MOVEMENT_SPEED * self.dt:
                self.current_pos = np.copy(self.target_pos)
                self.is_moving = False
                self.wait_timer = random.uniform(self.MIN_PAUSE_S, self.MAX_PAUSE_S)
            else:
                self.current_pos += (direction / dist) * self.MOVEMENT_SPEED * self.dt
        else:
            self.wait_timer -= self.dt
            if self.wait_timer <= 0:
                self.target_pos = np.array(
                    [
                        random.uniform(self.X_MIN + 0.5, self.X_MAX - 0.5),
                        random.uniform(self.Y_MIN + 0.5, self.Y_MAX - 0.5),
                    ]
                )
                self.is_moving = True

        # Calculate acceleration and bias (for accelerometer simulation)
        measured_accel = np.zeros(2)
        if self.use_accelerometer:
            current_vel_sim = (self.current_pos - self.last_pos_sim) / self.dt
            true_accel = (current_vel_sim - self.last_vel_sim) / self.dt
            self.last_vel_sim = current_vel_sim

            # Update bias (random walk)
            self.true_bias += np.random.normal(
                0, self.ACCEL_BIAS_WALK_STD * np.sqrt(self.dt), 2
            )

            # Measured acceleration includes true acceleration + bias + noise
            measured_accel = (
                true_accel
                + self.true_bias
                + np.random.normal(0, self.ACCEL_NOISE_STD_DEV, 2)
            )

        self.last_pos_sim = np.copy(self.current_pos)

        # Simulate distance measurements (Omitted for brevity)
        measured_distances = []
        anchor_indices = []

        for i, (anchor_name, anchor_pos) in enumerate(self.anchors.items()):
            true_distance = np.linalg.norm(self.current_pos - anchor_pos)
            noise = np.random.normal(0, self.NOISE_STD_DEV)
            distance = max(0, true_distance + noise)

            # Add outliers
            if random.random() < self.OUTLIER_PROB:
                distance = max(
                    0, distance + random.choice([-1, 1]) * self.OUTLIER_MAGNITUDE
                )

            measured_distances.append(distance)
            anchor_indices.append(i)

        return np.array(anchor_indices), np.array(measured_distances), measured_accel

    # --- RANSAC Implementation (Optimized) ---

    # Update signature to accept quality metrics and return MSE/Inliers
    def perform_ransac(
        self, anchor_indices, measured_distances, rssi_values=None, samples_values=None
    ):
        """
        Perform RANSAC multilateration. Uses WLS refinement if quality metrics are provided.
        Returns estimate, residual_mse, and relative inlier indices.
        """
        if len(measured_distances) < 3:
            return None, np.inf, None

        # Calculate Weights if available and enabled
        weights = None
        if self.ENABLE_WLS and rssi_values is not None and samples_values is not None:
            try:
                weights = self.calculate_weights(rssi_values, samples_values)
            except Exception:
                weights = None

        num_measurements = len(measured_distances)
        best_estimate = None
        max_inliers = 0
        best_inlier_indices = []

        # Generate combinations of 3 indices
        measurement_combinations = list(
            itertools.combinations(range(num_measurements), 3)
        )

        # Determine the number of iterations
        iterations = min(self.RANSAC_ITERATIONS, len(measurement_combinations))

        # Randomly sample the combinations
        if iterations < len(measurement_combinations):
            sampled_combinations = random.sample(measurement_combinations, iterations)
        else:
            sampled_combinations = measurement_combinations

        for subset_indices in sampled_combinations:
            # 1. Select minimal subset
            subset_anchor_indices = anchor_indices[list(subset_indices)]
            subset_distances = measured_distances[list(subset_indices)]
            subset_anchors = self.anchor_array[subset_anchor_indices]

            # 2. Model Estimation (Trilateration)
            candidate_pos = self.trilaterate_lstsq(subset_anchors, subset_distances)

            if candidate_pos is None:
                continue

            # Note: Bounding box constraint is removed here.

            # 3. Consensus Check
            inlier_count, errors = self.count_inliers(
                candidate_pos, anchor_indices, measured_distances, self.RANSAC_THRESHOLD
            )

            if inlier_count > max_inliers:
                max_inliers = inlier_count
                best_estimate = candidate_pos
                best_inlier_indices = np.where(errors <= self.RANSAC_THRESHOLD)[0]

        # 4. Refinement
        final_estimate = None
        final_residual_mse = np.inf

        # Refine if we have at least 3 inliers
        if best_estimate is not None and len(best_inlier_indices) >= 3:
            inlier_anchor_indices = anchor_indices[best_inlier_indices]
            inlier_distances = measured_distances[best_inlier_indices]
            inlier_anchors = self.anchor_array[inlier_anchor_indices]

            # Use WLS if weights are available
            refined_estimate = None
            if weights is not None:
                inlier_weights = weights[best_inlier_indices]
                refined_estimate = self.trilaterate_wls(
                    inlier_anchors, inlier_distances, inlier_weights
                )

            # Fallback to LSTSQ if WLS failed or weights not provided
            if refined_estimate is None:
                refined_estimate = self.trilaterate_lstsq(
                    inlier_anchors, inlier_distances
                )

            if refined_estimate is not None:
                final_estimate = refined_estimate
                # Calculate the quality of the refined fit (MSE) - used for Dynamic R
                predicted_distances = np.linalg.norm(
                    inlier_anchors - final_estimate, axis=1
                )
                residuals = predicted_distances - inlier_distances
                final_residual_mse = np.mean(np.square(residuals))

        # Handle case where refinement fails but we still have an initial best_estimate
        if final_estimate is None and best_estimate is not None:
            final_estimate = best_estimate
            # Calculate MSE for the initial best estimate (fallback)
            _, errors = self.count_inliers(
                final_estimate,
                anchor_indices,
                measured_distances,
                self.RANSAC_THRESHOLD,
            )
            inlier_errors = errors[best_inlier_indices]
            final_residual_mse = np.mean(np.square(inlier_errors))

        return final_estimate, final_residual_mse, best_inlier_indices

    def calculate_asymmetric_R_adjustment(self, base_R_variance):
        """Calculate adjustment factor for R based on movement direction (Asymmetry)."""
        if (
            not self.ENABLE_ASYMMETRIC_R
            or not self.use_kalman_filter
            or self.kf.x is None
        ):
            return base_R_variance

        # Get estimated velocity and position from KF (Prediction state)
        velocity = self.kf.x[2:4].flatten()
        speed = np.linalg.norm(velocity)
        position = self.kf.x[:2].flatten()

        # Only apply adjustment if moving significantly
        if speed < self.VELOCITY_THRESHOLD:
            return base_R_variance

        # Calculate the average direction relative to the center of the anchor array
        # This is a robust heuristic for general movement direction
        anchor_center = np.mean(self.anchor_array, axis=0)
        vector_to_center = anchor_center - position
        dist_to_center = np.linalg.norm(vector_to_center)

        if dist_to_center > 0.01:
            # Cosine of the angle between velocity vector and vector to the center
            cosine = np.dot(velocity, vector_to_center) / (speed * dist_to_center)

            # cosine > 0 means moving towards center, < 0 means moving away
            if cosine < -0.2:  # Moving away
                factor = self.R_AWAY_FACTOR
            elif cosine > 0.2:  # Moving towards
                factor = self.R_TOWARDS_FACTOR
            else:
                factor = 1.0
        else:
            factor = 1.0

        return base_R_variance * factor

    def is_valid_measurement(self, z):
        """Validate the measurement using Mahalanobis distance gating."""
        if not self.ENABLE_MEASUREMENT_GATING or self.kf is None:
            return True

        # Prepare measurement and KF matrices (Position only)
        z_pos = z[:2].reshape(2, 1)
        if self.use_accelerometer:
            H = self.kf.H[:2, :]
            R = self.kf.R[:2, :2]
        else:
            H = self.kf.H
            R = self.kf.R

        # Calculate innovation (measurement residual)
        y = z_pos - H @ self.kf.x
        # Calculate innovation covariance (Using P from prediction step and base R)
        S = H @ self.kf.P @ H.T + R

        try:
            # Calculate Mahalanobis distance squared
            d2 = y.T @ np.linalg.inv(S) @ y
        except np.linalg.LinAlgError:
            return True  # Accept measurement if S is singular

        if d2 < self.GATING_THRESHOLD:
            return True
        else:
            # print(f"Measurement rejected by gating. d2={d2:.2f}")
            return False

    def update_anchor_stats(self, used_anchor_indices, inlier_indices_rel):
        """Update the inlier statistics for anchors used in RANSAC."""
        if inlier_indices_rel is None:
            return

        anchor_names = list(self.anchors.keys())

        # Get the global indices of the inliers
        inlier_global_indices = used_anchor_indices[inlier_indices_rel]

        for idx in used_anchor_indices:
            if idx < len(anchor_names):
                anchor_name = anchor_names[idx]
                stats = self.anchor_stats[anchor_name]
                stats["total_measurements"] += 1
                if idx in inlier_global_indices:
                    stats["inlier_count"] += 1

    def check_anchor_health(self):
        """Check anchor health based on inlier rates."""
        for anchor_name, stats in self.anchor_stats.items():
            if stats["total_measurements"] >= self.HEALTH_WINDOW_SIZE:
                inlier_rate = stats["inlier_count"] / stats["total_measurements"]

                if inlier_rate < self.MIN_INLIER_RATE:
                    if anchor_name not in self.unhealthy_anchors:
                        print(
                            f"Warning: Anchor {anchor_name} marked as unhealthy (Rate: {inlier_rate:.2f})"
                        )
                        self.unhealthy_anchors.add(anchor_name)
                else:
                    if anchor_name in self.unhealthy_anchors:
                        print(
                            f"Info: Anchor {anchor_name} recovered (Rate: {inlier_rate:.2f})"
                        )
                        self.unhealthy_anchors.remove(anchor_name)

                # Reset stats for the next window
                stats["total_measurements"] = 0
                stats["inlier_count"] = 0

    # --- Plot Updates ---

    def update_plots(
        self,
        frame,
        ransac_estimate,
        kf_estimate,
        measured_accel=None,
        anchor_indices=None,
        measured_distances=None,
    ):
        """Update all plots with current estimates"""
        # Update distance circles if we have measurements
        if anchor_indices is not None and measured_distances is not None:
            # Clear all circles first
            for circles in self.distance_circles.values():
                if isinstance(circles, tuple):
                    # New format with outline and filled circles
                    outline_circle, filled_circle = circles
                    outline_circle.set_radius(0)
                    filled_circle.set_radius(0)
                else:
                    # Legacy format with single circle (fallback)
                    circles.set_radius(0)

            # Update circles for anchors with measurements
            anchor_names = list(self.anchors.keys())
            for i, idx in enumerate(anchor_indices):
                if idx < len(anchor_names) and i < len(measured_distances):
                    anchor_name = anchor_names[idx]
                    distance = measured_distances[i]
                    if anchor_name in self.distance_circles:
                        circles = self.distance_circles[anchor_name]
                        if isinstance(circles, tuple):
                            # New format with outline and filled circles
                            outline_circle, filled_circle = circles
                            outline_circle.set_radius(distance)
                            filled_circle.set_radius(distance)
                        else:
                            # Legacy format with single circle (fallback)
                            circles.set_radius(distance)
                        self.current_distances[anchor_name] = distance

        # Update position plots
        if self.config["mode"] == "simulation":
            if hasattr(self, "true_pos_plot") and self.true_pos_plot:
                self.true_pos_plot.set_data(
                    [self.current_pos[0]], [self.current_pos[1]]
                )

        if ransac_estimate is not None and self.SHOW_RANSAC_ESTIMATE:
            self.ransac_pos_plot.set_data([ransac_estimate[0]], [ransac_estimate[1]])
            self.ransac_pos_plot.set_visible(True)
        else:
            self.ransac_pos_plot.set_data([], [])
            if not self.SHOW_RANSAC_ESTIMATE:
                self.ransac_pos_plot.set_visible(False)

        if self.use_kalman_filter and kf_estimate is not None and self.SHOW_KF_ESTIMATE:
            if hasattr(self, "kf_pos_plot") and self.kf_pos_plot:
                self.kf_pos_plot.set_data([kf_estimate[0]], [kf_estimate[1]])
                self.kf_pos_plot.set_visible(True)
        elif (
            hasattr(self, "kf_pos_plot")
            and self.kf_pos_plot
            and not self.SHOW_KF_ESTIMATE
        ):
            self.kf_pos_plot.set_visible(False)

        # Calculate and store errors (only for simulation mode)
        error_ransac = np.nan
        error_kf = np.nan

        if self.config["mode"] == "simulation":
            if ransac_estimate is not None:
                error_ransac = np.linalg.norm(self.current_pos - ransac_estimate)
            if self.use_kalman_filter and kf_estimate is not None:
                error_kf = np.linalg.norm(self.current_pos - kf_estimate)

        self.frames.append(frame)
        self.ransac_errors.append(error_ransac)
        self.kf_errors.append(error_kf)

        # Update coordinate text display
        self.update_coordinate_display(ransac_estimate, kf_estimate)

        # Update plots based on mode
        if self.config["mode"] == "simulation" and self.ax_error:
            # Update error plots (simulation mode only)
            if hasattr(self, "ransac_err_line") and self.ransac_err_line:
                self.ransac_err_line.set_data(self.frames, self.ransac_errors)
            if (
                self.use_kalman_filter
                and hasattr(self, "kf_err_line")
                and self.kf_err_line
            ):
                self.kf_err_line.set_data(self.frames, self.kf_errors)

            self.ax_error.relim()
            self.ax_error.autoscale_view()

            # Update RMSE text
            if hasattr(self, "rmse_text") and self.rmse_text:
                ransac_rmse = np.sqrt(np.nanmean(np.square(self.ransac_errors)))
                kf_rmse = (
                    np.sqrt(np.nanmean(np.square(self.kf_errors)))
                    if self.use_kalman_filter
                    else 0
                )

                if self.use_kalman_filter:
                    rmse_text = (
                        f"📊 Performance Metrics\n"
                        f"━━━━━━━━━━━━━━━━━━━━━\n"
                        f"📍 RANSAC:     {ransac_rmse:.4f} m\n"
                        f"🎯 Kalman:     {kf_rmse:.4f} m\n"
                        f"📈 Improvement: {((ransac_rmse - kf_rmse) / ransac_rmse * 100):+.1f}%"
                    )
                else:
                    rmse_text = (
                        f"📊 Performance Metrics\n"
                        f"━━━━━━━━━━━━━━━━━━━━━\n"
                        f"📍 RANSAC:     {ransac_rmse:.4f} m"
                    )

                self.rmse_text.set_text(rmse_text)

        elif self.config["mode"] == "realtime" and self.ax_anchor_data:
            # Update anchor data plots (real-time mode only)
            self.update_anchor_data_plot()

        # Update bias plots (only if using accelerometer and in simulation mode)
        if (
            self.use_accelerometer
            and self.use_kalman_filter
            and self.config["mode"] == "simulation"
            and self.ax_bias
        ):
            if hasattr(self.kf, "x") and len(self.kf.x) >= 8:
                estimated_bias = self.kf.x[6:].flatten()

                self.true_biases_x.append(self.true_bias[0])
                self.true_biases_y.append(self.true_bias[1])
                self.est_biases_x.append(estimated_bias[0])
                self.est_biases_y.append(estimated_bias[1])

                if hasattr(self, "true_bias_x_line"):
                    self.true_bias_x_line.set_data(self.frames, self.true_biases_x)
                    self.true_bias_y_line.set_data(self.frames, self.true_biases_y)
                    self.est_bias_x_line.set_data(self.frames, self.est_biases_x)
                    self.est_bias_y_line.set_data(self.frames, self.est_biases_y)

                self.ax_bias.relim()
                self.ax_bias.autoscale_view()

        # Collect return objects for blitting
        return_objects = []

        # Add distance circles to return objects
        if hasattr(self, "distance_circles"):
            for circles in self.distance_circles.values():
                if circles:
                    if isinstance(circles, tuple):
                        # New format with outline and filled circles
                        outline_circle, filled_circle = circles
                        return_objects.extend([outline_circle, filled_circle])
                    else:
                        # Legacy format with single circle (fallback)
                        return_objects.append(circles)

        if hasattr(self, "ransac_pos_plot"):
            return_objects.append(self.ransac_pos_plot)

        if self.config["mode"] == "simulation":
            if hasattr(self, "true_pos_plot") and self.true_pos_plot:
                return_objects.append(self.true_pos_plot)
            if (
                self.ax_error
                and hasattr(self, "ransac_err_line")
                and self.ransac_err_line
            ):
                return_objects.append(self.ransac_err_line)
                if hasattr(self, "rmse_text") and self.rmse_text:
                    return_objects.append(self.rmse_text)
                if (
                    self.use_kalman_filter
                    and hasattr(self, "kf_err_line")
                    and self.kf_err_line
                ):
                    return_objects.append(self.kf_err_line)
        elif self.config["mode"] == "realtime" and self.ax_anchor_data:
            # Add anchor data lines for real-time mode
            if hasattr(self, "anchor_distance_lines"):
                return_objects.extend(
                    [line for line in self.anchor_distance_lines.values() if line]
                )
            if hasattr(self, "anchor_data_text") and self.anchor_data_text:
                return_objects.append(self.anchor_data_text)

        if self.use_kalman_filter and hasattr(self, "kf_pos_plot") and self.kf_pos_plot:
            return_objects.append(self.kf_pos_plot)

        if (
            self.use_accelerometer
            and self.use_kalman_filter
            and self.config["mode"] == "simulation"
            and self.ax_bias
        ):
            if hasattr(self, "true_bias_x_line"):
                bias_objects = [
                    self.true_bias_x_line,
                    self.true_bias_y_line,
                    self.est_bias_x_line,
                    self.est_bias_y_line,
                ]
                return_objects.extend([obj for obj in bias_objects if obj])

        # Add coordinate text display to return objects
        if hasattr(self, "coordinate_text") and self.coordinate_text:
            return_objects.append(self.coordinate_text)

        # Filter out None objects
        return [obj for obj in return_objects if obj is not None]

    def update(self, frame):
        """Main update function called by animation"""

        # Calculate actual dt
        current_time = time.time()
        actual_dt = current_time - self.last_update_time
        self.last_update_time = current_time

        # Get distance measurements
        rssi_values, samples_values = None, None  # Initialize for simulation
        if self.config["mode"] == "simulation":
            anchor_indices, measured_distances, measured_accel = (
                self.simulate_movement_and_measurements()
            )
        else:
            # Get filtered distances and quality metrics
            anchor_indices, measured_distances, rssi_values, samples_values = (
                self.get_real_time_distances()
            )
            measured_accel = np.zeros(2)

        # Kalman filter update (Prediction)
        if self.use_kalman_filter:
            if self.config["mode"] == "realtime" and actual_dt > 0.01:
                self.kf.update_dt(actual_dt)
            self.kf.predict()

        kf_estimate = self.kf.x[:2].flatten() if self.use_kalman_filter else None

        # Update latest distances for logging
        if measured_distances is not None and anchor_indices is not None:
            anchor_names = list(self.anchors.keys())
            self.latest_distances.clear()
            self.latest_quality.clear()

            for i, anchor_idx in enumerate(anchor_indices):
                if anchor_idx < len(anchor_names) and i < len(measured_distances):
                    anchor_name = anchor_names[anchor_idx]
                    self.latest_distances[anchor_name] = measured_distances[i]

                    # Add quality metrics if available
                    if (
                        rssi_values is not None
                        and samples_values is not None
                        and i < len(rssi_values)
                        and i < len(samples_values)
                    ):
                        self.latest_quality[anchor_name] = (
                            rssi_values[i],
                            samples_values[i],
                        )

        if measured_distances is None:
            return self.update_plots(frame, None, kf_estimate, None, None, None)

        # >>>>> Filter out unhealthy anchors before RANSAC
        if (
            self.ENABLE_ANCHOR_HEALTH
            and self.config["mode"] == "realtime"
            and self.unhealthy_anchors
        ):
            healthy_mask = np.array(
                [
                    list(self.anchors.keys())[idx] not in self.unhealthy_anchors
                    for idx in anchor_indices
                ]
            )

            if np.sum(healthy_mask) < 3:
                print("Warning: Not enough healthy anchors. Relying on prediction.")
                # Pass original data for visualization
                return self.update_plots(
                    frame,
                    None,
                    kf_estimate,
                    measured_accel,
                    anchor_indices,
                    measured_distances,
                )

            current_anchor_indices = anchor_indices[healthy_mask]
            current_measured_distances = measured_distances[healthy_mask]
            current_rssi = (
                rssi_values[healthy_mask] if rssi_values is not None else None
            )
            current_samples = (
                samples_values[healthy_mask] if samples_values is not None else None
            )
        else:
            current_anchor_indices = anchor_indices
            current_measured_distances = measured_distances
            current_rssi = rssi_values
            current_samples = samples_values
        # <<<<< End Filter

        # Perform RANSAC multilateration (WLS Refinement)
        ransac_estimate, residual_mse, inlier_indices_rel = self.perform_ransac(
            current_anchor_indices,
            current_measured_distances,
            current_rssi,
            current_samples,
        )

        # >>>>> Update Anchor Statistics
        if self.ENABLE_ANCHOR_HEALTH and self.config["mode"] == "realtime":
            self.update_anchor_stats(current_anchor_indices, inlier_indices_rel)
            self.check_anchor_health()
        # <<<<< End Update Stats

        # Kalman filter update (Correction)
        if self.use_kalman_filter and ransac_estimate is not None:
            if self.use_accelerometer:
                measurement = np.concatenate([ransac_estimate, measured_accel])
            else:
                measurement = ransac_estimate

            # >>>>> Measurement Gating
            if self.is_valid_measurement(measurement):
                # >>>>> Dynamic R Calculation (if enabled)
                if self.ENABLE_DYNAMIC_R:
                    # 1. Dynamic R (Fit Quality): Base variance from RANSAC/WLS fit (MSE)
                    R_variance = residual_mse

                    # 2. Ensure bounds (avoid overconfidence or excessive lag)
                    R_variance = np.clip(
                        R_variance, self.MIN_R_VARIANCE, self.MAX_R_VARIANCE
                    )

                    # 3. Asymmetric R Adjustment (Sluggishness Compensation)
                    if self.config["mode"] == "realtime":
                        R_variance = self.calculate_asymmetric_R_adjustment(R_variance)

                    dynamic_R = np.diag([R_variance, R_variance])

                    # Update KF with dynamic R
                    self.kf.update(measurement, dynamic_R=dynamic_R)
                else:
                    # Use fixed R matrix (no dynamic adaptation)
                    self.kf.update(measurement)

                # Update estimate after correction
                kf_estimate = self.kf.x[:2].flatten()
            # <<<<< End Gating/Correction (Else: Rely on prediction)

        # Log data point if logging is active
        self.log_data_point()

        # Update plots (Pass original anchor_indices/measured_distances for visualization)
        return self.update_plots(
            frame,
            ransac_estimate,
            kf_estimate,
            measured_accel,
            anchor_indices,
            measured_distances,
        )

    def run(self):
        """Start the multilateration system"""
        try:
            # Tweak 4: Configure animation duration based on mode
            if self.config["mode"] == "simulation":
                frames = 500
                repeat = False
            else:
                # Run real-time mode indefinitely
                frames = None
                repeat = True

            # Set up animation
            if frames is None:
                # Real-time mode - disable frame caching to avoid memory issues
                self.ani = animation.FuncAnimation(
                    self.fig,
                    self.update,
                    frames=frames,
                    interval=self.UPDATE_INTERVAL_MS,
                    blit=True,
                    repeat=repeat,
                    cache_frame_data=False,
                )
            else:
                # Simulation mode - normal caching
                self.ani = animation.FuncAnimation(
                    self.fig,
                    self.update,
                    frames=frames,
                    interval=self.UPDATE_INTERVAL_MS,
                    blit=True,
                    repeat=repeat,
                )

            plt.show()

        finally:
            # Clean up
            if self.serial_reader:
                self.serial_reader.stop()

            # Stop logging and close file if active
            if self.is_logging:
                self.stop_logging()


def main():
    """Main application entry point"""
    print("Multilateration System - nRF54L15 Bluetooth Channel Sounding")
    print("=" * 60)

    while True:
        # Get configuration from user
        config_screen = ConfigurationScreen()
        config = config_screen.get_configuration()

        if config is None:
            print("Configuration cancelled.")
            return

        # Print configuration summary
        print("\nStarting system with configuration:")
        print(f"Mode: {config['mode']}")
        print(f"Use Accelerometer: {config['use_accelerometer']}")
        print(f"Use Kalman Filter: {config['use_kalman_filter']}")
        print(f"Number of Anchors: {len(config['anchors'])}")

        for name, pos in config["anchors"].items():
            print(f"  {name}: ({pos[0]:.2f}, {pos[1]:.2f})")

        if config["mode"] == "realtime" and config["com_ports"]:
            print("COM Port Assignments:")
            for name, port in config["com_ports"].items():
                print(f"  {name}: {port}")

        # (Configuration summary printing omitted for brevity)

        # Create and run the multilateration system
        try:
            system = MultilaterationSystem(config)
            system.run()

            # Check if we should restart configuration
            if not hasattr(system, "restart_config") or not system.restart_config:
                # Normal exit - break the loop
                break
            else:
                # User clicked "Back to Config" - continue the loop
                print("\nReturning to configuration...")
                continue

        except KeyboardInterrupt:
            print("\nSystem stopped by user.")
            break
        except Exception as e:
            print(f"An unexpected error occurred during system execution: {e}")
            import traceback

            traceback.print_exc()
            break


if __name__ == "__main__":
    main()
