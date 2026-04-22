module.exports = [
  {
    "type": "heading",
    "defaultValue": "Emery Watchface Configuration"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Colors"
      },
      {
        "type": "color",
        "messageKey": "INNER_RING_COLOR",
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
        "defaultValue": "0xFFFFFF",
        "label": "Line Color"
      },
      {
        "type": "color",
        "messageKey": "NUMBER_COLOR",
        "defaultValue": "0xFFFFFF",
        "label": "Number Color"
      },
      {
        "type": "color",
        "messageKey": "CENTER_TEXT_COLOR",
        "defaultValue": "0xFFFFFF",
        "label": "Center Time Text Color"
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
        "defaultValue": "Animations"
      },
      {
        "type": "toggle",
        "messageKey": "ANIMATION_TOGGLE",
        "label": "Enable Animations",
        "defaultValue": true
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
