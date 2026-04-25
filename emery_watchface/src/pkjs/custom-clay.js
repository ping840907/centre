module.exports = function(minified) {
  var clayConfig = this;

  function toggleBounce() {
    var bounceItem = clayConfig.getItemByMessageKey('INERTIA_TOGGLE');
    if (bounceItem) {
      if (this.get()) {
        bounceItem.show();
      } else {
        bounceItem.hide();
      }
    }
  }

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var animToggle = clayConfig.getItemByMessageKey('ANIMATION_TOGGLE');
    if (animToggle) {
      toggleBounce.call(animToggle);
      animToggle.on('change', toggleBounce);
    }
  });
};
