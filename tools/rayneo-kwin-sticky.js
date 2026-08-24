function keepRayNeoViewportVisible(window) {
    if (window && window.caption === "RayNeo pinned viewport") {
        window.onAllDesktops = true;
        print("RayNeo KWin integration: viewport is on all desktops");
    }
}

workspace.stackingOrder.forEach(keepRayNeoViewportVisible);
workspace.windowAdded.connect(keepRayNeoViewportVisible);
