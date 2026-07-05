// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "maki-classes.h"

namespace qtWasabi::Maki {

// Flat binding registry (maki-bindings.cpp) — the migration fallback.
void *makiFlatLookup(const wchar_t *name, int *nparams);

namespace {

int ciCmp(const wchar_t *a, const wchar_t *b) {
    while (*a && *b) {
        wchar_t la = *a, lb = *b;
        if (la >= L'A' && la <= L'Z') la = wchar_t(la - L'A' + L'a');
        if (lb >= L'A' && lb <= L'Z') lb = wchar_t(lb - L'A' + L'a');
        if (la != lb) return int(la) - int(lb);
        ++a; ++b;
    }
    return int(*a) - int(*b);
}

}  // namespace

// Per-class split bodies (maki-bindings.cpp).  Declared opaquely —
// the table stores addresses only; callDLF casts to the real Maki
// method ABI at the call site, exactly as it does for the flat rows.
extern "C" {
void wq_sliderGetPosition();
void wq_sliderSetPosition();
void wq_frameGetPosition();
void wq_frameSetPosition();
void wq_systemGetPosition();
void wq_menuAddCommand();
void wq_menuAddSeparator();
void wq_menuGetNumCommands();
void wq_menuCheckCommand();
void wq_menuDisableCommand();
void wq_menuPopAtMouse();
void wq_menuPopAtXY();
void wq_listAddItem();
void wq_listRemoveItem();
void wq_listEnumItem();
void wq_listGetNumItems();
void wq_listFindItem();
void wq_listFindItem2();
void wq_listRemoveAll();
void wq_bitlistGetItem();
void wq_bitlistSetItem();
void wq_bitlistGetSize();
void wq_bitlistSetSize();
void wq_mapLoadMap();
void wq_mapGetValue();
void wq_mapGetARGBValue();
void wq_mapInRegion();
void wq_mapGetWidth();
void wq_mapGetHeight();
void wq_mapGetRegion();
void wq_regionAdd();
void wq_regionSub();
void wq_regionOffset();
void wq_regionStretch();
void wq_regionCopy();
void wq_regionLoadFromBitmap();
void wq_regionLoadFromMap();
void wq_regionGetBoundingBoxX();
void wq_regionGetBoundingBoxY();
void wq_regionGetBoundingBoxW();
void wq_regionGetBoundingBoxH();
}

#define FN(f) reinterpret_cast<void *>(&f)

namespace {

// ── Per-class method tables ─────────────────────────────────────────
// Transcribed from the reference exportedFunction tables (name +
// parameter count only — the API surface every compiled skin binds
// against).  Rows with a null pointer resolve their body AND arity
// through the flat table when it carries the name; otherwise they are
// honestly unbound at the reference arity, which keeps the operand
// stack aligned for methods without an implementation yet.  Explicit
// FN(...) rows are the per-class splits of names that collide across
// classes.  kSystemObject carries a literal duplicate getStatus row —
// the reference table does too; the first match wins.

const ClassMethod kObject[] = {
    { L"getClassName", 0, nullptr },
    { L"notify",       4, nullptr },
    { L"onNotify",     4, nullptr },
};

const ClassMethod kSystemObject[] = {
    { L"getRuntimeVersion", 0, nullptr },
    { L"onScriptLoaded", 0, nullptr },
    { L"onScriptUnloading", 0, nullptr },
    { L"onQuit", 0, nullptr },
    { L"onKeyDown", 1, nullptr },
    { L"onKeyUp", 1, nullptr },
    { L"onAccelerator", 3, nullptr },
    { L"getMousePosX", 0, nullptr },
    { L"getMousePosY", 0, nullptr },
    { L"isMinimized", 0, nullptr },
    { L"restoreApplication", 0, nullptr },
    { L"activateApplication", 0, nullptr },
    { L"minimizeApplication", 0, nullptr },
    { L"isDesktopAlphaAvailable", 0, nullptr },
    { L"isTransparencyAvailable", 0, nullptr },
    { L"integerToString", 1, nullptr },
    { L"stringToInteger", 1, nullptr },
    { L"floatToString", 2, nullptr },
    { L"stringToFloat", 1, nullptr },
    { L"integerToTime", 1, nullptr },
    { L"integerToLongTime", 1, nullptr },
    { L"dateToTime", 1, nullptr },
    { L"dateToLongTime", 1, nullptr },
    { L"formatDate", 1, nullptr },
    { L"formatLongDate", 1, nullptr },
    { L"getDateYear", 1, nullptr },
    { L"getDateMonth", 1, nullptr },
    { L"getDateDay", 1, nullptr },
    { L"getDateDow", 1, nullptr },
    { L"getDateDoy", 1, nullptr },
    { L"getDateHour", 1, nullptr },
    { L"getDateMin", 1, nullptr },
    { L"getDateSec", 1, nullptr },
    { L"getDateDst", 1, nullptr },
    { L"getDate", 0, nullptr },
    { L"StrMid", 3, nullptr },
    { L"StrLeft", 2, nullptr },
    { L"StrRight", 2, nullptr },
    { L"StrSearch", 2, nullptr },
    { L"StrLen", 1, nullptr },
    { L"StrUpper", 1, nullptr },
    { L"StrLower", 1, nullptr },
    { L"UrlEncode", 1, nullptr },
    { L"UrlDecode", 1, nullptr },
    { L"RemovePath", 1, nullptr },
    { L"GetPath", 1, nullptr },
    { L"GetExtension", 1, nullptr },
    { L"getToken", 3, nullptr },
    { L"sin", 1, nullptr },
    { L"cos", 1, nullptr },
    { L"tan", 1, nullptr },
    { L"asin", 1, nullptr },
    { L"acos", 1, nullptr },
    { L"atan", 1, nullptr },
    { L"atan2", 2, nullptr },
    { L"pow", 2, nullptr },
    { L"sqr", 1, nullptr },
    { L"sqrt", 1, nullptr },
    { L"random", 1, nullptr },
    { L"integer", 1, nullptr },
    { L"frac", 1, nullptr },
    { L"ln", 1, nullptr },
    { L"log10", 1, nullptr },
    { L"getParam", 0, nullptr },
    { L"getViewportWidth", 0, nullptr },
    { L"getViewportHeight", 0, nullptr },
    { L"getViewportLeft", 0, nullptr },
    { L"getViewportTop", 0, nullptr },
    { L"getViewportWidthFromPoint", 2, nullptr },
    { L"getViewportHeightFromPoint", 2, nullptr },
    { L"getViewportLeftFromPoint", 2, nullptr },
    { L"getViewportTopFromPoint", 2, nullptr },
    { L"getViewportWidthFromGuiObject", 1, nullptr },
    { L"getViewportHeightFromGuiObject", 1, nullptr },
    { L"getViewportLeftFromGuiObject", 1, nullptr },
    { L"getViewportTopFromGuiObject", 1, nullptr },
    { L"onViewPortChanged", 2, nullptr },
    { L"debugString", 2, nullptr },
    { L"isObjectValid", 1, nullptr },
    { L"getTimeOfDay", 0, nullptr },
    { L"navigateUrl", 1, nullptr },
    { L"navigateUrlBrowser", 1, nullptr },
    { L"isKeyDown", 1, nullptr },
    { L"setClipboardText", 1, nullptr },
    { L"Chr", 1, nullptr },
    { L"triggerAction", 3, nullptr },
    { L"messageBox", 4, nullptr },
    { L"setAtom", 2, nullptr },
    { L"getAtom", 1, nullptr },
    { L"invokeDebugger", 0, nullptr },
    { L"newGroup", 1, nullptr },
    { L"onSetXuiParam", 2, nullptr },
    { L"getScriptGroup", 0, nullptr },
    { L"getSkinName", 0, nullptr },
    { L"newGroupAsLayout", 1, nullptr },
    { L"getNumContainers", 0, nullptr },
    { L"enumContainer", 1, nullptr },
    { L"onCreateLayout", 1, nullptr },
    { L"onShowLayout", 1, nullptr },
    { L"onHideLayout", 1, nullptr },
    { L"switchSkin", 1, nullptr },
    { L"isLoadingSkin", 0, nullptr },
    { L"lockUI", 0, nullptr },
    { L"unlockUI", 0, nullptr },
    { L"getContainer", 1, nullptr },
    { L"newDynamicContainer", 1, nullptr },
    { L"onGetCancelComponent", 2, nullptr },
    { L"onLookForComponent", 1, nullptr },
    { L"isAppActive", 0, nullptr },
    { L"showWindow", 3, nullptr },
    { L"hideWindow", 1, nullptr },
    { L"hideNamedWindow", 1, nullptr },
    { L"isNamedWindowVisible", 1, nullptr },
    { L"getCurAppLeft", 0, nullptr },
    { L"getCurAppTop", 0, nullptr },
    { L"getCurAppWidth", 0, nullptr },
    { L"getCurAppHeight", 0, nullptr },
    { L"setPrivateString", 3, nullptr },
    { L"setPrivateInt", 3, nullptr },
    { L"getPrivateString", 3, nullptr },
    { L"getPrivateInt", 3, nullptr },
    { L"setPublicString", 2, nullptr },
    { L"setPublicInt", 2, nullptr },
    { L"getPublicString", 2, nullptr },
    { L"getPublicInt", 2, nullptr },
    { L"getPlayItemString", 0, nullptr },
    { L"getPlayItemLength", 0, nullptr },
    { L"getPlayItemMetadataString", 1, nullptr },
    { L"getMetadataString", 2, nullptr },
    { L"getPlayItemDisplayTitle", 0, nullptr },
    { L"getExtFamily", 1, nullptr },
    { L"getDecoderName", 1, nullptr },
    { L"playFile", 1, nullptr },
    { L"enqueueFile", 1, nullptr },
    { L"clearPlaylist", 0, nullptr },
    { L"onStop", 0, nullptr },
    { L"onPlay", 0, nullptr },
    { L"onPause", 0, nullptr },
    { L"onResume", 0, nullptr },
    { L"onTitleChange", 1, nullptr },
    { L"onTitle2Change", 1, nullptr },
    { L"onUrlChange", 1, nullptr },
    { L"onInfoChange", 1, nullptr },
    { L"onStatusMsg", 1, nullptr },
    { L"getLeftVuMeter", 0, nullptr },
    { L"getRightVuMeter", 0, nullptr },
    { L"getVisBand", 2, nullptr },
    { L"getVolume", 0, nullptr },
    { L"setVolume", 1, nullptr },
    { L"play", 0, nullptr },
    { L"stop", 0, nullptr },
    { L"pause", 0, nullptr },
    { L"next", 0, nullptr },
    { L"previous", 0, nullptr },
    { L"eject", 0, nullptr },
    { L"seekTo", 1, nullptr },
    { L"getPosition", 0, FN(wq_systemGetPosition) },
    { L"setEqBand", 2, nullptr },
    { L"setEqPreAmp", 1, nullptr },
    { L"setEq", 1, nullptr },
    { L"getEqBand", 1, nullptr },
    { L"getEqPreAmp", 0, nullptr },
    { L"getEq", 0, nullptr },
    { L"onEqBandChanged", 2, nullptr },
    { L"onEqFreqChanged", 1, nullptr },
    { L"onEqPreAmpChanged", 1, nullptr },
    { L"onEqChanged", 1, nullptr },
    { L"onVolumeChanged", 1, nullptr },
    { L"onSeek", 1, nullptr },
    { L"getStatus", 0, nullptr },
    { L"getStatus", 0, nullptr },
    { L"getSongInfoText", 0, nullptr },
    { L"getSongInfoTextTranslated", 0, nullptr },
    { L"hasVideoSupport", 0, nullptr },
    { L"isVideo", 0, nullptr },
    { L"isVideoFullscreen", 0, nullptr },
    { L"setVideoFullscreen", 1, nullptr },
    { L"getIdealVideoWidth", 0, nullptr },
    { L"getIdealVideoHeight", 0, nullptr },
    { L"getPlaylistIndex", 0, nullptr },
    { L"onShowNotification", 0, nullptr },
    { L"getPlaylistLength", 0, nullptr },
    { L"getCurrentTrackRating", 0, nullptr },
    { L"setCurrentTrackRating", 1, nullptr },
    { L"getWac", 1, nullptr },
    { L"ddesend", 3, nullptr },
    { L"setMenuTransparency", 1, nullptr },
    { L"popMainBrowser", 0, nullptr },
    { L"getMainBrowser", 0, nullptr },
    { L"windowMenu", 0, nullptr },
    { L"systemMenu", 0, nullptr },
    { L"selectFile", 3, nullptr },
    { L"selectFolder", 3, nullptr },
    { L"onOpenURL", 1, nullptr },
    { L"getMonitorLeft", 0, nullptr },
    { L"getMonitorTop", 0, nullptr },
    { L"getMonitorLeftFromPoint", 2, nullptr },
    { L"getMonitorTopFromPoint", 2, nullptr },
    { L"getMonitorLeftFromGuiObject", 1, nullptr },
    { L"getMonitorTopFromGuiObject", 1, nullptr },
    { L"getMonitorWidth", 0, nullptr },
    { L"getMonitorHeight", 0, nullptr },
    { L"getMonitorWidthFromPoint", 2, nullptr },
    { L"getMonitorHeightFromPoint", 2, nullptr },
    { L"getMonitorWidthFromGuiObject", 1, nullptr },
    { L"getMonitorHeightFromGuiObject", 1, nullptr },
    { L"downloadURL", 3, nullptr },
    { L"downloadMedia", 4, nullptr },
    { L"onDownloadFinished", 3, nullptr },
    { L"getDownloadPath", 0, nullptr },
    { L"setDownloadPath", 1, nullptr },
    { L"getAlbumArt", 1, nullptr },
    { L"isProVersion", 0, nullptr },
    { L"enumEmbedGUID", 1, nullptr },
    { L"getWinampVersion", 0, nullptr },
    { L"getBuildNumber", 0, nullptr },
    { L"getFileSize", 1, nullptr },
    { L"getString", 2, nullptr },
    { L"translate", 1, nullptr },
    { L"getLanguageId", 0, nullptr },
};

const ClassMethod kGuiObject[] = {
    { L"getId", 0, nullptr },
    { L"show", 0, nullptr },
    { L"hide", 0, nullptr },
    { L"onSetVisible", 1, nullptr },
    { L"isVisible", 0, nullptr },
    { L"setAlpha", 1, nullptr },
    { L"getAlpha", 0, nullptr },
    { L"setActiveAlpha", 1, nullptr },
    { L"getActiveAlpha", 0, nullptr },
    { L"setInactiveAlpha", 1, nullptr },
    { L"getInactiveAlpha", 0, nullptr },
    { L"onLeftButtonDown", 2, nullptr },
    { L"onLeftButtonUp", 2, nullptr },
    { L"onRightButtonDown", 2, nullptr },
    { L"onRightButtonUp", 2, nullptr },
    { L"onRightButtonDblClk", 2, nullptr },
    { L"onLeftButtonDblClk", 2, nullptr },
    { L"onMouseWheelUp", 2, nullptr },
    { L"onMouseWheelDown", 2, nullptr },
    { L"onMouseMove", 2, nullptr },
    { L"onEnterArea", 0, nullptr },
    { L"onLeaveArea", 0, nullptr },
    { L"isMouseOverRect", 0, nullptr },
    { L"onStartup", 0, nullptr },
    { L"onChar", 1, nullptr },
    { L"onKeyDown", 1, nullptr },
    { L"onKeyUp", 1, nullptr },
    { L"setEnabled", 1, nullptr },
    { L"getEnabled", 0, nullptr },
    { L"onEnable", 1, nullptr },
    { L"resize", 4, nullptr },
    { L"onResize", 4, nullptr },
    { L"isMouseOver", 2, nullptr },
    { L"getLeft", 0, nullptr },
    { L"getTop", 0, nullptr },
    { L"getWidth", 0, nullptr },
    { L"getHeight", 0, nullptr },
    { L"getGuiX", 0, nullptr },
    { L"getGuiY", 0, nullptr },
    { L"getGuiW", 0, nullptr },
    { L"getGuiH", 0, nullptr },
    { L"getGuiRelatX", 0, nullptr },
    { L"getGuiRelatY", 0, nullptr },
    { L"getGuiRelatW", 0, nullptr },
    { L"getGuiRelatH", 0, nullptr },
    { L"clientToScreenX", 1, nullptr },
    { L"clientToScreenY", 1, nullptr },
    { L"clientToScreenW", 1, nullptr },
    { L"clientToScreenH", 1, nullptr },
    { L"screenToClientX", 1, nullptr },
    { L"screenToClientY", 1, nullptr },
    { L"screenToClientW", 1, nullptr },
    { L"screenToClientH", 1, nullptr },
    { L"setTargetX", 1, nullptr },
    { L"setTargetY", 1, nullptr },
    { L"setTargetW", 1, nullptr },
    { L"setTargetH", 1, nullptr },
    { L"setTargetA", 1, nullptr },
    { L"setTargetSpeed", 1, nullptr },
    { L"gotoTarget", 0, nullptr },
    { L"onTargetReached", 0, nullptr },
    { L"cancelTarget", 0, nullptr },
    { L"reverseTarget", 1, nullptr },
    { L"isGoingToTarget", 0, nullptr },
    { L"setXmlParam", 2, nullptr },
    { L"getXmlParam", 1, nullptr },
    { L"init", 1, nullptr },
    { L"bringToFront", 0, nullptr },
    { L"bringToBack", 0, nullptr },
    { L"bringAbove", 1, nullptr },
    { L"bringBelow", 1, nullptr },
    { L"isActive", 0, nullptr },
    { L"getParent", 0, nullptr },
    { L"getTopParent", 0, nullptr },
    { L"getInterface", 1, nullptr },
    { L"onAction", 7, nullptr },
    { L"getParentLayout", 0, nullptr },
    { L"runModal", 0, nullptr },
    { L"endModal", 1, nullptr },
    { L"popParentLayout", 0, nullptr },
    { L"setStatusText", 2, nullptr },
    { L"findObject", 1, nullptr },
    { L"findObjectXY", 2, nullptr },
    { L"getName", 0, nullptr },
    { L"getAutoWidth", 0, nullptr },
    { L"getAutoHeight", 0, nullptr },
    { L"setFocus", 0, nullptr },
    { L"onGetFocus", 0, nullptr },
    { L"onKillFocus", 0, nullptr },
    { L"sendAction", 6, nullptr },
    { L"onAccelerator", 1, nullptr },
    { L"cfg_getInt", 0, nullptr },
    { L"cfg_setInt", 1, nullptr },
    { L"cfg_getFloat", 0, nullptr },
    { L"cfg_setFloat", 1, nullptr },
    { L"cfg_getString", 0, nullptr },
    { L"cfg_setString", 1, nullptr },
    { L"cfg_onDataChanged", 0, nullptr },
    { L"cfg_getItemGuid", 0, nullptr },
    { L"cfg_getAttributeName", 0, nullptr },
    { L"onDragEnter", 0, nullptr },
    { L"onDragOver", 2, nullptr },
    { L"onDragLeave", 0, nullptr },
};

const ClassMethod kGroupRef[] = {
    { L"getObject", 1, nullptr },
    { L"enumObject", 1, nullptr },
    { L"getNumObjects", 0, nullptr },
    { L"onCreateObject", 1, nullptr },
    { L"getMousePosX", 0, nullptr },
    { L"getMousePosY", 0, nullptr },
    { L"isLayout", 0, nullptr },
    { L"autoResize", 0, nullptr },
};

const ClassMethod kLayoutRef[] = {
    { L"onDock", 1, nullptr },
    { L"onUndock", 0, nullptr },
    { L"getScale", 0, nullptr },
    { L"setScale", 1, nullptr },
    { L"onScale", 1, nullptr },
    { L"setDesktopAlpha", 1, nullptr },
    { L"getDesktopAlpha", 0, nullptr },
    { L"isTransparencySafe", 0, nullptr },
    { L"isLayoutAnimationSafe", 0, nullptr },
    { L"getContainer", 0, nullptr },
    { L"center", 0, nullptr },
    { L"onMove", 0, nullptr },
    { L"onEndMove", 0, nullptr },
    { L"snapAdjust", 4, nullptr },
    { L"getSnapAdjustTop", 0, nullptr },
    { L"getSnapAdjustLeft", 0, nullptr },
    { L"getSnapAdjustRight", 0, nullptr },
    { L"getSnapAdjustBottom", 0, nullptr },
    { L"onUserResize", 4, nullptr },
    { L"setRedrawOnResize", 1, nullptr },
    { L"beforeRedock", 0, nullptr },
    { L"redock", 0, nullptr },
    { L"onMouseEnterLayout", 0, nullptr },
    { L"onMouseLeaveLayout", 0, nullptr },
    { L"onSnapAdjustChanged", 0, nullptr },
};

const ClassMethod kContainerRef[] = {
    { L"onSwitchToLayout", 1, nullptr },
    { L"onBeforeSwitchToLayout", 2, nullptr },
    { L"onHideLayout", 1, nullptr },
    { L"onShowLayout", 1, nullptr },
    { L"getLayout", 1, nullptr },
    { L"getNumLayouts", 0, nullptr },
    { L"enumLayout", 1, nullptr },
    { L"getCurLayout", 0, nullptr },
    { L"switchToLayout", 1, nullptr },
    { L"isDynamic", 0, nullptr },
    { L"show", 0, nullptr },
    { L"hide", 0, nullptr },
    { L"close", 0, nullptr },
    { L"toggle", 0, nullptr },
    { L"setName", 1, nullptr },
    { L"getName", 0, nullptr },
    { L"getGuid", 0, nullptr },
    { L"setXmlParam", 2, nullptr },
    { L"onAddContent", 3, nullptr },
};

const ClassMethod kButtonRef[] = {
    { L"onActivate", 1, nullptr },
    { L"setActivated", 1, nullptr },
    { L"getActivated", 0, nullptr },
    { L"onLeftClick", 0, nullptr },
    { L"onRightClick", 0, nullptr },
    { L"leftClick", 0, nullptr },
    { L"rightClick", 0, nullptr },
    { L"setActivatedNoCallback", 1, nullptr },
};

const ClassMethod kToggleButtonRef[] = {
    { L"onToggle", 1, nullptr },
    { L"getCurCfgVal", 0, nullptr },
};

const ClassMethod kLayer[] = {
    { L"setRegionFromMap", 3, nullptr },
    { L"setRegion", 1, nullptr },
    { L"isInvalid", 0, nullptr },
    { L"onBeginResize", 4, nullptr },
    { L"onEndResize", 4, nullptr },
    { L"fx_setEnabled", 1, nullptr },
    { L"fx_getEnabled", 0, nullptr },
    { L"fx_onInit", 0, nullptr },
    { L"fx_onFrame", 0, nullptr },
    { L"fx_onGetPixelR", 4, nullptr },
    { L"fx_onGetPixelD", 4, nullptr },
    { L"fx_onGetPixelX", 4, nullptr },
    { L"fx_onGetPixelY", 4, nullptr },
    { L"fx_onGetPixelA", 4, nullptr },
    { L"fx_setWrap", 1, nullptr },
    { L"fx_getWrap", 0, nullptr },
    { L"fx_setRect", 1, nullptr },
    { L"fx_getRect", 0, nullptr },
    { L"fx_setBgFx", 1, nullptr },
    { L"fx_getBgFx", 0, nullptr },
    { L"fx_setClear", 1, nullptr },
    { L"fx_getClear", 0, nullptr },
    { L"fx_setSpeed", 1, nullptr },
    { L"fx_getSpeed", 0, nullptr },
    { L"fx_setRealtime", 1, nullptr },
    { L"fx_getRealtime", 0, nullptr },
    { L"fx_setLocalized", 1, nullptr },
    { L"fx_getLocalized", 0, nullptr },
    { L"fx_setBilinear", 1, nullptr },
    { L"fx_getBilinear", 0, nullptr },
    { L"fx_setAlphaMode", 1, nullptr },
    { L"fx_getAlphaMode", 0, nullptr },
    { L"fx_setGridSize", 2, nullptr },
    { L"fx_update", 0, nullptr },
    { L"fx_restart", 0, nullptr },
};

const ClassMethod kAnimatedLayer[] = {
    { L"setSpeed", 1, nullptr },
    { L"gotoFrame", 1, nullptr },
    { L"setStartFrame", 1, nullptr },
    { L"setEndFrame", 1, nullptr },
    { L"setAutoReplay", 1, nullptr },
    { L"play", 0, nullptr },
    { L"togglePause", 0, nullptr },
    { L"stop", 0, nullptr },
    { L"pause", 0, nullptr },
    { L"isPlaying", 0, nullptr },
    { L"isPaused", 0, nullptr },
    { L"isStopped", 0, nullptr },
    { L"getStartFrame", 0, nullptr },
    { L"getEndFrame", 0, nullptr },
    { L"getLength", 0, nullptr },
    { L"getDirection", 0, nullptr },
    { L"getAutoReplay", 0, nullptr },
    { L"getCurFrame", 0, nullptr },
    { L"onPlay", 0, nullptr },
    { L"onPause", 0, nullptr },
    { L"onResume", 0, nullptr },
    { L"onStop", 0, nullptr },
    { L"onFrame", 1, nullptr },
    { L"setRealtime", 1, nullptr },
};

const ClassMethod kTextRef[] = {
    { L"setText", 1, nullptr },
    { L"setAlternateText", 1, nullptr },
    { L"getText", 0, nullptr },
    { L"getTextWidth", 0, nullptr },
    { L"onTextChanged", 1, nullptr },
};

const ClassMethod kEdit[] = {
    { L"setText", 1, nullptr },
    { L"setAutoEnter", 1, nullptr },
    { L"getAutoEnter", 0, nullptr },
    { L"getText", 0, nullptr },
    { L"onEnter", 0, nullptr },
    { L"onAbort", 0, nullptr },
    { L"onIdleEditUpdate", 0, nullptr },
    { L"onEditUpdate", 0, nullptr },
    { L"selectAll", 0, nullptr },
    { L"enter", 0, nullptr },
    { L"setIdleEnabled", 1, nullptr },
    { L"getIdleEnabled", 0, nullptr },
};

const ClassMethod kSliderRef[] = {
    { L"setPosition", 1, FN(wq_sliderSetPosition) },
    { L"getPosition", 0, FN(wq_sliderGetPosition) },
    { L"onSetPosition", 1, nullptr },
    { L"onPostedPosition", 1, nullptr },
    { L"onSetFinalPosition", 1, nullptr },
    { L"lock", 0, nullptr },
    { L"unlock", 0, nullptr },
};

const ClassMethod kVis[] = {
    { L"onFrame", 0, nullptr },
    { L"setRealtime", 1, nullptr },
    { L"getRealtime", 0, nullptr },
    { L"setMode", 1, nullptr },
    { L"getMode", 0, nullptr },
    { L"nextMode", 0, nullptr },
};

const ClassMethod kComponentBucket[] = {
    { L"getMaxWidth", 0, nullptr },
    { L"getMaxHeight", 0, nullptr },
    { L"getScroll", 0, nullptr },
    { L"setScroll", 1, nullptr },
    { L"getNumChildren", 0, nullptr },
    { L"enumChildren", 1, nullptr },
    { L"fake", 0, nullptr },
};

const ClassMethod kGuiList[] = {
    { L"getNumItems", 0, nullptr },
    { L"getWantAutoDeselect", 0, nullptr },
    { L"setWantAutoDeselect", 1, nullptr },
    { L"onSetVisible", 1, nullptr },
    { L"setAutoSort", 1, nullptr },
    { L"next", 0, nullptr },
    { L"selectCurrent", 0, nullptr },
    { L"selectFirstEntry", 0, nullptr },
    { L"previous", 0, nullptr },
    { L"pagedown", 0, nullptr },
    { L"pageup", 0, nullptr },
    { L"home", 0, nullptr },
    { L"end", 0, nullptr },
    { L"reset", 0, nullptr },
    { L"addColumn", 3, nullptr },
    { L"getNumColumns", 0, nullptr },
    { L"getColumnWidth", 1, nullptr },
    { L"setColumnWidth", 2, nullptr },
    { L"getColumnLabel", 1, nullptr },
    { L"setColumnLabel", 2, nullptr },
    { L"getColumnNumeric", 1, nullptr },
    { L"setColumnDynamic", 2, nullptr },
    { L"isColumnDynamic", 1, nullptr },
    { L"setMinimumSize", 1, nullptr },
    { L"addItem", 1, nullptr },
    { L"insertItem", 2, nullptr },
    { L"getLastAddedItemPos", 0, nullptr },
    { L"setSubItem", 3, nullptr },
    { L"deleteAllItems", 0, nullptr },
    { L"deleteByPos", 1, nullptr },
    { L"getItemLabel", 2, nullptr },
    { L"setItemLabel", 2, nullptr },
    { L"setItemIcon", 2, nullptr },
    { L"getItemIcon", 1, nullptr },
    { L"setShowIcons", 1, nullptr },
    { L"getShowIcons", 0, nullptr },
    { L"setIconWidth", 1, nullptr },
    { L"getIconWidth", 0, nullptr },
    { L"setIconHeight", 1, nullptr },
    { L"getIconHeight", 0, nullptr },
    { L"onIconLeftclick", 3, nullptr },
    { L"getItemSelected", 1, nullptr },
    { L"isItemFocused", 1, nullptr },
    { L"getItemFocused", 0, nullptr },
    { L"setItemFocused", 1, nullptr },
    { L"ensureItemVisible", 1, nullptr },
    { L"invalidateColumns", 0, nullptr },
    { L"scrollAbsolute", 1, nullptr },
    { L"scrollRelative", 1, nullptr },
    { L"scrollLeft", 1, nullptr },
    { L"scrollRight", 1, nullptr },
    { L"scrollUp", 1, nullptr },
    { L"scrollDown", 1, nullptr },
    { L"getSubitemText", 2, nullptr },
    { L"getFirstItemSelected", 0, nullptr },
    { L"getNextItemSelected", 1, nullptr },
    { L"selectAll", 0, nullptr },
    { L"deselectAll", 0, nullptr },
    { L"invertSelection", 0, nullptr },
    { L"invalidateItem", 1, nullptr },
    { L"getFirstItemVisible", 0, nullptr },
    { L"getLastItemVisible", 0, nullptr },
    { L"setFontSize", 1, nullptr },
    { L"getFontSize", 0, nullptr },
    { L"jumpToNext", 1, nullptr },
    { L"scrollToItem", 1, nullptr },
    { L"resort", 0, nullptr },
    { L"getSortDirection", 0, nullptr },
    { L"getSortColumn", 0, nullptr },
    { L"setSortColumn", 1, nullptr },
    { L"setSortDirection", 1, nullptr },
    { L"getItemCount", 0, nullptr },
    { L"setSelectionStart", 1, nullptr },
    { L"setSelectionEnd", 1, nullptr },
    { L"setSelected", 2, nullptr },
    { L"toggleSelection", 2, nullptr },
    { L"getHeaderHeight", 0, nullptr },
    { L"getPreventMultipleSelection", 0, nullptr },
    { L"setPreventMultipleSelection", 1, nullptr },
    { L"moveItem", 2, nullptr },
    { L"onSelectAll", 0, nullptr },
    { L"onDelete", 0, nullptr },
    { L"onDoubleClick", 1, nullptr },
    { L"onLeftClick", 1, nullptr },
    { L"onSecondLeftClick", 1, nullptr },
    { L"onRightClick", 1, nullptr },
    { L"onColumnDblClick", 3, nullptr },
    { L"onColumnLabelClick", 3, nullptr },
    { L"onItemSelection", 2, nullptr },
};

const ClassMethod kFrameRef[] = {
    { L"setPosition", 1, FN(wq_frameSetPosition) },
    { L"getPosition", 0, FN(wq_frameGetPosition) },
};

const ClassMethod kWindowHolder[] = {
    { L"getGUID", 1, nullptr },
    { L"setRegionFromMap", 3, nullptr },
    { L"setRegion", 1, nullptr },
    { L"getContent", 0, nullptr },
    { L"getComponentName", 0, nullptr },
};

const ClassMethod kLayoutStatus[] = {
    { L"callme", 1, nullptr },
};

const ClassMethod kTimerRef[] = {
    { L"setDelay", 1, nullptr },
    { L"getDelay", 0, nullptr },
    { L"start", 0, nullptr },
    { L"stop", 0, nullptr },
    { L"onTimer", 0, nullptr },
    { L"isRunning", 0, nullptr },
    { L"getSkipped", 0, nullptr },
};

const ClassMethod kPopupMenu[] = {
    { L"addSubMenu", 2, nullptr },
    { L"addCommand", 4, FN(wq_menuAddCommand) },
    { L"addSeparator", 0, FN(wq_menuAddSeparator) },
    { L"popAtXY", 2, FN(wq_menuPopAtXY) },
    { L"popAtMouse", 0, FN(wq_menuPopAtMouse) },
    { L"getNumCommands", 0, FN(wq_menuGetNumCommands) },
    { L"checkCommand", 2, FN(wq_menuCheckCommand) },
    { L"disableCommand", 2, FN(wq_menuDisableCommand) },
};

const ClassMethod kList[] = {
    { L"addItem", 1, FN(wq_listAddItem) },
    { L"removeItem", 1, FN(wq_listRemoveItem) },
    { L"enumItem", 1, FN(wq_listEnumItem) },
    { L"findItem2", 2, FN(wq_listFindItem2) },
    { L"findItem", 1, FN(wq_listFindItem) },
    { L"getNumItems", 0, FN(wq_listGetNumItems) },
    { L"removeAll", 0, FN(wq_listRemoveAll) },
};

const ClassMethod kRegion[] = {
    { L"add", 1, FN(wq_regionAdd) },
    { L"sub", 1, FN(wq_regionSub) },
    { L"offset", 2, FN(wq_regionOffset) },
    { L"stretch", 1, FN(wq_regionStretch) },
    { L"copy", 1, FN(wq_regionCopy) },
    { L"loadFromMap", 3, FN(wq_regionLoadFromMap) },
    { L"loadFromBitmap", 1, FN(wq_regionLoadFromBitmap) },
    { L"getBoundingBoxX", 0, FN(wq_regionGetBoundingBoxX) },
    { L"getBoundingBoxY", 0, FN(wq_regionGetBoundingBoxY) },
    { L"getBoundingBoxW", 0, FN(wq_regionGetBoundingBoxW) },
    { L"getBoundingBoxH", 0, FN(wq_regionGetBoundingBoxH) },
};

const ClassMethod kMap[] = {
    { L"getValue", 2, FN(wq_mapGetValue) },
    { L"getARGBValue", 3, FN(wq_mapGetARGBValue) },
    { L"inRegion", 2, FN(wq_mapInRegion) },
    { L"loadMap", 1, FN(wq_mapLoadMap) },
    { L"getWidth", 0, FN(wq_mapGetWidth) },
    { L"getHeight", 0, FN(wq_mapGetHeight) },
    { L"getRegion", 0, FN(wq_mapGetRegion) },
};

const ClassMethod kBitList[] = {
    { L"getItem", 1, FN(wq_bitlistGetItem) },
    { L"setItem", 2, FN(wq_bitlistSetItem) },
    { L"setSize", 1, FN(wq_bitlistSetSize) },
    { L"getSize", 0, FN(wq_bitlistGetSize) },
};

const ClassMethod kFile[] = {
    { L"load", 1, nullptr },
    { L"getSize", 0, nullptr },
    { L"exists", 0, nullptr },
};

const ClassMethod kXmlDoc[] = {
    { L"parser_addCallback", 1, nullptr },
    { L"parser_start", 0, nullptr },
    { L"parser_destroy", 0, nullptr },
    { L"parser_onCallback", 4, nullptr },
    { L"parser_onCloseCallback", 2, nullptr },
    { L"parser_onError", 5, nullptr },
};

const ClassMethod kPrivate[] = {
    { L"updateLinks", 2, nullptr },
    { L"onLinksUpdated", 1, nullptr },
};

const ClassMethod kApplicationRef[] = {
    { L"GetApplicationName", 0, nullptr },
    { L"GetVersionString", 0, nullptr },
    { L"GetVersionNumberString", 0, nullptr },
    { L"GetBuildNumber", 0, nullptr },
    { L"GetGUID", 0, nullptr },
    { L"GetCommandLine", 0, nullptr },
    { L"Shutdown", 0, nullptr },
    { L"CancelShutdown", 0, nullptr },
    { L"IsShuttingDown", 0, nullptr },
    { L"GetApplicationPath", 0, nullptr },
    { L"GetSettingsPath", 0, nullptr },
    { L"GetWorkingPath", 0, nullptr },
    { L"SetWorkingPath", 1, nullptr },
    { L"GetMachineGUID", 0, nullptr },
    { L"GetUserGUID", 0, nullptr },
    { L"GetSessionGUID", 0, nullptr },
};

const ClassMethod kWac[] = {
    { L"getGUID", 1, nullptr },
    { L"getName", 1, nullptr },
    { L"onNotify", 3, nullptr },
    { L"sendCommand", 4, nullptr },
    { L"show", 0, nullptr },
    { L"hide", 0, nullptr },
    { L"isVisible", 0, nullptr },
    { L"onShow", 0, nullptr },
    { L"onHide", 0, nullptr },
    { L"setStatusBar", 1, nullptr },
    { L"getStatusBar", 0, nullptr },
};

const ClassMethod kPlEdit[] = {
    { L"showCurrentlyPlayingTrack", 0, nullptr },
    { L"showTrack", 1, nullptr },
    { L"getNumTracks", 0, nullptr },
    { L"getCurrentIndex", 0, nullptr },
    { L"getRating", 1, nullptr },
    { L"setRating", 2, nullptr },
    { L"enqueueFile", 1, nullptr },
    { L"clear", 0, nullptr },
    { L"removeTrack", 1, nullptr },
    { L"swapTracks", 2, nullptr },
    { L"moveUp", 1, nullptr },
    { L"moveDown", 1, nullptr },
    { L"moveTo", 2, nullptr },
    { L"getTitle", 1, nullptr },
    { L"getLength", 1, nullptr },
    { L"getMetaData", 2, nullptr },
    { L"getNumSelectedTracks", 0, nullptr },
    { L"getNextSelectedTrack", 1, nullptr },
    { L"getFileName", 1, nullptr },
    { L"playTrack", 1, nullptr },
    { L"onPleditModified", 0, nullptr },
};

const ClassMethod kConfigRef[] = {
    { L"getItem", 1, nullptr },
    { L"getItemByGuid", 1, nullptr },
    { L"newItem", 2, nullptr },
};

const ClassMethod kConfigItemRef[] = {
    { L"getAttribute", 1, nullptr },
    { L"getGuid", 0, nullptr },
    { L"newAttribute", 2, nullptr },
};

const ClassMethod kConfigAttributeRef[] = {
    { L"getData", 0, nullptr },
    { L"setData", 1, nullptr },
    { L"onDataChanged", 0, nullptr },
    { L"getParentItem", 0, nullptr },
    { L"getAttributeName", 0, nullptr },
};

const ClassMethod kWinampConfigRef[] = {
    { L"getGroup", 1, nullptr },
};

const ClassMethod kWinampConfigGroupRef[] = {
    { L"getBool", 1, nullptr },
    { L"getString", 1, nullptr },
    { L"getInt", 1, nullptr },
    { L"setBool", 2, nullptr },
};

// ── Class registry ──────────────────────────────────────────────────
// Parents-first; index + CLASS_ID_BASE = the global classid handed to
// the interpreter.  The GUIDs are the interop identifiers compiled
// .maki binaries embed for these classes.
#define G(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
    { a, b, c, { d0, d1, d2, d3, d4, d5, d6, d7 } }
#define M(t) t, int(sizeof(t) / sizeof(t[0]))

const MakiClass kClasses[] = {
/* 0*/ { L"Object",         G(0x51654971,0x0d87,0x4a51,0x91,0xe3,0xa6,0xb5,0x32,0x35,0xf3,0xe7), -1, M(kObject) },
/* 1*/ { L"SystemObject",   G(0xd6f50f64,0x93fa,0x49b7,0x93,0xf1,0xba,0x66,0xef,0xae,0x3e,0x98),  0, M(kSystemObject) },
/* 2*/ { L"GuiObject",      G(0x4ee3e199,0xc636,0x4bec,0x97,0xcd,0x78,0xbc,0x9c,0x86,0x28,0xb0),  0, M(kGuiObject) },
/* 3*/ { L"Group",          G(0x45be95e5,0x2072,0x4191,0x93,0x5c,0xbb,0x5f,0xf9,0xf1,0x17,0xfd),  2, M(kGroupRef) },
/* 4*/ { L"Layout",         G(0x60906d4e,0x537e,0x482e,0xb0,0x04,0xcc,0x94,0x61,0x88,0x56,0x72),  3, M(kLayoutRef) },
/* 5*/ { L"Container",      G(0xe90dc47b,0x840d,0x4ae7,0xb0,0x2c,0x04,0x0b,0xd2,0x75,0xf7,0xfc),  0, M(kContainerRef) },
/* 6*/ { L"Button",         G(0x698eddcd,0x8f1e,0x4fec,0x9b,0x12,0xf9,0x44,0xf9,0x09,0xff,0x45),  2, M(kButtonRef) },
/* 7*/ { L"ToggleButton",   G(0xb4dccfff,0x81fe,0x4bcc,0x96,0x1b,0x72,0x0f,0xd5,0xbe,0x0f,0xff),  6, M(kToggleButtonRef) },
/* 8*/ { L"Text",           G(0xefaa8672,0x310e,0x41fa,0xb7,0xdc,0x85,0xa9,0x52,0x5b,0xcb,0x4b),  2, M(kTextRef) },
/* 9*/ { L"Edit",           G(0x64e4bbfa,0x81f4,0x49d9,0xb0,0xc0,0xa8,0x5b,0x2e,0xc3,0xbc,0xfd),  2, M(kEdit) },
/*10*/ { L"Slider",         G(0x62b65e3f,0x375e,0x408d,0x8d,0xea,0x76,0x81,0x4a,0xb9,0x1b,0x77),  2, M(kSliderRef) },
/*11*/ { L"Layer",          G(0x5ab9fa15,0x9a7d,0x4557,0xab,0xc8,0x65,0x57,0xa6,0xc6,0x7c,0xa9),  2, M(kLayer) },
/*12*/ { L"AnimatedLayer",  G(0x6b64cd27,0x5a26,0x4c4b,0x8c,0x59,0xe6,0xa7,0x0c,0xf6,0x49,0x3a), 11, M(kAnimatedLayer) },
/*13*/ { L"Timer",          G(0x5d0c5bb6,0x7de1,0x4b1f,0xa7,0x0f,0x8d,0x16,0x59,0x94,0x19,0x41),  0, M(kTimerRef) },
/*14*/ { L"PopupMenu",      G(0xf4787af4,0xb2bb,0x4ef7,0x9c,0xfb,0xe7,0x4b,0xa9,0xbe,0xa8,0x8d),  0, M(kPopupMenu) },
/*15*/ { L"Region",         G(0x3a370c02,0x3cbf,0x439f,0x84,0xf1,0x86,0x88,0x5b,0xcf,0x1e,0x36),  0, M(kRegion) },
/*16*/ { L"Map",            G(0x38603665,0x461b,0x42a7,0xaa,0x75,0xd8,0x3f,0x66,0x67,0xbf,0x73),  0, M(kMap) },
/*17*/ { L"List",           G(0xb2023ab5,0x434d,0x4ba1,0xbe,0xae,0x59,0x63,0x75,0x03,0xf3,0xc6),  0, M(kList) },
/*18*/ { L"BitList",        G(0x87c65778,0xe743,0x49fe,0x85,0xf9,0x09,0xcc,0x53,0x2a,0xfd,0x56),  0, M(kBitList) },
/*19*/ { L"Wac",            G(0x00c074a0,0xfea2,0x49a0,0xbe,0x8d,0xfa,0xbb,0xdb,0x16,0x16,0x40),  0, M(kWac) },
/*20*/ { L"Application",    G(0xb8e867b0,0x2715,0x4da7,0xa5,0xba,0x53,0xdb,0xa1,0xfc,0xfe,0xac),  0, M(kApplicationRef) },
/*21*/ { L"Color",          G(0x95ddb221,0x00e3,0x4e2b,0x8e,0xa5,0x83,0x35,0x48,0xc1,0x3c,0x10),  0, nullptr, 0 },
/*22*/ { L"ColorMgr",       G(0xaee235ff,0xebd1,0x498f,0x96,0xaf,0xd7,0xe0,0xda,0xd4,0x54,0x1a),  0, nullptr, 0 },
/*23*/ { L"GammaSet",       G(0x0d024db9,0x9574,0x42d0,0xb8,0xc7,0x26,0xb5,0x53,0xf1,0xf9,0x87),  0, nullptr, 0 },
/*24*/ { L"GammaGroup",     G(0xb81f004d,0xacba,0x453d,0xa0,0x6b,0x30,0x19,0x2a,0x1d,0xa1,0x7d),  0, nullptr, 0 },
/*25*/ { L"File",           G(0x836f8b2e,0xe0d1,0x4db4,0x93,0x7f,0x0d,0x0a,0x04,0xc8,0xdc,0xd1),  0, M(kFile) },
/*26*/ { L"XmlDoc",         G(0x417ffb69,0x987f,0x4be8,0x8d,0x87,0xd9,0x96,0x5e,0xee,0xc8,0x68), 25, M(kXmlDoc) },
/*27*/ { L"Private",        G(0x78bd6ed9,0x0dbc,0x4fa5,0xb5,0xcd,0x59,0x77,0xe3,0xa9,0x12,0xf8),  0, M(kPrivate) },
/*28*/ { L"Config",         G(0x593dba22,0xd077,0x4976,0xb9,0x52,0xf4,0x71,0x36,0x55,0x40,0x0b),  0, M(kConfigRef) },
/*29*/ { L"ConfigItem",     G(0xd4030282,0x3aab,0x4d87,0x87,0x8d,0x12,0x32,0x6f,0xad,0xfc,0xd5),  0, M(kConfigItemRef) },
/*30*/ { L"ConfigAttribute",G(0x24dec283,0xb76e,0x4a36,0x8c,0xcc,0x9e,0x24,0xc4,0x6b,0x6c,0x73),  0, M(kConfigAttributeRef) },
/*31*/ { L"WinampConfig",   G(0xb2ad3f2b,0x31ed,0x4e31,0xbc,0x6d,0xe9,0x95,0x1c,0xd5,0x55,0xbb),  0, M(kWinampConfigRef) },
/*32*/ { L"WinampConfigGroup", G(0xfc17844e,0xc72b,0x4518,0xa0,0x68,0xa8,0xf9,0x30,0xa5,0xba,0x80), 0, M(kWinampConfigGroupRef) },
/*33*/ { L"PlEdit",         G(0x345beebc,0x0229,0x4921,0x90,0xbe,0x6c,0xb6,0xa4,0x9a,0x79,0xd9),  0, M(kPlEdit) },
/*34*/ { L"Browser",        G(0xa8c2200d,0x51eb,0x4b2a,0xba,0x7f,0x5d,0x4b,0xc6,0x5d,0x4c,0x71),  2, nullptr, 0 },
/*35*/ { L"WindowHolder",   G(0x403abcc0,0x6f22,0x4bd6,0x8b,0xa4,0x10,0xc8,0x29,0x93,0x25,0x47),  2, M(kWindowHolder) },
/*36*/ { L"ObjectEmbedder", G(0x1819d795,0x7a6f,0x4f2a,0x8a,0x4d,0x7d,0xb3,0xee,0xa9,0x09,0x11),  2, nullptr, 0 },
/*37*/ { L"DropDownList",   G(0x36d59b71,0x03fd,0x4af8,0x97,0x95,0x05,0x02,0xb7,0xdb,0x26,0x7a), 36, nullptr, 0 },
/*38*/ { L"CheckBox",       G(0xc7ed3199,0x5319,0x4798,0x98,0x63,0x60,0xb1,0x5a,0x29,0x8c,0xaa),  2, M(kTextRef) },
/*39*/ { L"Frame",          G(0xe2bbc14d,0x84f6,0x4173,0xbd,0xb3,0xb2,0xeb,0x2f,0x66,0x55,0x50),  2, M(kFrameRef) },
/*40*/ { L"TabSheet",       G(0xa8f61649,0xaa6c,0x46d1,0x94,0x15,0x0a,0xe4,0x91,0x99,0x9e,0x25),  2, nullptr, 0 },
/*41*/ { L"GuiList",        G(0x6129fec1,0xdab7,0x4d51,0x91,0x65,0x01,0xca,0x0c,0x1b,0x70,0xdb),  2, M(kGuiList) },
/*42*/ { L"GuiTree",        G(0xd59514f7,0xed36,0x45e8,0x98,0x0f,0x3f,0x4e,0xa0,0x52,0x2c,0xd9),  2, nullptr, 0 },
/*43*/ { L"TreeItem",       G(0x9b3b4b82,0x667a,0x420e,0x8f,0xfc,0x79,0x41,0x15,0x80,0x9c,0x02),  0, nullptr, 0 },
/*44*/ { L"MenuButton",     G(0x1d8631c8,0x80d0,0x4792,0x9f,0x98,0xbd,0x5d,0x36,0xb4,0x91,0x36),  6, nullptr, 0 },
/*45*/ { L"Vis",            G(0xce4f97be,0x77b0,0x4e19,0x99,0x56,0xd4,0x98,0x33,0xc9,0x6c,0x27),  2, M(kVis) },
/*46*/ { L"Status",         G(0x0f08c940,0xaf39,0x4b23,0x80,0xf3,0xb8,0xc4,0x8f,0x7e,0xbb,0x59),  2, nullptr, 0 },
/*47*/ { L"Title",          G(0x7dfd3244,0x3751,0x4e7c,0xbf,0x40,0x82,0xae,0x5f,0x3a,0xdc,0x33),  2, nullptr, 0 },
/*48*/ { L"ComponentBucket",G(0x97aa3e4d,0xf4d0,0x4fa8,0x81,0x7b,0x0a,0xf2,0x2a,0x45,0x49,0x83),  2, M(kComponentBucket) },
/*49*/ { L"MouseRedir",     G(0x9b2e341b,0x6c98,0x40fa,0x8b,0x85,0x0c,0x1b,0x6e,0xe8,0x94,0x05),  2, nullptr, 0 },
/*50*/ { L"GroupList",      G(0x01e28ce1,0xb059,0x11d5,0x97,0x9f,0xe4,0xde,0x6f,0x51,0x76,0x0a),  2, nullptr, 0 },
/*51*/ { L"QueryList",      G(0xcdcb785d,0x81f2,0x4253,0x8f,0x05,0x61,0xb8,0x72,0x28,0x3c,0xfa),  2, nullptr, 0 },
/*52*/ { L"LayoutStatus",   G(0x7fd5f210,0xacc4,0x48df,0xa6,0xa0,0x54,0x51,0x57,0x6c,0xdc,0x76),  2, M(kLayoutStatus) },
/*53*/ { L"GuiList2",       G(0x0b099223,0x4eb3,0x4780,0x99,0x37,0x6d,0x21,0x03,0x72,0xf2,0xcf),  2, M(kGuiList) },
};

#undef M
#undef G

constexpr int kClassCount = int(sizeof(kClasses) / sizeof(kClasses[0]));

}  // namespace

const MakiClass *makiClassTable(int *count) {
    if (count) *count = kClassCount;
    return kClasses;
}

int makiClassIndexFromGuid(const GUID &g) {
    for (int i = 0; i < kClassCount; ++i) {
        const GUID &c = kClasses[i].guid;
        if (c.Data1 == g.Data1 && c.Data2 == g.Data2 && c.Data3 == g.Data3 &&
            c.Data4[0] == g.Data4[0] && c.Data4[1] == g.Data4[1] &&
            c.Data4[2] == g.Data4[2] && c.Data4[3] == g.Data4[3] &&
            c.Data4[4] == g.Data4[4] && c.Data4[5] == g.Data4[5] &&
            c.Data4[6] == g.Data4[6] && c.Data4[7] == g.Data4[7])
            return i;
    }
    return -1;
}

int makiClassIndexFromName(const wchar_t *name) {
    if (!name) return -1;
    for (int i = 0; i < kClassCount; ++i)
        if (ciCmp(kClasses[i].name, name) == 0) return i;
    // std.mi-facing aliases: the engine class names differ from the
    // script-facing ones for two classes.
    if (ciCmp(name, L"System") == 0)
        return makiClassIndexFromName(L"SystemObject");
    if (ciCmp(name, L"Popup") == 0)
        return makiClassIndexFromName(L"PopupMenu");
    return -1;
}

bool makiResolveScoped(int classIdx, const wchar_t *name,
                       int *nparams, void **ptr) {
    if (!name) return false;
    for (int idx = classIdx; idx >= 0 && idx < kClassCount;
         idx = kClasses[idx].parent) {
        const MakiClass &c = kClasses[idx];
        for (int m = 0; m < c.methodCount; ++m) {
            if (ciCmp(c.methods[m].name, name) != 0) continue;
            // Migration contract: explicit row pointer wins (the
            // per-class splits); else the flat table supplies pointer
            // AND arity (behaviour-identical); else honestly unbound
            // with this row's arity so the operand stack stays
            // aligned.
            if (c.methods[m].ptr) {
                *ptr     = c.methods[m].ptr;
                *nparams = c.methods[m].nparams;
                return true;
            }
            int fnp = 0;
            if (void *fp = makiFlatLookup(name, &fnp)) {
                *ptr     = fp;
                *nparams = fnp;
                return true;
            }
            *ptr     = nullptr;
            *nparams = c.methods[m].nparams;
            return true;
        }
    }
    return false;
}

}  // namespace qtWasabi::Maki
