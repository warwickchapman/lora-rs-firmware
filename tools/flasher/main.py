import multiprocessing
import sys

# Standard requirement for PyInstaller bundled apps on Windows
if __name__ == "__main__":
    multiprocessing.freeze_support()

import flet as ft
import logic
import threading
import os
import subprocess
import pathlib
import datetime

# Glassmorphism Theme Constants
BG_COLOR = "#0f172a"
CARD_BG = "rgba(15, 23, 42, 0.4)"
GLASS_BORDER = "rgba(255, 255, 255, 0.1)"

def get_region_guess():
    """Guess region based on timezone."""
    try:
        tz = datetime.datetime.now(datetime.timezone.utc).astimezone().tzname()
        if tz in ["SAST", "CAT", "EAT"]: return "ZA"
        if any(us_tz in tz for us_tz in ["EST", "CST", "MST", "PST", "EDT", "CDT", "MDT", "PDT"]): return "US"
        return "EU"
    except:
        return "EU"

def main(page: ft.Page):
    # v3.9: Branding and Region Selector
    page.title = "Thanda LoRa Flasher"
    page.theme_mode = ft.ThemeMode.DARK
    page.window_width = 1100
    page.bgcolor = BG_COLOR
    page.padding = 0 
    
    # Logic instances
    flasher_logic = logic.FlasherLogic()
    firmware_manager = logic.FirmwareManager()
    
    current_device_info = {}
    available_firmwares = [] # List of dicts from GitHub
    selected_local_path = None # For "Local File..." override
    
    # --- UI Components ---
    
    # Dense Log ListView
    log_box = ft.ListView(expand=True, spacing=0, padding=5, auto_scroll=True)

    def log(message):
        msg_text = str(message).strip()
        if not msg_text: return
        log_box.controls.append(
            ft.Container(
                content=ft.Text(
                    f"> {msg_text}", size=10, font_family="monospace", color=ft.Colors.GREY_300, no_wrap=True,
                ),
                padding=0, margin=0
            )
        )
        page.update()

    # Firmware Dropdown
    firmware_dropdown = ft.Dropdown(
        label="Firmware Version",
        hint_text="Fetching from GitHub...",
        expand=True,
        text_size=11,
        bgcolor="rgba(255, 255, 255, 0.03)",
        border_color=GLASS_BORDER,
        border_radius=8,
        on_change=lambda e: handle_firmware_change(e)
    )
    
    # Region Dropdown
    region_dropdown = ft.Dropdown(
        label="Region",
        options=[
            ft.dropdown.Option("ZA"),
            ft.dropdown.Option("EU"),
            ft.dropdown.Option("US"),
        ],
        value=get_region_guess(),
        expand=True,
        text_size=11,
        bgcolor="rgba(255, 255, 255, 0.03)",
        border_color=GLASS_BORDER,
        border_radius=8,
        on_change=lambda _: refresh_firmwares()
    )

    devices_dropdown = ft.Dropdown(
        label="Serial Port",
        hint_text="Scanning...",
        expand=True,
        text_size=11,
        bgcolor="rgba(255, 255, 255, 0.03)",
        border_color=GLASS_BORDER,
        border_radius=8,
    )

    sticker_content = ft.Column(spacing=0, scroll=ft.ScrollMode.AUTO)

    def refresh_ports(e=None):
        log("Scanning ports...")
        try:
            ports_info = logic.list_serial_ports()
            devices_dropdown.options = [
                ft.dropdown.Option(key=p["device"], text=f"{p['device']} ({p['description']})") 
                for p in ports_info
            ]
            if ports_info:
                devices_dropdown.value = ports_info[0]["device"]
                log(f"Found {len(ports_info)} ports.")
            else:
                devices_dropdown.value = None
                devices_dropdown.hint_text = "No ports found"
                log("No serial ports detected.")
        except Exception as ex:
            log(f"Error scanning ports: {ex}")
        page.update()

    def refresh_firmwares():
        log("Fetching releases from GitHub...")
        nonlocal available_firmwares
        available_firmwares = firmware_manager.get_available_firmwares()
        
        region = region_dropdown.value
        options = [ft.dropdown.Option(key="local", text="[ Local File... Browse ]")]
        
        for fw in available_firmwares:
            filename = fw['filename'].lower()
            name = fw['name']
            
            # Stricter filtering rules
            is_us = "-us.bin" in filename
            is_za = "-za.bin" in filename
            is_eu = "-eu.bin" in filename
            
            if region == "US" and is_us:
                options.append(ft.dropdown.Option(key=fw['url'], text=name))
            elif region == "ZA" and is_za:
                options.append(ft.dropdown.Option(key=fw['url'], text=name))
            elif region == "EU" and is_eu:
                options.append(ft.dropdown.Option(key=fw['url'], text=name))
            # Generic files or mismatched regions are hidden
            
        firmware_dropdown.options = options
        if len(options) > 1:
            firmware_dropdown.value = options[1].key
        else:
            firmware_dropdown.value = "local"
            firmware_dropdown.hint_text = "No compatible versions found"
        page.update()

    def handle_firmware_change(e):
        if firmware_dropdown.value == "local":
            on_browse_click(None)
        else:
            selected_text = next((opt.content.value if hasattr(opt.content, 'value') else opt.text for opt in firmware_dropdown.options if opt.key == firmware_dropdown.value), "Unknown")
            log(f"Selected cloud version: {selected_text}")

    # --- Clipboard & File Logic ---
    def copy_to_clipboard(text, label):
        page.set_clipboard(text)
        page.snack_bar = ft.SnackBar(ft.Text(f"Copied {label} to clipboard"))
        page.snack_bar.open = True
        page.update()

    def copy_all_device_info(e):
        if not current_device_info:
            log("No device info to copy.")
            return
        block = "\n".join([f"{k.replace('_', ' ').title()}: {v}" for k, v in current_device_info.items()])
        copy_to_clipboard(block, "All Device Configuration")

    def on_browse_click(e):
        log("Triggering local file dialog...")
        if sys.platform == "darwin":
            try:
                posix_script = 'set theFile to (choose file with prompt "Select LRS Firmware .bin" of type {"bin"}) \n return POSIX path of theFile'
                posix_result = subprocess.run(['osascript', '-e', posix_script], capture_output=True, text=True)
                if posix_result.returncode == 0:
                    chosen_path = posix_result.stdout.strip()
                    update_local_selection(chosen_path)
                else:
                    log("Selection cancelled.")
            except Exception as ex:
                log(f"Native Dialog Error: {ex}")
        else:
            file_picker.pick_files(allowed_extensions=["bin"])
        page.update()

    def pick_files_result(e: ft.FilePickerResultEvent):
        if e.files:
            update_local_selection(e.files[0].path)
        page.update()

    def update_local_selection(path):
        nonlocal selected_local_path
        selected_local_path = path
        filename = os.path.basename(path)
        log(f"Local file selected: {filename}")
        # Update dropdown to show local file is selected
        firmware_dropdown.options.insert(0, ft.dropdown.Option(key="local_selected", text=f"Local: {filename}"))
        firmware_dropdown.value = "local_selected"
        page.update()

    file_picker = ft.FilePicker(on_result=pick_files_result)
    page.overlay.append(file_picker)

    # --- UI Generators ---
    def show_sticker(info):
        nonlocal current_device_info
        current_device_info = info
        sticker_content.controls.clear()
        for key, value in info.items():
            label_text = key.replace("_", " ").title()
            sticker_content.controls.append(
                ft.Container(
                    content=ft.Row([
                        ft.Text(label_text, size=10, color=ft.Colors.GREY_500, width=75),
                        ft.Text(str(value), size=11, weight="bold", font_family="monospace", expand=True, selectable=True),
                        ft.IconButton(ft.Icons.COPY, icon_size=14, padding=0, 
                                      on_click=lambda e, v=value, l=label_text: copy_to_clipboard(str(v), l))
                    ], spacing=5),
                    padding=0, border=ft.border.only(bottom=ft.border.BorderSide(0.5, ft.Colors.GREY_900))
                )
            )
        page.update()

    # --- Actions ---
    def start_flash():
        if not devices_dropdown.value:
            log("Error: No serial port selected.")
            return
        if not firmware_dropdown.value:
            log("Error: No firmware selected.")
            return
            
        flash_btn.disabled = True
        log("--- Preparing Firmware ---")
        threading.Thread(target=lambda: run_prep_and_flash(), daemon=True).start()

    def run_prep_and_flash():
        try:
            final_path = None
            if firmware_dropdown.value == "local_selected":
                final_path = selected_local_path
            elif firmware_dropdown.value == "local":
                log("Error: Please browse for a local file first.")
                flash_btn.disabled = False
                page.update()
                return
            else:
                # Cloud version
                selected_fw = next(fw for fw in available_firmwares if fw['url'] == firmware_dropdown.value)
                log(f"Downloading {selected_fw['filename']}...")
                final_path = firmware_manager.download_firmware(selected_fw['url'], selected_fw['filename'])
                log("Download complete.")
            
            log(f"Flashing {os.path.basename(final_path)}...")
            flasher_logic.flash_firmware(devices_dropdown.value, 460800, final_path, callback=log)
            log("FLASH SUCCESSFUL!")
            read_info()
        except Exception as ex:
            log(f"ERROR: {ex}")
        finally:
            flash_btn.disabled = False
            page.update()

    def read_info():
        if not devices_dropdown.value: return
        log("Reading chip info...")
        def run_read():
            try:
                info = flasher_logic.get_chip_info(devices_dropdown.value)
                show_sticker(info)
                log("Retrieved info.")
            except Exception as ex:
                log(f"READ ERROR: {ex}")
        
        threading.Thread(target=run_read, daemon=True).start()

    # --- UI Layout ---
    flash_btn = ft.ElevatedButton(
        "Flash Firmware", icon=ft.Icons.BOLT, expand=True,
        style=ft.ButtonStyle(color=ft.Colors.WHITE, bgcolor="#4f46e5", shape=ft.RoundedRectangleBorder(radius=10)),
        on_click=lambda _: start_flash()
    )
    
    info_btn = ft.OutlinedButton(
        "Read Device Info", icon=ft.Icons.INFO_OUTLINE, expand=True,
        style=ft.ButtonStyle(shape=ft.RoundedRectangleBorder(radius=10), side=ft.BorderSide(1, GLASS_BORDER)),
        on_click=lambda _: read_info()
    )

    bg_stack = ft.Stack([
        ft.Container(width=400, height=400, bgcolor="#4c1d95", right=-100, top=-100, border_radius=200, blur=80, opacity=0.3),
        ft.Container(width=400, height=400, bgcolor="#1e3a8a", left=-100, bottom=-100, border_radius=200, blur=80, opacity=0.3),
        ft.Container(
            padding=15,
            content=ft.Column([
                ft.Column([
                    ft.Text("Thanda LoRa Flasher", size=32, weight="bold", color=ft.Colors.WHITE, text_align=ft.TextAlign.CENTER),
                    ft.Text("Firmware Updater", size=18, color=ft.Colors.BLUE_200, text_align=ft.TextAlign.CENTER),
                ], horizontal_alignment=ft.CrossAxisAlignment.CENTER, width=page.window_width),
                ft.Row([
                ft.Column([
                    ft.Text("LRS Log", size=20, weight="bold", color=ft.Colors.BLUE_200),
                    ft.Container(content=log_box, expand=True, bgcolor=CARD_BG, blur=24, border=ft.border.all(1, GLASS_BORDER), border_radius=16),
                ], expand=True, spacing=10),
                ft.Column([
                    ft.Container(
                        content=ft.Column([
                            ft.Text("Interface Controls", size=15, weight="bold", color=ft.Colors.GREY_100),
                            ft.Row([region_dropdown], spacing=5),
                            ft.Row([devices_dropdown, ft.IconButton(ft.Icons.REFRESH, on_click=refresh_ports, icon_size=18)], spacing=5),
                            ft.Row([firmware_dropdown, ft.IconButton(ft.Icons.CLOUD_DOWNLOAD, on_click=lambda _: refresh_firmwares(), icon_size=18, tooltip="Refresh Cloud")], spacing=5),
                            ft.Row([flash_btn, info_btn], spacing=10),
                        ], spacing=10),
                        padding=15, bgcolor=CARD_BG, blur=24, border=ft.border.all(1, GLASS_BORDER), border_radius=16,
                    ),
                    ft.Container(
                        content=ft.Column([
                            ft.Row([ft.Text("Device Configuration", size=15, weight="bold", color=ft.Colors.BLUE_200, expand=True), ft.IconButton(ft.Icons.COPY, icon_size=16, on_click=copy_all_device_info)]),
                            sticker_content
                        ], scroll=ft.ScrollMode.AUTO, spacing=5),
                        expand=True, padding=ft.padding.only(left=15, right=5, top=10, bottom=15), bgcolor=CARD_BG, blur=24, border=ft.border.all(1, GLASS_BORDER), border_radius=16,
                    )
                ], width=420, spacing=15)
                ], expand=True, spacing=15),
            ], expand=True, spacing=20)
        )
    ], expand=True)

    page.add(bg_stack)
    refresh_ports()
    # Fetch firmwares in background thread to avoid UI freeze
    def initial_refresh():
        try:
            refresh_firmwares()
        except Exception as ex:
            log(f"STARTUP ERROR: {ex}")
    
    threading.Thread(target=initial_refresh, daemon=True).start()

if __name__ == "__main__":
    ft.app(target=main)
