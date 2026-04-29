# Continuum

A watch face for Pebble smartwatches (supports all models including OG, Steel, Time, Time Round, 2, and Emery). Time is displayed as four concentric rounded rectangles — each ring rotates to show one digit of the current hour and minute.

[Download on the Pebble App Store](https://apps.repebble.com/continuum_64924ceaf0c34b4fbda7a8f9)

> **Note:** This project was developed with AI assistance (Claude by Anthropic, and Jules).

---

## How It Works

The display is divided into four rings, from the inside out:

| Ring | Shows | Values |
|------|-------|--------|
| Innermost | Hour (tens digit) | 0 – 2 |
| Inner | Hour (ones digit) | 0 – 9 |
| Middle | Minute (tens digit) | 0 – 5 |
| Outer | Minute (ones digit) | 0 – 9 |

Numbers are arranged around each ring. When the time changes, the ring rotates to bring the correct number to the **highlight box** — a small square that marks the reading position. A center panel shows the weekday, month, and day, with an optional battery indicator below.

---

## Features

- **Smooth animations** — rings rotate with a natural inertia effect (slight overshoot, then spring back)
- **Staggered motion** — each ring starts 150 ms after the previous one for a flowing look
- **Full color customization** — independently set colors for each ring, the highlight box, numbers, center text, and background
- **Preset themes** — choose from Ocean Blue, Forest Green, or Cyberpunk, or build your own palette
- **Highlight position** — place the reading marker at the top, bottom, left, or right
- **Battery indicator** — color-coded bar turns red below 20 % and green while charging
- **Persistent settings** — your configuration survives app restarts and watch reboots

---

## Configuration

Open the Pebble app on your phone, go to **My Watchfaces → Continuum → Settings**.

### Colors

| Setting | What it affects |
|---------|-----------------|
| Background Color | The area outside and behind all rings |
| Innermost Ring Fill | Background of the innermost ring |
| Inner Ring Fill | Background of the second ring |
| Middle Ring Fill | Background of the third ring |
| Outer Ring Fill | Background of the outermost ring |
| Highlight Fill | Color of the reading-position box |
| Line Color | Borders drawn around each ring |
| Number Color | Digits that are *not* currently highlighted |
| Highlight Number Color | The digit inside the highlight box |
| Center Text Color | Weekday / month / day labels and battery icon |

### Highlight Position

Moves the reading box (and highlight) to **Top**, **Bottom**, **Left**, or **Right** of each ring.

### Settings

| Toggle | Default | Description |
|--------|---------|-------------|
| Enable Animations | On | Rings rotate smoothly; turn off for instant updates |
| Inertia Effect | On | Adds the overshoot-and-spring-back feel (requires animations on) |
| Show Battery Indicator | On | Displays a small battery bar below the date |

---

## Building

Requires the [Pebble SDK](https://developer.rebble.io/developer.pebble.com/sdk/index.html) and Node.js.

```bash
cd continuum
npm install
pebble build
pebble install --emulator emery   # or --phone <IP> for a real device
```

The build produces `build/continuum.pbw`.

---

## Project Structure

```
continuum/
├── src/
│   ├── c/
│   │   ├── main.c           # Watch face logic: drawing, animation, time updates
│   │   └── continuum.h      # Shared types and constants
│   └── pkjs/
│       ├── index.js         # Phone-side handler: receives settings, applies themes
│       ├── config.js        # Settings UI definition (Clay framework)
│       └── custom-clay.js   # Hides inertia toggle when animations are disabled
├── package.json             # App metadata and message key declarations
└── wscript                  # WAF build script
```

---

## License

MIT — Copyright (c) 2026 Sloth8497. See [LICENSE](LICENSE) for the full text.

---

## AI Assistance

This project was developed with the assistance of **Claude** (by Anthropic) and **Jules**. The watch face logic, animation system, geometry calculations, and configuration pipeline were written collaboratively between the human author and the AI. All code has been reviewed and curated by the project author.
