mergeInto(LibraryManager.library, {
    jsExchangeData: function (type, count, data) {
        Module.updateWorld(type, count, data);
    },
});
