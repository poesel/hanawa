#!/usr/bin/env python3
"""
Generiert ein Testbild (640x480) für die lokale Ambilight-Test-Version.
Das Bild enthält verschiedene Farbzonen zur besseren Visualisierung der Ambilight-Effekte.
"""

from PIL import Image, ImageDraw, ImageFont
import random

def create_tv_testimage(width=640, height=480):
    """Erstellt ein Testbild das einen Fernseh-Screenshot simuliert"""
    
    # Erstelle Bild mit schwarzem Hintergrund
    img = Image.new('RGB', (width, height), color='black')
    draw = ImageDraw.Draw(img)
    
    # Simuliere einen Fernseher mit bunten Bereichen
    # Hauptbild-Bereich (zentriert)
    main_x1, main_y1 = 80, 60
    main_x2, main_y2 = 560, 420
    
    # Hintergrund: Dunkelgrau
    draw.rectangle([main_x1, main_y1, main_x2, main_y2], fill=(40, 40, 40))
    
    # Erstelle farbige Bereiche für bessere Ambilight-Visualisierung
    regions = [
        # Top-left: Blau
        ([main_x1, main_y1, main_x1 + 240, main_y1 + 180], (20, 100, 200)),
        # Top-right: Rot
        ([main_x2 - 240, main_y1, main_x2, main_y1 + 180], (200, 50, 50)),
        # Bottom-left: Grün
        ([main_x1, main_y2 - 180, main_x1 + 240, main_y2], (50, 180, 50)),
        # Bottom-right: Gelb
        ([main_x2 - 240, main_y2 - 180, main_x2, main_y2], (200, 200, 50)),
    ]
    
    for coords, color in regions:
        draw.rectangle(coords, fill=color)
    
    # Füge einige helle Highlights hinzu für bessere Kontraste
    for _ in range(20):
        x = random.randint(main_x1 + 20, main_x2 - 20)
        y = random.randint(main_y1 + 20, main_y2 - 20)
        size = random.randint(10, 40)
        brightness = random.randint(150, 255)
        color = (brightness, brightness, brightness)
        draw.ellipse([x, y, x + size, y + size], fill=color)
    
    # Schwarzer Rahmen um das "TV-Bild"
    draw.rectangle([main_x1 - 2, main_y1 - 2, main_x2 + 2, main_y2 + 2], 
                   outline=(0, 0, 0), width=4)
    
    # Füge Text hinzu
    try:
        # Versuche eine Systemschrift zu laden
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 24)
    except:
        # Fallback auf Standard-Font
        font = ImageFont.load_default()
    
    text = "ESP32-CAM AMBILIGHT TEST"
    # Zeichne Text in der Mitte
    text_bbox = draw.textbbox((0, 0), text, font=font)
    text_width = text_bbox[2] - text_bbox[0]
    text_x = (width - text_width) // 2
    text_y = height // 2 - 20
    
    # Text mit Schatten
    draw.text((text_x + 2, text_y + 2), text, fill=(0, 0, 0), font=font)
    draw.text((text_x, text_y), text, fill=(255, 255, 255), font=font)
    
    return img

def create_gradient_testimage(width=640, height=480):
    """Erstellt ein Testbild mit Farbverläufen"""
    
    img = Image.new('RGB', (width, height))
    pixels = img.load()
    
    for y in range(height):
        for x in range(width):
            # Erstelle bunten Farbverlauf
            r = int((x / width) * 255)
            g = int((y / height) * 255)
            b = int(((x + y) / (width + height)) * 255)
            pixels[x, y] = (r, g, b)
    
    # Füge einen dunkleren Bereich in der Mitte hinzu
    draw = ImageDraw.Draw(img)
    center_x, center_y = width // 2, height // 2
    draw.rectangle([center_x - 200, center_y - 150, 
                   center_x + 200, center_y + 150], 
                  fill=(30, 30, 30))
    
    return img

if __name__ == "__main__":
    import sys
    
    print("Generiere Testbild für Ambilight-Visualisierung...")
    
    # Wähle Testbild-Typ
    if len(sys.argv) > 1 and sys.argv[1] == "gradient":
        print("Erstelle Farbverlauf-Testbild...")
        img = create_gradient_testimage()
    else:
        print("Erstelle TV-Simulator-Testbild...")
        img = create_tv_testimage()
    
    # Speichere als JPEG
    output_path = "testimage.jpg"
    img.save(output_path, "JPEG", quality=90)
    print(f"✅ Testbild gespeichert: {output_path}")
    print(f"   Größe: {img.size[0]}x{img.size[1]} Pixel")
    print("\nStarte die lokale Version mit:")
    print("  python3 -m http.server 8000")
    print("  → http://localhost:8000/ambilight_test.html")
