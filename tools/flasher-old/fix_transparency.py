import os
from PIL import Image

def convert_to_alpha(source_path, target_path):
    img = Image.open(source_path).convert("RGBA")
    data = img.getdata()
    
    # Get top-left corner pixel as background color reference
    # or use the known blue: (0, 11, 46)
    bg_color = data[0]
    print(f"Detected background color: {bg_color}")
    
    new_data = []
    for item in data:
        # If pixel is the background color, make it transparent
        # Using a small tolerance for compressed images
        if abs(item[0] - bg_color[0]) < 10 and \
           abs(item[1] - bg_color[1]) < 10 and \
           abs(item[2] - bg_color[2]) < 10:
            new_data.append((255, 255, 255, 0))
        else:
            new_data.append(item)
            
    img.putdata(new_data)
    img.save(target_path, "PNG")
    print(f"Saved transparent logo to {target_path}")

if __name__ == "__main__":
    base_dir = "tools/flasher/assets"
    convert_to_alpha(os.path.join(base_dir, "logo_source.png"), os.path.join(base_dir, "logo.png"))
