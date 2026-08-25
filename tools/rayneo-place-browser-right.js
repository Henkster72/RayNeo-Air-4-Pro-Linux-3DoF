function isBrowser(window) {
    var text = (window.resourceClass + " " + window.resourceName + " " + window.caption).toLowerCase();
    return text.indexOf("chrome") !== -1 || text.indexOf("chromium") !== -1;
}

function sourceOutput() {
    for (var i = 0; i < workspace.screens.length; ++i) {
        var output = workspace.screens[i];
        if (output.name.indexOf("SmartGlasses") === -1 &&
            output.name.indexOf("RayNeo") === -1)
            return output;
    }
    return workspace.screens.length > 0 ? workspace.screens[0] : null;
}

var rightDesktop = workspace.desktops.length > 1 ? workspace.desktops[1] : null;
var source = sourceOutput();
var moved = 0;

if (rightDesktop) {
    workspace.stackingOrder.forEach(function(window) {
        if (isBrowser(window) && window.normalWindow) {
            window.desktops = [rightDesktop];
            if (source)
                workspace.sendClientToScreen(window, source);
            ++moved;
        }
    });
}

print("RayNeo workspace setup: moved " + moved + " Chrome/Chromium window(s) to Right");
