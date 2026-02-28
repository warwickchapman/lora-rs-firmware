import os
from PIL import Image

def generate_icons(source_path, target_dir):
    if not os.path.exists(source_path):
        print(f"Error: {source_path} not found.")
        return

    img = Image.open(source_path)
    
    # Ensure target directory exists
    os.makedirs(target_dir, exist_ok=True)

    # 1. Generate ICO for Windows
    # Sizes: 16, 32, 48, 64, 128, 256
    ico_path = os.path.join(target_dir, "thanda_lora.ico")
    img.save(ico_path, format='ICO', sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print(f"Generated {ico_path}")

    # 2. Generate PNGs for UI and Packaging
    # Application header/logo
    img.resize((64, 64), Image.Resampling.LANCZOS).save(os.path.join(target_dir, "icon_64.png"))
    img.resize((128, 128), Image.Resampling.LANCZOS).save(os.path.join(target_dir, "icon_128.png"))
    img.resize((512, 512), Image.Resampling.LANCZOS).save(os.path.join(target_dir, "icon_512.png"))
    print("Generated PNG assets.")

if __name__ == "__main__":
    base_dir = os.path.dirname(os.path.abspath(__file__))
    source = os.path.join(base_dir, "assets", "logo.png")
    output = os.path.join(base_dir, "assets")
    generate_icons(source, output)
