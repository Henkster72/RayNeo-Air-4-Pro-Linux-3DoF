let viewer = null;

function isViewer(window) {
    return window && window.caption.indexOf("RayNeo pinned viewport") === 0;
}

function keepViewerAbove(window) {
    if (isViewer(window))
        viewer = window;
    if (viewer) {
        viewer.keepAbove = true;
        workspace.raiseWindow(viewer);
    }
}

workspace.windowAdded.connect(keepViewerAbove);
workspace.windowActivated.connect(keepViewerAbove);
