function rayneoOutput(window) {
    var preferred = readConfig("outputName", "");
    var prefixes = ["RayNeo pinned viewport - ", "RayNeo head-tracking demo - "];
    var requested = "";
    for (var p = 0; p < prefixes.length; ++p) {
        if (window.caption.indexOf(prefixes[p]) === 0) {
            requested = window.caption.substring(prefixes[p].length);
            break;
        }
    }
    var fallback = null;
    for (var i = 0; i < workspace.screens.length; ++i) {
        var output = workspace.screens[i];
        if (!fallback)
            fallback = output;
        if (requested && (output.name === requested || output.name.indexOf(requested) !== -1))
            return output;
        if (preferred && output.name.indexOf(preferred) !== -1)
            return output;
        if (output.name.indexOf("SmartGlasses") !== -1 ||
            output.name.indexOf("RayNeo") !== -1)
            return output;
    }
    return workspace.screens.length > 1 ? workspace.screens[workspace.screens.length - 1] : fallback;
}

function keepRayNeoViewportVisible(window) {
    if (window && (window.caption.indexOf("RayNeo pinned viewport") === 0 ||
                   window.caption.indexOf("RayNeo head-tracking demo") === 0)) {
        window.onAllDesktops = true;
        var output = rayneoOutput(window);
        if (output) {
            workspace.sendClientToScreen(window, output);
            print("RayNeo KWin integration: viewport pinned to " + output.name + " on all desktops");
        } else {
            print("RayNeo KWin integration: no target output found");
        }
    }
}

function watchRayNeoViewport(window) {
    if (!window || (window.caption.indexOf("RayNeo pinned viewport") !== 0 &&
                    window.caption.indexOf("RayNeo head-tracking demo") !== 0))
        return;

    keepRayNeoViewportVisible(window);
    // SDL negotiates fullscreen after the Wayland window is created. KWin may
    // therefore move it again; re-pin after those state changes.
    window.outputChanged.connect(function() {
        keepRayNeoViewportVisible(window);
    });
    window.fullScreenChanged.connect(function() {
        keepRayNeoViewportVisible(window);
    });
}

workspace.stackingOrder.forEach(watchRayNeoViewport);
workspace.windowAdded.connect(watchRayNeoViewport);
