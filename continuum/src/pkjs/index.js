var Clay = require('pebble-clay');
var clayConfig = require('./config');
var customClay = require('./custom-clay');
var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });

Pebble.addEventListener('showConfiguration', function (e) {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (e && !e.response) {
    return;
  }
  var dict = clay.getSettings(e.response);

  // Theme overrides
  var theme = parseInt(dict['THEME'] || dict[Pebble.Enums.THEME] || 0);
  if (theme > 0) {
    var themes = {
      1: { // Ocean Blue
        'INNER_RING_COLOR': '0x000055',
        'SUB_INNER_RING_COLOR': '0x000080',
        'MIDDLE_RING_COLOR': '0x0000AA',
        'OUTER_RING_COLOR': '0x0000FF',
        'HIGHLIGHT_FILL_COLOR': '0x00FFFF',
        'LINE_COLOR': '0xFFFFFF',
        'NUMBER_COLOR': '0xFFFFFF',
        'CENTER_TEXT_COLOR': '0xFFFFFF',
        'HIGHLIGHT_NUMBER_COLOR': '0x00FFFF'
      },
      2: { // Forest Green
        'INNER_RING_COLOR': '0x005500',
        'SUB_INNER_RING_COLOR': '0x007700',
        'MIDDLE_RING_COLOR': '0x00AA00',
        'OUTER_RING_COLOR': '0x55FF00',
        'HIGHLIGHT_FILL_COLOR': '0xFFFF00',
        'LINE_COLOR': '0xFFFFFF',
        'NUMBER_COLOR': '0xFFFFFF',
        'CENTER_TEXT_COLOR': '0xFFFFFF',
        'HIGHLIGHT_NUMBER_COLOR': '0xFFFF00'
      },
      3: { // Cyberpunk
        'INNER_RING_COLOR': '0x000000',
        'SUB_INNER_RING_COLOR': '0x2B0055',
        'MIDDLE_RING_COLOR': '0x550055',
        'OUTER_RING_COLOR': '0xAA00AA',
        'HIGHLIGHT_FILL_COLOR': '0xFF00FF',
        'LINE_COLOR': '0xFFFF00',
        'NUMBER_COLOR': '0xFFFF00',
        'CENTER_TEXT_COLOR': '0xFFFF00',
        'HIGHLIGHT_NUMBER_COLOR': '0xFF00FF'
      }
    };

    var selectedTheme = themes[theme];
    if (selectedTheme) {
      for (var key in selectedTheme) {
        // Use the numeric message key ID so we update the correct entry in the
        // dict returned by clay.getSettings() (which uses numeric keys), rather
        // than adding a separate string-keyed duplicate that may be ignored or
        // sent twice.
        var numericKey = Pebble.Enums[key];
        if (numericKey !== undefined) {
          dict[numericKey] = parseInt(selectedTheme[key]);
        } else {
          dict[key] = parseInt(selectedTheme[key]);
        }
      }
    }
  }

  // THEME is a JS-only selector; the watch has no handler for it.
  var themeKey = Pebble.Enums['THEME'];
  if (themeKey !== undefined) {
    delete dict[themeKey];
  }

  Pebble.sendAppMessage(dict, function (e) {
    console.log('Sent config data to Pebble');
  }, function (e) {
    console.log('Failed to send config data!');
    console.log(JSON.stringify(e));
  });
});
