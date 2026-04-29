module.exports = function(minified) {
  var clayConfig = this;

  // Hide the Inertia Effect toggle while animations are disabled.
  function toggleInertia() {
    var inertiaItem = clayConfig.getItemByMessageKey('INERTIA_TOGGLE');
    if (inertiaItem) {
      if (this.get()) {
        inertiaItem.show();
      } else {
        inertiaItem.hide();
      }
    }
  }

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    // Existing animation/inertia coupling
    var animToggle = clayConfig.getItemByMessageKey('ANIMATION_TOGGLE');
    if (animToggle) {
      toggleInertia.call(animToggle);
      animToggle.on('change', toggleInertia);
    }
  });
};
