module.exports = [
  {
    "type": "heading",
    "defaultValue": "Continuum Configuration"
  },
  {
    "type": "section",
    "id": "section-theme",
    "capabilities": ["COLOR"],
    "items": [
      {
        "type": "heading",
        "defaultValue": "Theme"
      },
      {
        "type": "select",
        "messageKey": "THEME",
        "defaultValue": "0",
        "label": "Color Theme",
        "options": [
          { "label": "Custom / Default", "value": "0" },
          { "label": "Ocean Blue", "value": "1" },
          { "label": "Forest Green", "value": "2" },
          { "label": "Cyberpunk", "value": "3" }
        ],
        "description": "Selecting a theme will override individual color settings."
      }
    ]
  },
  {
    "type": "section",
    "id": "section-colors",
    "capabilities": ["COLOR"],
    "items": [
      {
        "type": "heading",
        "defaultValue": "Colors"
      },
      {
        "type": "color",
        "messageKey": "BACKGROUND_COLOR",
        "defaultValue": "0x000000",
        "label": "Background Color"
      },
      {
        "type": "color",
        "messageKey": "INNER_RING_COLOR",
        "defaultValue": "0x000000",
        "label": "Innermost Ring Fill Color"
      },
      {
        "type": "color",
        "messageKey": "SUB_INNER_RING_COLOR",
        "defaultValue": "0x000000",
        "label": "Inner Ring Fill Color"
      },
      {
        "type": "color",
        "messageKey": "MIDDLE_RING_COLOR",
        "defaultValue": "0x000000",
        "label": "Middle Ring Fill Color"
      },
      {
        "type": "color",
        "messageKey": "OUTER_RING_COLOR",
        "defaultValue": "0x000000",
        "label": "Outer Ring Fill Color"
      },
      {
        "type": "color",
        "messageKey": "HIGHLIGHT_FILL_COLOR",
        "defaultValue": "0xFF0000",
        "label": "Highlight Fill Color"
      },
      {
        "type": "color",
        "messageKey": "LINE_COLOR",
        "defaultValue": "0x555555",
        "label": "Line Color"
      },
      {
        "type": "color",
        "messageKey": "NUMBER_COLOR",
        "defaultValue": "0xAAAAAA",
        "label": "Number Color"
      },
      {
        "type": "color",
        "messageKey": "CENTER_TEXT_COLOR",
        "defaultValue": "0xFFFFFF",
        "label": "Center Time Text Color"
      },
      {
        "type": "color",
        "messageKey": "HIGHLIGHT_NUMBER_COLOR",
        "defaultValue": "0xFFFFFF",
        "label": "Highlight Number Color"
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Highlight Position"
      },
      {
        "type": "select",
        "messageKey": "HIGHLIGHT_POSITION",
        "defaultValue": "3",
        "label": "Highlight Read Position",
        "options": [
          { "label": "Top", "value": "0" },
          { "label": "Bottom", "value": "1" },
          { "label": "Left", "value": "2" },
          { "label": "Right", "value": "3" }
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Settings"
      },
      {
        "type": "toggle",
        "messageKey": "ANIMATION_TOGGLE",
        "label": "Enable Animations",
        "defaultValue": true
      },
      {
        "type": "slider",
        "messageKey": "ANIM_FPS",
        "defaultValue": 30,
        "label": "Animation FPS",
        "description": "Adjust the animation frames per second (10-30). Lower FPS can save battery.",
        "min": 10,
        "max": 30,
        "step": 1
      },
      {
        "type": "toggle",
        "messageKey": "INERTIA_TOGGLE",
        "label": "Inertia Effect",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "messageKey": "BATTERY_TOGGLE",
        "label": "Show Battery Indicator",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "messageKey": "INVERT_BW",
        "label": "Invert B&W (white background)",
        "defaultValue": false,
        "description": "Aplite/Diorite only: swap to white background with black text and borders.",
        "capabilities": ["BW"]
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
