#!/usr/bin/env python3
"""Generates the macOS app icon for EGA Trek following Tahoe / modern macOS squircle guidelines."""
import math
import os
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

def make_icon():
    # Render at 2048x2048 supersampled, then downsample to 1024x1024.
    S = 2048
    cx, cy = S / 2, S / 2
    # Squircle radius: 412 * 2 = 824 in 2048 space
    rx, ry = 412.0 * 2, 412.0 * 2

    # 1. Base image
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Background gradient in space
    for y in range(S):
        t = y / S
        # Dark space navy to deep void
        r = int(6 + t * 4)
        g = int(12 + t * 2)
        b = int(32 - t * 18)
        draw.line([(0, y), (S, y)], fill=(r, g, b, 255))

    # Stars in background
    import random
    rng = random.Random(1701)
    for _ in range(350):
        sx = rng.randint(200, S - 200)
        sy = rng.randint(200, S - 200)
        brightness = rng.randint(120, 255)
        sz = rng.choice([1, 1, 2, 2, 3])
        draw.ellipse([sx - sz, sy - sz, sx + sz, sy + sz], fill=(brightness, brightness, int(brightness * 1.1), 200))

    # Tactical sensor rings in EGA Cyan (#00AAAA)
    grid_col = (0, 160, 190, 80)
    grid_bright = (0, 220, 255, 140)
    for radius in [280, 520, 760]:
        draw.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], outline=grid_col, width=4)
    # Range tick marks and crosshairs
    draw.line([(cx - 780, cy), (cx + 780, cy)], fill=grid_col, width=3)
    draw.line([(cx, cy - 780), (cx, cy + 780)], fill=grid_col, width=3)

    # Angle guide lines (45 deg)
    d = 550
    draw.line([(cx - d, cy - d), (cx + d, cy + d)], fill=(0, 160, 190, 50), width=2)
    draw.line([(cx - d, cy + d), (cx + d, cy - d)], fill=(0, 160, 190, 50), width=2)

    # USS Enterprise NCC-1701 vector illustration
    # Saucer center slightly above middle
    sc_y = cy - 180
    sc_r = 340  # Saucer radius

    # Saucer glow
    saucer_glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sg_draw = ImageDraw.Draw(saucer_glow)
    sg_draw.ellipse([cx - sc_r - 20, sc_y - sc_r - 20, cx + sc_r + 20, sc_y + sc_r + 20], fill=(0, 200, 255, 60))
    saucer_glow = saucer_glow.filter(ImageFilter.GaussianBlur(15))
    img.alpha_composite(saucer_glow)

    # Primary Saucer body (solid EGA grey-white #D8E4EC)
    draw.ellipse([cx - sc_r, sc_y - sc_r, cx + sc_r, sc_y + sc_r], fill=(210, 228, 240, 255), outline=(0, 220, 255, 255), width=10)

    # Inner concentric rings on saucer
    draw.ellipse([cx - sc_r * 0.72, sc_y - sc_r * 0.72, cx + sc_r * 0.72, sc_y + sc_r * 0.72], outline=(140, 180, 210, 200), width=6)
    draw.ellipse([cx - sc_r * 0.45, sc_y - sc_r * 0.45, cx + sc_r * 0.45, sc_y + sc_r * 0.45], outline=(0, 180, 220, 220), width=6)

    # Bridge dome
    draw.ellipse([cx - 45, sc_y - 45, cx + 45, sc_y + 45], fill=(255, 255, 255, 255), outline=(0, 220, 255, 255), width=6)
    draw.ellipse([cx - 16, sc_y - 16, cx + 16, sc_y + 16], fill=(255, 220, 0, 255))

    # Secondary Hull (Engineering Hull)
    eh_w, eh_h = 100, 480
    eh_top = sc_y + 180
    draw.rounded_rectangle([cx - eh_w, eh_top, cx + eh_w, eh_top + eh_h], radius=40, fill=(190, 212, 230, 255), outline=(0, 200, 240, 255), width=8)

    # Deflector Dish (Amber / Gold glow)
    deflector_y = eh_top + eh_h - 40
    draw.ellipse([cx - 48, deflector_y - 20, cx + 48, deflector_y + 20], fill=(255, 170, 0, 255), outline=(255, 240, 100, 255), width=6)

    # Warp Pylons (struts extending outward to nacelles)
    pylon_y_start = eh_top + 160
    nacelle_spacing = 380
    nacelle_y_top = eh_top - 60
    nacelle_w = 70
    nacelle_h = 560

    # Left pylon
    draw.polygon([(cx - 70, pylon_y_start + 60), (cx - nacelle_spacing + 20, pylon_y_start - 30),
                  (cx - nacelle_spacing + 20, pylon_y_start + 10), (cx - 70, pylon_y_start + 100)],
                 fill=(160, 190, 215, 255), outline=(0, 200, 240, 255))
    # Right pylon
    draw.polygon([(cx + 70, pylon_y_start + 60), (cx + nacelle_spacing - 20, pylon_y_start - 30),
                  (cx + nacelle_spacing - 20, pylon_y_start + 10), (cx + 70, pylon_y_start + 100)],
                 fill=(160, 190, 215, 255), outline=(0, 200, 240, 255))

    # Twin Warp Nacelles
    for nx in [cx - nacelle_spacing, cx + nacelle_spacing]:
        # Nacelle main body
        draw.rounded_rectangle([nx - nacelle_w, nacelle_y_top, nx + nacelle_w, nacelle_y_top + nacelle_h],
                               radius=25, fill=(180, 205, 225, 255), outline=(0, 200, 240, 255), width=8)
        # Bussard Ramscoop at forward end (glowing red / orange)
        draw.ellipse([nx - nacelle_w + 10, nacelle_y_top - 20, nx + nacelle_w - 10, nacelle_y_top + 40],
                     fill=(255, 60, 40, 255), outline=(255, 160, 80, 255), width=5)
        # Warp grille (glowing blue-cyan)
        draw.rectangle([nx - 20, nacelle_y_top + 80, nx + 20, nacelle_y_top + nacelle_h - 60],
                       fill=(0, 230, 255, 255))

    # Starfleet Delta Emblem subtle watermark / crest at bottom center
    badge_y = S - 320
    badge = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    b_draw = ImageDraw.Draw(badge)
    delta_pts = [
        (cx, badge_y - 120),
        (cx + 80, badge_y + 80),
        (cx, badge_y + 40),
        (cx - 80, badge_y + 80)
    ]
    b_draw.polygon(delta_pts, fill=(255, 210, 0, 230), outline=(255, 255, 255, 255))
    # Star inside delta
    b_draw.ellipse([cx - 10, badge_y - 10, cx + 10, badge_y + 10], fill=(255, 255, 255, 255))
    img.alpha_composite(badge)

    # Title label banner: "EGA TREK"
    # Draw retro futuristic text or lettering
    # High-contrast label bar near bottom
    bar_y = S - 200
    draw.rounded_rectangle([cx - 280, bar_y - 45, cx + 280, bar_y + 45], radius=20, fill=(0, 20, 45, 220), outline=(0, 220, 255, 255), width=6)

    # Vector letter strokes for "EGA TREK"
    def draw_glyph(lines, ox, oy, scale):
        for (x1, y1, x2, y2) in lines:
            draw.line([(ox + x1 * scale, oy + y1 * scale), (ox + x2 * scale, oy + y2 * scale)], fill=(255, 255, 255, 255), width=7)

    # 5x7 block font definitions
    E = [(0,0, 0,6), (0,0, 4,0), (0,3, 3,3), (0,6, 4,6)]
    G = [(0,0, 4,0), (0,0, 0,6), (0,6, 4,6), (4,6, 4,3), (2,3, 4,3)]
    A = [(0,6, 0,2), (0,2, 2,0), (2,0, 4,2), (4,2, 4,6), (0,4, 4,4)]
    T = [(0,0, 4,0), (2,0, 2,6)]
    R = [(0,0, 0,6), (0,0, 4,0), (4,0, 4,3), (0,3, 4,3), (2,3, 4,6)]
    K = [(0,0, 0,6), (4,0, 0,3), (0,3, 4,6)]

    letters = [E, G, A, None, T, R, E, K]
    scale = 8
    start_x = cx - 220
    for idx, glyph in enumerate(letters):
        gx = start_x + idx * 56
        if glyph:
            draw_glyph(glyph, gx, bar_y - 24, scale)

    # 2. Build continuous squircle mask (superellipse p=4.6)
    mask = Image.new("L", (S, S), 0)
    m_draw = ImageDraw.Draw(mask)
    for y in range(S):
        dy = abs(y - cy) / ry
        if dy <= 1.0:
            rem = 1.0 - dy**4.6
            if rem > 0:
                dx = (rem**(1.0 / 4.6)) * rx
                m_draw.line([(int(cx - dx), y), (int(cx + dx), y)], fill=255)

    # Apply mask to image
    final_2k = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    final_2k.paste(img, (0, 0), mask=mask)

    # Add subtle border highlight around the squircle rim
    border_mask = mask.filter(ImageFilter.FIND_EDGES).filter(ImageFilter.GaussianBlur(2))
    border_overlay = Image.new("RGBA", (S, S), (0, 220, 255, 120))
    final_2k.paste(border_overlay, (0, 0), mask=border_mask)

    # 3. Downsample to 1024x1024 high-res master
    icon_1024 = final_2k.resize((1024, 1024), Image.Resampling.LANCZOS)
    return icon_1024

def main():
    root = Path(__file__).resolve().parent.parent
    res_dir = root / "engine" / "resources"
    res_dir.mkdir(parents=True, exist_ok=True)
    iconset_dir = res_dir / "AppIcon.iconset"
    iconset_dir.mkdir(exist_ok=True)

    master = make_icon()

    # Generate all standard macOS icon sizes
    sizes = [
        (16, 1, "icon_16x16.png"),
        (16, 2, "icon_16x16@2x.png"),
        (32, 1, "icon_32x32.png"),
        (32, 2, "icon_32x32@2x.png"),
        (128, 1, "icon_128x128.png"),
        (128, 2, "icon_128x128@2x.png"),
        (256, 1, "icon_256x256.png"),
        (256, 2, "icon_256x256@2x.png"),
        (512, 1, "icon_512x512.png"),
        (512, 2, "icon_512x512@2x.png"),
    ]

    for base_sz, scale, filename in sizes:
        dim = base_sz * scale
        resized = master.resize((dim, dim), Image.Resampling.LANCZOS)
        resized.save(iconset_dir / filename, "PNG")

    # Master 1024 PNG
    master.save(res_dir / "AppIcon-1024.png", "PNG")

    # Generate .icns via iconutil
    icns_path = res_dir / "AppIcon.icns"
    subprocess.run(["iconutil", "-c", "icns", str(iconset_dir), "-o", str(icns_path)], check=True)
    print(f"Generated {icns_path} ({icns_path.stat().st_size} bytes)")

if __name__ == "__main__":
    main()
