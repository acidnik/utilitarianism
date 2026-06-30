"""Colorize Pebble weather SVGs. 
For each icon type, we apply specific color rules to polygons (clouds/body) vs lines (precipitation rays/snow)."""

import re, os

SVG_DIR = "/tmp/weather-icons"
OUT_DIR = "/tmp/weather-icons/colored"
os.makedirs(OUT_DIR, exist_ok=True)

def read_svg(name):
    with open(os.path.join(SVG_DIR, name)) as f:
        return f.read()

def write_svg(name, content):
    with open(os.path.join(OUT_DIR, name), 'w') as f:
        f.write(content)

# ---- Sunny day: sun = yellow ----
svg = read_svg("Pebble_50x50_Sunny_day.svg")
# Sun body (polygon) and sun rays (lines) — make sun yellow, rays orange
svg = svg.replace('fill="#FFFFFF"', 'fill="#FFD700"')   # gold/yellow body
svg = svg.replace('stroke="#000000"', 'stroke="#FF8C00"')  # dark orange rays
write_svg("Pebble_50x50_Sunny_day.svg", svg)
print("Sunny_day → yellow (#FFD700)")

# ---- Partly cloudy: cloud white/gray, sun accents yellow ----
svg = read_svg("Pebble_50x50_Partly_cloudy.svg")
# Main cloud (large polygon): light gray fill, gray stroke
svg = svg.replace('fill="#FFFFFF"', 'fill="#E0E0E0"')
svg = svg.replace('stroke="#000000"', 'stroke="#808080"')
write_svg("Pebble_50x50_Partly_cloudy.svg", svg)
print("Partly_cloudy → light gray")

# ---- Cloudy day: gray cloud ----
svg = read_svg("Pebble_50x50_Cloudy_day.svg")
svg = svg.replace('fill="#FFFFFF"', 'fill="#C0C0C0"')
svg = svg.replace('stroke="#000000"', 'stroke="#808080"')
write_svg("Pebble_50x50_Cloudy_day.svg", svg)
print("Cloudy_day → gray")

# ---- Light rain: blue drops, gray cloud ----
svg = read_svg("Pebble_50x50_Light_rain.svg")
# Cloud is the <polygon> elements, rain is <line> elements
# Lines have both fill and stroke. Change line stroke to blue.
svg = re.sub(r'(<line[^>]*stroke="#)000000(")', r'\g<1>2A9BFF\2', svg)
# Lines fill: keep white, or change to blue for precip
svg = re.sub(r'(<line[^>]*fill="#)FFFFFF(")', r'\g<1>2A9BFF\2', svg)
# Cloud polygon: gray fill
svg = re.sub(r'(<polygon[^>]*fill="#)FFFFFF(")', r'\g<1>B0B0B0\2', svg)
# Cloud polyline: gray
svg = re.sub(r'(<polyline[^>]*fill="#)FFFFFF(")', r'\g<1>B0B0B0\2', svg)
# Cloud stroke: darker gray
svg = re.sub(r'(<(?:polygon|polyline)[^>]*stroke="#)000000(")', r'\g<1>808080\2', svg)
write_svg("Pebble_50x50_Light_rain.svg", svg)
print("Light_rain → blue drops + gray cloud")

# ---- Heavy rain: more blue, darker cloud ----
svg = read_svg("Pebble_50x50_Heavy_rain.svg")
svg = re.sub(r'(<line[^>]*stroke="#)000000(")', r'\g<1>2A9BFF\2', svg)
svg = re.sub(r'(<line[^>]*fill="#)FFFFFF(")', r'\g<1>2A9BFF\2', svg)
svg = re.sub(r'(<polygon[^>]*fill="#)FFFFFF(")', r'\g<1>909090\2', svg)
svg = re.sub(r'(<polyline[^>]*fill="#)FFFFFF(")', r'\g<1>909090\2', svg)
svg = re.sub(r'(<(?:polygon|polyline)[^>]*stroke="#)000000(")', r'\g<1>606060\2', svg)
write_svg("Pebble_50x50_Heavy_rain.svg", svg)
print("Heavy_rain → blue drops + dark gray cloud")

# ---- Light snow: white flakes, gray cloud ----
svg = read_svg("Pebble_50x50_Light_snow.svg")
# Keep snow lines white (fill="#FFFFFF") — they're snow
# Change cloud to light gray
svg = re.sub(r'(<polygon[^>]*fill="#)FFFFFF(")', r'\g<1>C8C8C8\2', svg)
svg = re.sub(r'(<polyline[^>]*fill="#)FFFFFF(")', r'\g<1>C8C8C8\2', svg)
# Cloud stroke gray
svg = re.sub(r'(<(?:polygon|polyline)[^>]*stroke="#)000000(")', r'\g<1>888888\2', svg)
# Line stroke: keep black for contrast on white snow, or light gray
svg = re.sub(r'(<line[^>]*stroke="#)000000(")', r'\g<1>88BBFF\2', svg)  # faint blue for snow
write_svg("Pebble_50x50_Light_snow.svg", svg)
print("Light_snow → white flakes + gray cloud")

# ---- Heavy snow: white flakes, darker cloud ----
svg = read_svg("Pebble_50x50_Heavy_snow.svg")
svg = re.sub(r'(<polygon[^>]*fill="#)FFFFFF(")', r'\g<1>909090\2', svg)
svg = re.sub(r'(<polyline[^>]*fill="#)FFFFFF(")', r'\g<1>909090\2', svg)
svg = re.sub(r'(<(?:polygon|polyline)[^>]*stroke="#)000000(")', r'\g<1>606060\2', svg)
svg = re.sub(r'(<line[^>]*stroke="#)000000(")', r'\g<1>88DDFF\2', svg)
write_svg("Pebble_50x50_Heavy_snow.svg", svg)
print("Heavy_snow → white flakes + dark gray cloud")

# ---- Raining and snowing: mixed ----
svg = read_svg("Pebble_50x50_Raining_and_snowing.svg")
# Some lines rain (blue), some snow (white). Here all lines get blue for rain.
svg = re.sub(r'(<line[^>]*stroke="#)000000(")', r'\g<1>4488CC\2', svg)  # blue-gray
svg = re.sub(r'(<line[^>]*fill="#)FFFFFF(")', r'\g<1>4488CC\2', svg)
svg = re.sub(r'(<polygon[^>]*fill="#)FFFFFF(")', r'\g<1>909090\2', svg)
svg = re.sub(r'(<polyline[^>]*fill="#)FFFFFF(")', r'\g<1>909090\2', svg)
svg = re.sub(r'(<(?:polygon|polyline)[^>]*stroke="#)000000(")', r'\g<1>606060\2', svg)
write_svg("Pebble_50x50_Raining_and_snowing.svg", svg)
print("Raining_and_snowing → blue-gray + dark gray cloud")

# ---- Generic weather: light gray ----
svg = read_svg("Pebble_50x50_Generic_weather.svg")
svg = svg.replace('fill="#FFFFFF"', 'fill="#C0C0C0"')
svg = svg.replace('stroke="#000000"', 'stroke="#808080"')
write_svg("Pebble_50x50_Generic_weather.svg", svg)
print("Generic_weather → gray")

print("\nAll done!")
